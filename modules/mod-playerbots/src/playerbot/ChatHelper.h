#pragma once
#include <set>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include "Language.h"
#include "PlayerbotMemoryStore.h"

typedef std::set<uint32> ItemIds;
typedef std::set<uint32> SpellIds;

namespace ai
{
    enum BotRoles
    {
        BOT_ROLE_NONE = 0x00,
        BOT_ROLE_TANK = 0x01,
        BOT_ROLE_HEALER = 0x02,
        BOT_ROLE_DPS = 0x04
    };

    class ItemQualifier;

    class WorldPosition;
    class GuidPosition;

    class ChatHelper : public PlayerbotAIAware
    {
    public:
        ChatHelper(PlayerbotAI* ai);

    public:
        static std::string formatMoney(uint32 copper);
        static uint32 parseMoney(const std::string& text);

        static std::set<uint32> ExtractAllQuestIds(const std::string& text);
        static std::set<uint32> ExtractAllItemIds(const std::string& text);
        static std::set<uint32> ExtractAllSkillIds(const std::string& text);
        static std::set<uint32> ExtractAllFactionIds(const std::string& text);

        static std::string formatQuest(Quest const* quest);

        static std::string formatItem(ItemQualifier& itemQualifier, int count = 0, int total = 0);
        static std::string formatItem(ItemPrototype const * proto, int count = 0, int total = 0);
        static std::string formatItem(Item* item, int count = 0, int total = 0);
        static std::string formatQItem(uint32 itemId);
        static std::string formatSlot(uint8 slotId);
        static std::string formatSkill(uint32 skillId, Player* player = nullptr);
        static std::string formatReaction(ReputationRank rank, Player* player = nullptr);
        static std::string formatFaction(uint32 factionId, Player* player = nullptr);


        static ItemIds parseItems(const std::string& text, bool validate = false);
        static std::vector<uint32> parseItemsUnordered(const std::string& text, bool validate = false);
        static std::set<std::string> parseItemQualifiers(const std::string& text);
        static uint32 parseItemQuality(const std::string& text);
        static bool parseItemClass(const std::string& text, uint32* itemClass, uint32* itemSubClass);
        static uint32 parseSlot(const std::string& text);

        static std::string formatSpell(SpellEntry const *sInfo);
        // Penqle uses sSpellMgr.GetSpellEntry() instead of cmangos's sSpellTemplate.LookupEntry<SpellEntry>().
        static std::string formatSpell(uint32 spellId) {SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId); if (!spellInfo) return ""; return formatSpell(spellInfo);};
        uint32 parseSpell(std::string& text);

        static std::string formatGameobject(const GameObject* go);
        static std::list<ObjectGuid> parseGameobjects(const std::string& text);

        static std::string formatWorldobject(const WorldObject* wo);

        static std::string formatWorldEntry(int32 entry);
        static std::list<int32> parseWorldEntries(const std::string& text);

        static std::string formatQuestObjective(const std::string& name, int available, int required);

        static std::string formatValue(const std::string& type, const std::string& code, const std::string& name, const std::string& color = "0000FFFF");
        static std::string parseValue(const std::string& type, const std::string& text);

        static std::string formatChat(ChatMsg chat);
        static ChatMsg parseChat(const std::string& text);

        static BotRoles parseRole(const std::string& text);
        static std::string formatRole(BotRoles role);

        static std::string specName(const Player* player);

        static uint32 parseGender(const std::string& text);
        static std::string formatGender(uint8 gender);

        static Team parseTeam(const std::string& text);
        static std::string formatTeam(Team team);

        static uint32 parseClass(const std::string& text);
        static std::string formatClass(const Player* player, int spec);
        static std::string formatClass(uint8 cls);

        static uint32 parseRace(const std::string& text);
        static std::string formatRace(uint8 race);
        static std::string formatLifeState(Unit* unit);

        static std::string formatFactionName(uint32 factionId);

        // Case-insensitive, alphanumeric-boundary-aware check whether `name` is
        // addressed inside `message` (e.g. "Ravanne, ..." / "ravanne?" match,
        // "Ravannestad" does not). No regex: a plain scan, safe to call once per
        // group member per incoming chat message.
        static bool isNameMentioned(const std::string& message, const std::string& name);

        // Deterministic, non-LLM check for whether answering `message` requires the
        // CURRENT physical position (or relative position) of one or both characters --
        // NOT whether the message is about a place. A favorite-place question, a
        // hypothetical/desired destination, or "where is <that place>?" does NOT need
        // this; only present-tense "where are you / are you still in X / can you see me /
        // are you coming to X now" style questions do. An explicit "don't say where you
        // are" constraint overrides everything else to false.
        // The algorithm here is language-neutral: it just scans an ordered list of phrase
        // profiles (see GetLocationPhraseProfiles() in ChatHelper.cpp). English and Dutch
        // are supplied now; a future language is added there as a new profile entry --
        // this function and its logic never need to change for that.
        // Reuses isNameMentioned's boundary-aware matching unmodified; does not alter
        // isNameMentioned or addressing behavior.
        static bool needsCurrentLocationContext(const std::string& message);

