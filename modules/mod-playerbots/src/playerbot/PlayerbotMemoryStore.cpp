
#include "playerbot/playerbot.h"
#include "PlayerbotMemoryStore.h"

#include <algorithm>
#include <cctype>

INSTANTIATE_SINGLETON_1(PlayerbotMemoryStore);

namespace
{
    PlayerbotMemoryEntry RowToEntry(Field* fields)
    {
        PlayerbotMemoryEntry entry;
        entry.id = fields[0].GetUInt64();
        entry.botGuid = fields[1].GetUInt64();
        entry.subjectGuid = fields[2].GetUInt64();
        entry.category = fields[3].GetString();
        entry.factNormalized = fields[4].GetString();
        entry.factOriginal = fields[5].GetString();
        entry.status = fields[6].GetString();
        entry.sourceType = fields[7].GetString();
        entry.sourceGuid = fields[8].GetUInt64();
        return entry;
    }
}

// Trim, collapse internal whitespace, strip a single trailing '.'/'!'/'?'/','
// and lowercase (ASCII only -- same approach ChatHelper::isNameMentioned
// already uses for its boundary checks, no locale dependency). This is the
// dedup/forget-lookup key: "Remember that my favorite city is Ironforge."
// and "remember that my favorite city is Ironforge" normalize to the same
// string, but no attempt is made at anything smarter (no stemming, no
// synonym handling) -- Phase 1 is deliberately literal, per the design
// instruction "Phase 1 hoeft niet slim te zijn. Phase 1 moet BETROUWBAAR
// zijn."
std::string PlayerbotMemoryStore::Normalize(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    bool lastWasSpace = false;
    for (unsigned char c : text)
    {
        if (std::isspace(c))
        {
            if (!out.empty() && !lastWasSpace)
                out += ' ';
            lastWasSpace = true;
            continue;
        }
        out += static_cast<char>(std::tolower(c));
        lastWasSpace = false;
    }

    while (!out.empty() && out.back() == ' ')
        out.pop_back();

    while (!out.empty() && (out.back() == '.' || out.back() == '!' || out.back() == '?' || out.back() == ','))
        out.pop_back();

    return out;
}

void PlayerbotMemoryStore::EnsureLoaded(uint64 botGuid)
{
    if (loaded[botGuid])
        return;

    std::vector<PlayerbotMemoryEntry> rows;

    auto result = CharacterDatabase.PQuery(
        "SELECT `id`,`bot_guid`,`subject_guid`,`category`,`fact_normalized`,`fact_original`,`status`,`source_type`,`source_guid` "
        "FROM `ai_playerbot_memory` WHERE `bot_guid` = '%lu' AND `status` = 'active' ORDER BY `id` ASC",
        botGuid);

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            rows.push_back(RowToEntry(fields));
        } while (result->NextRow());
    }

    cache[botGuid] = std::move(rows);
    loaded[botGuid] = true;
}

std::vector<PlayerbotMemoryEntry> PlayerbotMemoryStore::GetActive(uint64 botGuid)
{
    std::scoped_lock lock(mutex);
    EnsureLoaded(botGuid);
    return cache[botGuid];
}

std::vector<PlayerbotMemoryEntry> PlayerbotMemoryStore::GetActiveForSubject(uint64 botGuid, uint64 subjectGuid)
{
    std::scoped_lock lock(mutex);
    EnsureLoaded(botGuid);

    std::vector<PlayerbotMemoryEntry> out;
    for (auto const& entry : cache[botGuid])
        if (entry.subjectGuid == subjectGuid)
            out.push_back(entry);

    return out;
}

PlayerbotMemoryEntry PlayerbotMemoryStore::Remember(uint64 botGuid, uint64 subjectGuid, uint64 sourceGuid,
                                                      const std::string& factOriginal, bool* wasNew)
{
    std::scoped_lock lock(mutex);
    EnsureLoaded(botGuid);

    if (wasNew)
        *wasNew = false;

    std::string normalized = Normalize(factOriginal);

    for (auto const& entry : cache[botGuid])
    {
        if (entry.subjectGuid == subjectGuid && entry.factNormalized == normalized)
            return entry; // exact duplicate of an existing active memory -- reuse, no new row. wasNew stays false.
    }

    if (wasNew)
        *wasNew = true;

    std::string escOriginal = factOriginal;
    std::string escNormalized = normalized;
    CharacterDatabase.escape_string(escOriginal);
    CharacterDatabase.escape_string(escNormalized);

    CharacterDatabase.PExecute(
        "INSERT INTO `ai_playerbot_memory` "
        "(`bot_guid`,`subject_guid`,`category`,`fact_normalized`,`fact_original`,`status`,`source_type`,`source_guid`) "
        "VALUES ('%lu','%lu','agreement','%s','%s','active','human_statement','%lu')",
        botGuid, subjectGuid, escNormalized.c_str(), escOriginal.c_str(), sourceGuid);

    // `id` is intentionally left at 0 here: capturing it would need
    // LAST_INSERT_ID(), which is only reliable on the same connection that
    // ran the INSERT, and CharacterDatabase.PExecute()/PQuery() do not
    // guarantee that in this codebase (no existing call site relies on it
    // either). Forget() below therefore matches on the (bot_guid,
    // subject_guid, fact_normalized, status) columns instead of `id`, so the
    // row's numeric id is never needed after insert.
    PlayerbotMemoryEntry entry;
    entry.botGuid = botGuid;
    entry.subjectGuid = subjectGuid;
    entry.category = "agreement";
    entry.factNormalized = normalized;
    entry.factOriginal = factOriginal;
    entry.status = "active";
    entry.sourceType = "human_statement";
    entry.sourceGuid = sourceGuid;

    cache[botGuid].push_back(entry);
    return entry;
}

PlayerbotMemoryForgetResult PlayerbotMemoryStore::Forget(uint64 botGuid, uint64 subjectGuid, const std::string& factText)
{
    std::scoped_lock lock(mutex);
    EnsureLoaded(botGuid);

    std::string normalized = Normalize(factText);

    std::vector<PlayerbotMemoryEntry>& rows = cache[botGuid];
    std::vector<size_t> matches;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].subjectGuid == subjectGuid && rows[i].factNormalized == normalized)
            matches.push_back(i);

    if (matches.empty())
        return PlayerbotMemoryForgetResult::NotFound;

    if (matches.size() > 1)
        return PlayerbotMemoryForgetResult::Ambiguous; // never guess which one -- leave all of them untouched.

    // Matched by columns, not `id` (Remember() never captures the inserted
    // row's auto-increment id -- see the comment there). `LIMIT 1` is a
    // defensive backstop only: application logic already guarantees at most
    // one active (bot_guid, subject_guid, fact_normalized) row exists,
    // because Remember() dedups against the same key before inserting.
    std::string escNormalized = normalized;
    CharacterDatabase.escape_string(escNormalized);
    CharacterDatabase.PExecute(
        "UPDATE `ai_playerbot_memory` SET `status` = 'retracted' "
        "WHERE `bot_guid` = '%lu' AND `subject_guid` = '%lu' AND `fact_normalized` = '%s' AND `status` = 'active' LIMIT 1",
        botGuid, subjectGuid, escNormalized.c_str());

    rows.erase(rows.begin() + matches[0]);
    return PlayerbotMemoryForgetResult::Forgotten;
}
