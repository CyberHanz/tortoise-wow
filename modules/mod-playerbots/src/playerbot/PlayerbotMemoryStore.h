#ifndef _PlayerbotMemoryStore_H
#define _PlayerbotMemoryStore_H

#include "Common.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Phase 1 persistent LLM memory (see ai_updates/LLM_PERSISTENT_MEMORY_DESIGN.md
// section 4, and the Phase-1 correction round that narrowed it). One entry =
// one explicit fact a real player asked one specific bot to remember about
// that player. This store is the ONLY code path allowed to touch the
// `ai_playerbot_memory` table.
//
// Deliberately separate from AiObjectContext/llmcontext: llmcontext is
// destroyed and rebuilt on logout/AI-rebuild (PlayerbotAI.cpp ~257-258) and
// must stay that way -- it is short-term, RAM-only conversational context.
// This store's cache lives independently in this process-wide singleton, so
// it survives PlayerbotAI/AiObjectContext rebuilds and is only ever
// invalidated by an explicit write through this class.
//
// Threading: every public method takes `mutex` for the duration of its cache
// access, mirroring PlayerbotAI's own chatRepliesMutex pattern
// (PlayerbotAI.cpp, std::scoped_lock in QueueChatResponse/QueueChatContext).
// The underlying CharacterDatabase PQuery/PExecute calls are already used
// concurrently elsewhere in this codebase without extra locking.
//
// Call-site guarantee (enforced in strategy/actions/PlayerbotChatAI.cpp, NOT here):
// Remember()/Forget() are only ever invoked from
// ChatReplyAction::HandleMemoryCommand(), which is only ever invoked from
// ChatReplyAction::ChatReplyDo(), which is only ever invoked from
// PlayerbotAI::UpdateAIInternal()'s chatReplies queue-drain loop -- i.e.
// always on the bot's own tick thread, never from
// PlayerbotAI::HandleBotOutgoingPacket() or any other possibly-async packet
// handler path. HandleMemoryCommand() itself additionally requires
// IsRealPlayer(sender) == true before calling either method, so this store
// has no code path that can be reached by bot-generated chat.
struct PlayerbotMemoryEntry
{
    uint64      id = 0;
    uint64      botGuid = 0;
    uint64      subjectGuid = 0;
    std::string category;          // Phase 1 always "agreement".
    std::string factNormalized;    // trimmed/collapsed/lowercased, trailing punctuation stripped.
    std::string factOriginal;      // verbatim captured text -- what the LLM and diagnostics see.
    std::string status;            // "active" | "retracted".
    std::string sourceType;        // Phase 1 always "human_statement".
    uint64      sourceGuid = 0;    // equals subjectGuid for every Phase-1 trigger.
};

enum class PlayerbotMemoryForgetResult
{
    Forgotten,      // exactly one active match found and retracted.
    NotFound,       // no active match -- nothing changed.
    Ambiguous       // more than one active match -- nothing changed, by design (see H in the Phase-1 instructions: never mass-delete on an unclear request).
};

class PlayerbotMemoryStore
{
public:
    PlayerbotMemoryStore() {}
    virtual ~PlayerbotMemoryStore() {}
    static PlayerbotMemoryStore& instance()
    {
        static PlayerbotMemoryStore instance;
        return instance;
    }

    // Returns every ACTIVE memory row for botGuid (all subjects), loading it
    // from `ai_playerbot_memory` with one synchronous PQuery on first use per
    // botGuid per process, then serving every later call from the in-memory
    // cache. Callers filter by subjectGuid themselves (see
    // GetActiveForSubject()) -- the cache is kept per-bot, not per-(bot,
    // subject), since a bot's total Phase-1 memory count is expected to stay
    // small.
    std::vector<PlayerbotMemoryEntry> GetActive(uint64 botGuid);

    // Convenience wrapper: GetActive(botGuid) filtered to one subject, in
    // stored (chronological/id) order. Used by the <persistent memory>
    // prompt block in SayAction.cpp.
    std::vector<PlayerbotMemoryEntry> GetActiveForSubject(uint64 botGuid, uint64 subjectGuid);

    // Normalizes factText the same way a stored row would be (see
    // Normalize() in the .cpp), and either reuses an existing ACTIVE row with
    // the same (botGuid, subjectGuid, factNormalized) -- no-op, prevents
    // duplicate rows for a repeated identical "remember" -- or inserts a new
    // one. DB write happens first, then the cache is updated to match,
    // mirroring the design doc's "update DB -> update cache" ordering.
    // Returns the resulting entry (existing or newly inserted). If `wasNew`
    // is non-null, it is set to true when a new row was actually inserted
    // and false when an existing active row was reused (dedup hit) -- lets
    // the caller distinguish REMEMBER_SUCCESS from ALREADY_KNOWN for the
    // confirmation without any extra DB round-trip.
    PlayerbotMemoryEntry Remember(uint64 botGuid, uint64 subjectGuid, uint64 sourceGuid,
                                   const std::string& factOriginal, bool* wasNew = nullptr);

    // Normalizes factText and looks for an exact match among this
    // (botGuid, subjectGuid) pair's ACTIVE rows. Exactly one match: marks it
    // `status='retracted'` in the DB, removes it from the cached active
    // vector, returns Forgotten. Zero or more-than-one match: touches
    // nothing, returns NotFound/Ambiguous.
    PlayerbotMemoryForgetResult Forget(uint64 botGuid, uint64 subjectGuid, const std::string& factText);

private:
    void EnsureLoaded(uint64 botGuid);              // caller must hold mutex
    static std::string Normalize(const std::string& text);

    std::mutex mutex;
    std::unordered_map<uint64, std::vector<PlayerbotMemoryEntry>> cache;
    std::unordered_map<uint64, bool> loaded;         // botGuid -> has this bot's cache been loaded from DB yet
};

#define sPlayerbotMemoryStore PlayerbotMemoryStore::instance()

#endif