        // Ticket 3a: coarse, deterministic, non-LLM RETRIEVAL gate for the
        // <persistent memory> prompt block in SayAction.cpp's ChatReplyDo().
        // Deliberately NOT a personal-info classifier and NOT per-fact
        // relevance/ranking (that is Ticket 3b) -- this only decides whether
        // the current message looks like ANY kind of information request at
        // all, so casual/status/progress chat ("ik heb nu 15 [Small Egg]")
        // stops spontaneously triggering unrelated stored facts. Same
        // phrase-profile shape as needsCurrentLocationContext() above: an
        // override-to-false tier checked first, then a positive-trigger
        // tier, both via isNameMentioned()'s boundary-safe substring match.
        // Known false positives (an unrelated question still passes, e.g.
        // "weet je hoeveel eieren ik nu heb?") and false negatives (a real
        // request phrased without "?" or a recognised question word) are
        // accepted for this coarse round -- see GetMemoryGatePhraseProfiles()
        // in ChatHelper.cpp and Ticket 3b for the planned refinement.
        static bool needsPersistentMemoryContext(const std::string& message);

        // Ticket 3b1: single relevance ENTRY POINT for the <persistent
        // memory> block in SayAction.cpp. This is deliberately the ONLY
        // piece of this mechanism SayAction.cpp (or any other caller) is
        // meant to see: "which of these stored facts are relevant to this
        // message?" All concept/topic-specific matching (the small, fixed
        // Phase-1 keyword-based concept set -- favorite color, favorite
        // city/place, best friend) is a private implementation detail kept
        // entirely inside ChatHelper.cpp's anonymous namespace, and is NOT
        // exposed here. That is intentional: a future caller must never need
        // a new special codepath per memory category, and a future, more
        // general relevance mechanism (e.g. a semantic/structured
        // representation -- subject/relation/object_type/value tuples, or
        // similar normalized semantic keys/tags) can replace the matching
        // done INSIDE this function later without this signature, or any
        // caller, having to change at all.
        //
        // allMemories is every active stored fact for the subject (already
        // fetched by the caller via PlayerbotMemoryStore -- this function
        // does no DB/cache access itself). outRelevant is cleared and, on
        // FACTS_FOUND, filled with the subset of allMemories (original order
        // preserved) judged relevant to `message`; the caller still applies
        // its own maxRows bound on top of this, AFTER this call.
        //
        // Ticket 3a already decided whether ANY memory may be shown this
        // turn (see needsPersistentMemoryContext() above) -- callers should
        // only reach this function once that coarser gate has already
        // passed.
        //
        // Result meaning (conservative-by-default, per Ticket 3b1 design):
        //   NO_KNOWN_CONCEPT       -- `message` did not look on-topic for
        //                              ANY fact this bootstrap phase can
        //                              recognise. outRelevant is left empty.
        //                              Caller must inject NOTHING at all --
        //                              no fact block, no "no facts" text --
        //                              never a permissive fallback to
        //                              showing every active fact.
        //   KNOWN_CONCEPT_NO_FACTS -- `message` looked on-topic, but none of
        //                              allMemories were judged relevant to
        //                              it. outRelevant is left empty. Caller
        //                              should show a topic-specific "no
        //                              relevant fact" message, not silence
        //                              and not the fact block.
        //   FACTS_FOUND            -- outRelevant holds at least one
        //                              relevant fact.
        //
        // IMPORTANT -- Phase-1 BOOTSTRAP scope, not final architecture:
        // today this is implemented via a small, FIXED concept set (COLOR /
        // CITY_PLACE / FRIENDSHIP) matched with boundary-safe concept-carrier
        // keywords (see GetMemoryConceptProfiles() in ChatHelper.cpp),
        // covering only what the live memory data actually uses right now.
        // These three concepts are explicitly NOT considered a complete
        // solution for memory relevance, and this is deliberately NOT grown
        // one hardcoded concept at a time into dozens of categories
        // (favorite food, dislikes, family members, hobbies, profession,
        // ...) -- that path was explicitly rejected. The real, more general
        // problem is semantic retrieval: e.g. matching a stored "Ik vind
        // Darnassus een fijne plek." against "Waar kom ik graag?"/"What
        // place do I really like?" without a new enum value per topic. This
        // function is the seam where that future mechanism (and, separately,
        // a future NATURAL/IMPLICIT MEMORY ACQUISITION mechanism that would
        // need the same semantic representation on the write side) plugs in
        // later, without touching SayAction.cpp or any other caller.
        enum class MemoryRelevanceResult
        {
            NO_KNOWN_CONCEPT,
            KNOWN_CONCEPT_NO_FACTS,
            FACTS_FOUND
        };

