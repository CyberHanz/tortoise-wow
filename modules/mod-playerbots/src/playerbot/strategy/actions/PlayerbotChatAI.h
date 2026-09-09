#pragma once

#include <set>
#include "playerbot/strategy/Action.h"
#include "QuestAction.h"

namespace ai
{
    typedef std::pair<WorldPacket, uint32> delayedPacket;
    typedef std::vector<delayedPacket> delayedPackets;
    typedef std::future<delayedPackets> futurePackets;

    // Phase-1 persistent memory: the authoritative, C++-determined outcome of
    // a successfully-authorized "<bot>, remember/forget that ..." command.
    // Computed strictly from PlayerbotMemoryStore::Remember()/Forget()'s
    // return values, BEFORE any LLM involvement -- the LLM is only ever
    // handed this already-decided outcome to phrase a short in-character
    // acknowledgement; it never sees or influences whether the DB write
    // itself succeeded.
    enum class MemoryCommandOutcome
    {
        REMEMBER_SUCCESS,   // a new row was inserted.
        ALREADY_KNOWN,      // Remember() deduped against an existing active row -- nothing new was written.
        FORGET_SUCCESS,     // exactly one active row was found and retracted.
        FORGET_NOT_FOUND,   // no active row matched -- nothing changed.
        FORGET_AMBIGUOUS    // more than one active row matched -- nothing changed (never guess).
    };

    // Result of HandleMemoryCommand() below. `handled == false` means: not a
    // recognised memory command, or recognised but rejected by the
    // IsRealPlayer guard or the per-bot addressing guard -- in every
    // `handled == false` case, PlayerbotMemoryStore was never touched and the
    // other fields are meaningless; ChatReplyDo() must treat the message
    // exactly like any ordinary chat line. `handled == true` means the DB
    // write/delete (or deliberate no-op, for NOT_FOUND/AMBIGUOUS) has ALREADY
    // happened by the time this is returned; `outcome`/`factText`/`language`
    // describe what to tell the player, never whether to act.
    struct MemoryCommandResult
    {
        bool handled = false;
        MemoryCommandOutcome outcome = MemoryCommandOutcome::REMEMBER_SUCCESS;
        std::string factText;   // the fact involved, verbatim (as typed by the player, after trigger-phrase stripping).
        std::string language;   // "en" or "nl" -- which trigger phrase matched. Used both for the LLM-route pre-prompt directive in ChatReplyDo() AND by the non-LLM fallback (BuildMemoryFallbackText()), which selects between an "_nl"/"_en" BOT_TEXT2 key pair per outcome -- text_loc1..8's GetLocalePriority() mechanism cannot represent Dutch (it is indexed by real WoW client locales; Dutch/nlNL is not one), so this field is the only per-message language signal available to the fallback.
    };

    class ChatReplyAction : public Action
    {
    public:
        ChatReplyAction(PlayerbotAI* ai) : Action(ai, "chat message") {}
        virtual bool Execute(Event& event) override { return true; }
        bool isUseful() override;
        virtual bool isUsefulWhenStunned() override { return true; }

        static void GetAIChatPlaceholders(std::map<std::string, std::string>& placeholders, Unit* sender = nullptr, Unit* receiver = nullptr);
        static void GetAIChatPlaceholders(std::map<std::string, std::string>& placeholders, Unit* unit, const std::string preFix = "bot", Player* observer = nullptr);
        static WorldPacket GetPacketTemplate(OpcodesList op, uint32 type, Unit* sender, Unit* target = nullptr, std::string channelName = "");
        static delayedPackets LinesToPackets(const std::vector<std::string>& lines, WorldPacket packetTemplate, bool debug = false, uint32 MsPerChar = 0, WorldPacket emoteTemplate = WorldPacket(), uint32 timeDiff = 0);