        static MemoryRelevanceResult FilterRelevantMemories(const std::string& message,
            const std::vector<PlayerbotMemoryEntry>& allMemories,
            std::vector<PlayerbotMemoryEntry>& outRelevant);

        // Ticket 4a: chat itemlink grounding. Resolves every |Hitem:...|h[...]|h
        // link in `message` against the in-memory item template cache --
        // ItemQualifier::GetProto(), the same sItemStorage lookup formatItem()
        // below already uses, so this is never a DB query. Returns one
        // compact, authoritative block for the LLM prompt, or an empty
        // string if `message` has no resolvable item link. Only fields
        // actually present on the resolved ItemPrototype are included --
        // nothing here is inferred or invented. Bounded to a small fixed
        // number of items so a message pasting many links cannot blow up
        // the prompt. Ticket 4a scope only: no gear/upgrade comparison
        // against the bot's own equipment, no mailbox, no extra LLM call,
        // no per-tick work (only called from the chat-reply path, on an
        // incoming message).
        static std::string BuildItemContextBlock(const std::string& message);

        // Phase-1 persistent-memory trigger action, returned by
        // detectMemoryTrigger() below. NONE means "not a memory command at
        // all" -- the message is left completely untouched and falls through
        // to normal chat handling.
        enum class MemoryTriggerAction
        {
            NONE,
            REMEMBER,
            FORGET
        };

        struct MemoryTriggerResult
        {
            MemoryTriggerAction action = MemoryTriggerAction::NONE;
            std::string factText;   // everything after the trigger phrase, trimmed. Only meaningful when action != NONE.
            std::string language;   // "en" or "nl" -- which phrase profile matched. Only meaningful when action != NONE. Lets the caller reply with a non-LLM confirmation in the same language as the trigger, without a translation step.
            std::string addressedName; // the exact leading vocative text as typed ("Ravanne" out of "Ravanne, onthoud ..."), captured BEFORE any trigger-phrase matching. Empty if no leading "<word>, "/"<word>: " vocative was present. Only meaningful when action != NONE. Callers use this -- not a whole-message name search -- to decide which bot a broadcast-channel (party/guild/raid/say/yell) memory command is actually addressed to: a scan for the bot's name anywhere in the message would also match a bot name mentioned mid-sentence ("Ravanne, onthoud dat Malurith mijn beste vriend is." must resolve to Ravanne only, never Malurith).
        };

        // Deterministic, non-LLM, conservative detector for the Phase-1
        // "remember"/"forget" commands (NL: "onthoud"/"vergeet", EN:
        // "remember"/"forget"). Same phrase-profile shape as
        // GetLocationPhraseProfiles() in ChatHelper.cpp, but anchored to the
        // START of the message (optionally after a single short leading
        // "<word>, " / "<word>: " vocative address, e.g. "Ravanne, ...") --
        // deliberately NOT a substring match anywhere in the message, so an
        // ordinary sentence that merely contains the word "remember" ("I will
        // never forget my favorite city") does not trigger. Only ever called
        // against the human's raw incoming message text; never against a
        // bot's own generated reply, and never against llmContext.
        static MemoryTriggerResult detectMemoryTrigger(const std::string& message);

        static std::string getSkillName(uint32 skill);
        static uint32 parseSkillName(const std::string& text);

        static std::string formatAngle(float angle);
        static std::string formatWorldPosition(const WorldPosition& pos, const WorldPosition refPos = WorldPosition());
        static std::string formatGuidPosition(const GuidPosition& guidP, const GuidPosition& ref);

        static std::string formatBoolean(bool flag);       
       
        static bool parseable(const std::string& text);

        void eraseAllSubStr(std::string& mainStr, const std::string& toErase);

        static void PopulateSpellNameList();
        static std::vector<uint32> SpellIds(const std::string& name);

        static std::vector<std::string> splitString(const std::string& text, const std::string& delimiter);
        static std::vector<std::string> findSubstringsBetween(const std::string& input, const std::string& start, const std::string& end, bool includeDelimiters = false);
        static void replaceSubstring(std::string& str, const std::string& oldStr, const std::string& newStr);

    private:
        static std::map<std::string, uint32> consumableSubClasses;
        static std::map<std::string, uint32> tradeSubClasses;
        static std::map<std::string, uint32> itemQualities;
        static std::map<std::string, uint32> projectileSubClasses;
        static std::map<std::string, std::pair<uint32, uint32>> itemClasses;
        static std::map<std::string, uint32> slots;
        static std::map<std::string, uint32> skills;
        static std::map<std::string, ChatMsg> chats;
        static std::map<uint8, std::string> classes;
        static std::map<uint8, std::string> races;
        static std::map<uint8, std::map<uint8, std::string> > specs;
        static std::unordered_map<std::string, std::vector<uint32>> spellIds;
    };
};