        // `fallbackText`: if non-empty and PlayerbotLLMInterface::Generate()
        // + ParseResponse() end up producing zero usable (non-empty) reply
        // lines -- Ollama unreachable (connection refused/hostname
        // resolution failure -> Generate() returns "error"), a timed-out
        // read (Generate() returns whatever partial/empty bytes it got, no
        // exception), or a response that fails to survive
        // start/end/delete-pattern extraction -- `fallbackText` is sent as
        // the reply instead of silently producing an empty packet list.
        // Evaluated entirely inside this already-async call; never triggers
        // a second Generate() call or a retry. Empty (default) reproduces
        // the exact previous behaviour: an unparseable/failed LLM response
        // silently yields no reply, which remains intentional for ordinary
        // (non-memory) chat.
        // `itemQualifiers`: Ticket 4a debug fix -- the qualifier strings
        // ChatHelper::parseItemQualifiers() resolved from the player's
        // incoming message (same set BuildItemContextBlock() used to build
        // the item-data prompt block). Applied deterministically to each
        // parsed reply line via ChatHelper::LinkifyItemMentions() before
        // LinesToPackets() below, so a plain-text item-name mention in the
        // LLM's own reply becomes a real clickable link -- the LLM itself
        // never has to produce |Hitem:...|h control codes. Empty (default)
        // reproduces the exact previous behaviour: no linkification.
        static delayedPackets GenerateResponsePackets(const std::string json
            , const WorldPacket chatTemplate, const WorldPacket emoteTemplate, const WorldPacket systemTemplate, const std::string startPattern, const std::string endPattern, const std::string deletePattern, const std::string splitPattern, bool debug = false, const std::string& fallbackText = std::string(), const std::set<std::string>& itemQualifiers = std::set<std::string>());

        static void ChatReplyDo(Player* bot, uint32 type, uint32 guid1, uint32 guid2, std::string msg, std::string chanName, std::string name);
        static void AppendPartyContextOnly(Player* bot, uint32 type, uint32 guid1, std::string msg, std::string chanName, std::string name);
        // Phase-1 persistent memory: "<bot>, remember/forget that ...".
        // Performs the DB write/delete itself (still hard-gated on
        // IsRealPlayer(sender) -- see the implementation) and returns a
        // MemoryCommandResult describing what happened. Does NOT send any
        // chat response itself -- ChatReplyDo() decides how to phrase the
        // confirmation (via the existing LLM call when eligible, or the
        // BuildMemoryFallbackText() fallback otherwise) from the returned
        // result.
        static MemoryCommandResult HandleMemoryCommand(Player* bot, ChatChannelSource chatChannelSource, std::string msg, uint32 guid1, std::string name);
        // Non-LLM, BOT_TEXT2-backed fallback confirmation text -- used when
        // the ordinary LLM-reply branch is not eligible for this message
        // (e.g. LLM chat disabled/blocked for this channel/bot), and also
        // when that branch WAS eligible but the runtime Generate() call
        // itself failed (see GenerateResponsePackets()'s fallbackText
        // parameter). Never contains hardcoded Dutch/English sentences in
        // this .cpp -- only symbolic BOT_TEXT2 keys: one pair ("_nl"/"_en")
        // per MemoryCommandOutcome, ten total, selected via result.language.
        // PlayerbotTextMgr's own text/text_loc1..8 + GetLocalePriority()
        // mechanism (used by every other bot text, e.g. "thunderfury_spam")
        // is deliberately NOT used here: text_loc1..8 are indexed by real
        // WoW client locales (WorldSession's LocaleConstant), and Dutch has
        // never been one of those, so no text_locN slot for it can exist.
        static std::string BuildMemoryFallbackText(const MemoryCommandResult& result);
        static bool HandleThunderfuryReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool HandleToxicLinksReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool HandleWTBItemsReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool HandleLFGQuestsReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name);
        static bool SendGeneralResponse(Player* bot, ChatChannelSource chatChannelSource, std::string responseMessage, std::string name);
        static std::string GenerateReplyMessage(Player* bot, std::string incomingMessage, uint32 guid1, std::string name);
    };
}
