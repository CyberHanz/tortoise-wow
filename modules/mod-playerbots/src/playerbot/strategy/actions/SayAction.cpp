
#include "playerbot/playerbot.h"
#include "SayAction.h"
#include "playerbot/PlayerbotTextMgr.h"
#include "Chat/ChannelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/AiFactory.h"
#include <regex>
#include <boost/algorithm/string.hpp>
#include "playerbot/PlayerbotLLMInterface.h"
#include "playerbot/PlayerbotMemoryStore.h"
#include "playerbot/ChatHelper.h"

using namespace ai;

std::unordered_set<std::string> noReplyMsgs = { "all ?", "attack", "attack rti", "bank", "c", "co ?", "de ?", "dead ?", "do accept invitation", "faction", "flee", "follow", "give leader", "guard", "guild leave", "help", "home", "items", "join", "jump", "leave", "lfg", "loot", "los", "nc ?", "pet aggressive", "pet defensive", "pet passive", "pet follow", "pet stay", "pet attack", "pet dismiss", "pet call", "pull", "pull rti", "quests", "quests co", "quests in", "quests all", "react ?", "release", "repair", "reset", "reset ai", "reset strats", "revive", "roll feedback", "rtsc", "rtsc cancel", "rtsc select", "skill", "spells", "stats", "stay", "summon", "talents", "talk", "trainer" "trainer learn", "u go", "who", "where" };

std::unordered_set<std::string> noReplyMsgParts = {  };

std::unordered_set<std::string> noReplyMsgStarts = { "@", "accept [", "accept |", "all +", "all -", "b [", "b |", "bank -", "bank [", "bank |", "boost target ", "buff target ", "cast ", "co +", "co -", "cs ", "d [", "d |", "dead +", "dead -", "destroy [", "destroy |", "drop ", "e [", "e |", "emote ", "faction ", "focus heal ", "follow target ", "go npc ", "go zone ", "items ", "jump ", "keep ", "mail ", "nc +", "nc -", "outfit ", "pet autocast ", "q [", "q |", "r [", "r |", "ra ", "range ", "react +", "react -", "repair [", "repair |", "revive target ", "rti ", "rtsc go ", "rtsc save ", "rtsc unsave ", "s [", "s |", "sendmail [", "sendmail |", "share [", "share |", "skill ", "skill unlearn ", "ss ", "t ", "talents ", "u [", "u |", " ue [", "ue |", "wait for attack time " };

SayAction::SayAction(PlayerbotAI* ai) : Action(ai, "say"), Qualified()
{
}

bool SayAction::Execute(Event& event)
{
    std::string text = "";
    std::map<std::string, std::string> placeholders;
    Unit* target = AI_VALUE(Unit*, "tank target");
    if (!target) target = AI_VALUE(Unit*, "current target");

    // set replace std::strings
    if (target) placeholders["<target>"] = target->GetName();
    placeholders["<randomfaction>"] = IsAlliance(bot->getRace()) ? "Alliance" : "Horde";
    if (qualifier == "low ammo" || qualifier == "no ammo")
    {
        Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (pItem)
        {
            switch (pItem->GetProto()->SubClass)
            {
            case ITEM_SUBCLASS_WEAPON_GUN:
                placeholders["<ammo>"] = "bullets";
                break;
            case ITEM_SUBCLASS_WEAPON_BOW:
            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                placeholders["<ammo>"] = "arrows";
                break;
            }
        }
    }

    if (bot->IsInWorld())
    {
        if (AreaTableEntry const* area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot)))
            placeholders["<subzone>"] = area->area_name[0];
    }

    // set delay before next say
    time_t lastSaid = AI_VALUE2(time_t, "last said", qualifier);
    uint32 nextTime = time(0) + urand(1, 30);
    ai->GetAiObjectContext()->GetValue<time_t>("last said", qualifier)->Set(nextTime);

    Group* group = bot->GetGroup();
    if (group)
    {
        std::vector<Player*> members;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            PlayerbotAI* memberAi = GetBotAI(member);
            if (memberAi) members.push_back(member);
        }

        uint32 count = members.size();
        if (count > 1)
        {
            for (uint32 i = 0; i < count * 5; i++)
            {
                int i1 = urand(0, count - 1);
                int i2 = urand(0, count - 1);

                Player* item = members[i1];
                members[i1] = members[i2];
                members[i2] = item;
            }
        }

        int index = 0;
        for (std::vector<Player*>::iterator i = members.begin(); i != members.end(); ++i)
        {
            PlayerbotAI* memberAi = GetBotAI((*i));
            if (memberAi)
                memberAi->GetAiObjectContext()->GetValue<time_t>("last said", qualifier)->Set(nextTime + (20 * ++index) + urand(1, 15));
        }
    }

    // load text based on chance
    if (!sPlayerbotTextMgr.GetBotText(qualifier, text, placeholders))
        return false;

    if (text.find("/y ") == 0)
        ai->Yell(text.substr(3));
    else
        ai->Say(text);

    return true;
}

bool SayAction::isUseful()
{
    if (!ai->AllowActivity())
        return false;

    if (ai->HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT))
        return false;

    time_t lastSaid = AI_VALUE2(time_t, "last said", qualifier);
    return (time(0) - lastSaid) > 30;
}

void ChatReplyAction::GetAIChatPlaceholders(std::map<std::string, std::string>& placeholders, Unit* sender, Unit* receiver)
{
    if(receiver)
        placeholders["<receiver name>"] = receiver->GetName();
    else
        placeholders["<receiver name>"];

    if (sender)
        placeholders["<sender name>"] = sender->GetName();
    else
        placeholders["<sender name>"];

#ifdef MANGOSBOT_ZERO
    placeholders["<expansion name>"] = "Vanilla";
#endif
#ifdef MANGOSBOT_ONE
    placeholders["<expansion name>"] = "The Burning Crusade";
#endif
#ifdef MANGOSBOT_TWO
    placeholders["<expansion name>"] = "Wrath of the Lich King";
#endif
    return;
}

void ChatReplyAction::GetAIChatPlaceholders(std::map<std::string, std::string>& placeholders, Unit* unit, const std::string preFix, Player* observer)
{
    placeholders["<" + preFix + " name>"] = unit->GetName();
    placeholders["<" + preFix + " gender>"] = unit->getGender() == GENDER_MALE ? "male" : "female";
    placeholders["<" + preFix + " level>"] = std::to_string(unit->GetLevel());
    placeholders["<" + preFix + " class>"] = ChatHelper::formatClass(unit->getClass());
    placeholders["<" + preFix + " race>"] = ChatHelper::formatRace(unit->getRace());
    placeholders["<" + preFix + " life state>"] = ChatHelper::formatLifeState(unit);

    FactionTemplateEntry const* factionTemplate = unit->GetFactionTemplateEntry();
    uint32 factionId = factionTemplate ? factionTemplate->faction : 0;

    placeholders["<" + preFix + " faction>"] = ChatHelper::formatFactionName(factionId);
    WorldPosition pos(unit);
    placeholders["<" + preFix + " zone>"] = pos.getAreaName();
    placeholders["<" + preFix + " subzone>"] = pos.getAreaOverride();

    if (unit->IsPlayer())
    {
        placeholders["<" + preFix + " type>"] = "player";
        placeholders["<" + preFix + " subname>"] = "";
        placeholders["<" + preFix + " gossip>"] = "";
    }
    if (unit->IsCreature())
    {
        Creature* creature = (Creature*)unit;

        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(creature->GetEntry());

        switch (creature->GetCreatureType())
        {
        case CREATURE_TYPE_BEAST:
            placeholders["<" + preFix + " type>"] = "beast";
            break;
        case  CREATURE_TYPE_DRAGONKIN:
            placeholders["<" + preFix + " type>"] = "dragonkin";
            break;
        case      CREATURE_TYPE_DEMON:
            placeholders["<" + preFix + " type>"] = "demon";
            break;
        case    CREATURE_TYPE_ELEMENTAL:
            placeholders["<" + preFix + " type>"] = "elemental";
            break;
        case    CREATURE_TYPE_GIANT:
            placeholders["<" + preFix + " type>"] = "giant";
            break;
        case   CREATURE_TYPE_UNDEAD:
            placeholders["<" + preFix + " type>"] = "undead";
            break;
        case  CREATURE_TYPE_HUMANOID:
            placeholders["<" + preFix + " type>"] = "humanoid";
            break;
        case  CREATURE_TYPE_CRITTER:
            placeholders["<" + preFix + " type>"] = "critter";
            break;
        case  CREATURE_TYPE_MECHANICAL:
            placeholders["<" + preFix + " type>"] = "mechanical";
            break;
        case  CREATURE_TYPE_NOT_SPECIFIED:
            placeholders["<" + preFix + " type>"] = "being";
            break;
        case  CREATURE_TYPE_TOTEM:
            placeholders["<" + preFix + " type>"] = "totem";
            break;
        }

        placeholders["<" + preFix + " subname>"] = creature->GetSubName();

        std::string gossipText = placeholders["<" + preFix + " gossip>"];


        GossipMenusMapBounds pMenuBounds = sObjectMgr.GetGossipMenusMapBounds(creature->GetDefaultGossipMenuId());
        GossipMenuItemsMapBounds pMenuItemBounds = sObjectMgr.GetGossipMenuItemsMapBounds(creature->GetDefaultGossipMenuId());

        for (auto& gossip = pMenuBounds.first; gossip != pMenuBounds.second; gossip++)
        {
            const GossipText* gos = sObjectMgr.GetGossipText(gossip->second.text_id);
            gossipText += " " + gos->Options->Text_0;
        }

        uint32 textId = observer->GetGossipTextId(creature);

        if (textId)
        {
            const GossipText* gos = sObjectMgr.GetGossipText(textId);
            if (gos)
                gossipText += " " + gos->Options->Text_0;
        }

        for (auto& gossip = pMenuItemBounds.first; gossip != pMenuItemBounds.second; gossip++)
        {
            gossipText += " " + gossip->second.option_text;
        }

        std::map<std::string, std::string> replace;
        replace["<"] = "*";
        replace[">"] = "*";
        replace["$N"] = observer->GetName();
        replace["$B"] = "";
        replace["$c"] = ChatHelper::formatRace(observer->getRace());
        replace["$r"] = ChatHelper::formatClass(unit->getClass());
        replace["$g boy : girl;"] = unit->getGender() == GENDER_MALE ? "boy" : "girl"; //Todo replace with regexp
        replace["$g lad : lass;"] = unit->getGender() == GENDER_MALE ? "lass" : "lad";

        replace["GOSSIP_OPTION_GOSSIP"] = unit->GetName() + std::string(" can chat some.");
        replace["GOSSIP_OPTION_QUESTGIVER"] = unit->GetName() + std::string(" can offer quests.");
        replace["GOSSIP_OPTION_VENDOR"] = unit->GetName() + std::string(" can sell and buy items.");
        replace["GOSSIP_OPTION_TAXIVENDOR"] = unit->GetName() + std::string(" is a flight master.");
        replace["GOSSIP_OPTION_TRAINER"] = unit->GetName() + std::string(" can train certain skills.");
        replace["GOSSIP_OPTION_SPIRITHEALER"] = unit->GetName() + std::string(" can revive de dead.");
        replace["GOSSIP_OPTION_SPIRITGUIDE"] = unit->GetName() + std::string(" can revive de dead.");
        replace["GOSSIP_OPTION_INNKEEPER"] = unit->GetName() + std::string(" runs an inn.");
        replace["GOSSIP_OPTION_BANKER"] = unit->GetName() + std::string(" can store items in the bank.");
        replace["GOSSIP_OPTION_PETITIONER"] = unit->GetName() + std::string(" can create new guilds.");
        replace["GOSSIP_OPTION_TABARDDESIGNER"] = unit->GetName() + std::string(" can redesign the guild tabard.");
        replace["GOSSIP_OPTION_BATTLEFIELD"] = unit->GetName() + std::string(" recruits to join the battlegrounds.");
        replace["GOSSIP_OPTION_AUCTIONEER"] = unit->GetName() + std::string(" is an auctioneer.");
        replace["GOSSIP_OPTION_STABLEPET"] = unit->GetName() + std::string(" can store pets.");
        replace["GOSSIP_OPTION_ARMORER"] = unit->GetName() + std::string(" can repair armor.");
        replace["GOSSIP_OPTION_UNLEARNTALENTS"] = unit->GetName() + std::string(" can help unlearning talents.");
        replace["GOSSIP_OPTION_TRAINER"] = unit->GetName() + std::string(" can train pets.");
        replace["GOSSIP_OPTION_UNLEARNPETSKILLS"] = unit->GetName() + std::string(" can help pets unlearn their skills.");

        PlayerbotTextMgr::ReplacePlaceholders(gossipText, replace);

        placeholders["<" + preFix + " gossip>"] = gossipText;
    }
}

WorldPacket ChatReplyAction::GetPacketTemplate(OpcodesList op, uint32 type, Unit* sender, Unit* target, std::string channelName)
{
    Player* senderPlayer = (sender->IsPlayer()) ? (Player*)sender : nullptr;
    ObjectGuid senderGuid = sender->GetObjectGuid();
    ObjectGuid targetGuid = target ? target->GetObjectGuid() : ObjectGuid();
    Player* targetPlayer = (target && target->IsPlayer()) ? (Player*)target : nullptr;
    const char* senderName = sender->GetName();

    WorldPacket packetTemplate(op);

    if (op == CMSG_MESSAGECHAT)
        packetTemplate << type;
    else
        packetTemplate << uint8(type);

    if (senderPlayer)
        packetTemplate << ((senderPlayer->GetTeam() == ALLIANCE) ? LANG_COMMON : LANG_ORCISH);
    else if (targetPlayer)
        packetTemplate << ((targetPlayer->GetTeam() == ALLIANCE) ? LANG_COMMON : LANG_ORCISH);
    else
        packetTemplate << LANG_UNIVERSAL;

    if (op == CMSG_MESSAGECHAT)
    {
        if (type == CHAT_MSG_WHISPER)
            packetTemplate << target->GetName();

        if (!channelName.empty())
            packetTemplate << channelName;
    }


    if (op != CMSG_MESSAGECHAT)
    {
        switch (type)
        {
        case CHAT_MSG_MONSTER_WHISPER:
        case CHAT_MSG_RAID_BOSS_WHISPER:
        case CHAT_MSG_RAID_BOSS_EMOTE:
        case CHAT_MSG_MONSTER_EMOTE:
            packetTemplate << ObjectGuid(senderGuid); //Deviation from standards. To support emotes.
            packetTemplate << uint32(strlen(senderName) + 1);
            packetTemplate << senderName;
            packetTemplate << ObjectGuid(targetGuid);
            break;

        case CHAT_MSG_SAY:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_YELL:
            packetTemplate << ObjectGuid(senderGuid);
            packetTemplate << ObjectGuid(senderGuid);
            break;

        case CHAT_MSG_MONSTER_SAY:
        case CHAT_MSG_MONSTER_YELL:
            MANGOS_ASSERT(senderName);
            packetTemplate << ObjectGuid(senderGuid);
            packetTemplate << uint32(strlen(senderName) + 1);
            packetTemplate << senderName;
            packetTemplate << ObjectGuid(targetGuid);
            break;
        default:
            packetTemplate << ObjectGuid(senderGuid);
            break;
        }
    }
    return packetTemplate;
}

inline void LineToPacket(delayedPackets& delayedPackets, const WorldPacket packetTemplate, const std::string& line, uint32 MsDelay, bool debug = false)
{
    WorldPacket packet(packetTemplate);
    if (packetTemplate.GetOpcode() != CMSG_MESSAGECHAT)
        packet << uint32(line.size() + 1 + (debug ? 2 : 0));
    packet << ((debug ? "d:" : "") + line);

    if (packetTemplate.GetOpcode() != CMSG_MESSAGECHAT)
        packet << CHAT_TAG_NONE;

    delayedPackets.push_back(std::make_pair(packet, MsDelay));
}

delayedPackets ChatReplyAction::LinesToPackets(const std::vector<std::string>& lines, WorldPacket packetTemplate, bool debug, uint32 MsPerChar, WorldPacket emoteTemplate, uint32 timeDiff)
{
    delayedPackets delayedPackets;

    WorldPacket packet;
    for (auto& line : lines)
    {
        bool isEmote = line.find('*') == 0 || line.find('[') == 0;

        std::string sentence = line;
        while (sentence.length() > 200) {
            size_t splitPos = sentence.rfind(' ', 200);
            if (splitPos == std::string::npos) {
                splitPos = 200;
            }

            sentence = std::regex_replace(sentence, std::regex("\\*"), "");
            sentence = std::regex_replace(sentence, std::regex("\\["), "");
            sentence = std::regex_replace(sentence, std::regex("\\]"), "");

            if ((!isEmote || !emoteTemplate.empty()) && !sentence.substr(0, splitPos).empty())
            {
                auto sentenceSplit = sentence.substr(0, splitPos);
                auto delay = sentenceSplit.size() * MsPerChar;
                if (timeDiff)
                {
                    if (timeDiff >= delay)
                    {
                        delay = 0;
                    }
                    else
                    {
                        delay -= timeDiff;
                    }
                    timeDiff = 0;
                }

                LineToPacket(delayedPackets, isEmote ? emoteTemplate : packetTemplate, sentenceSplit, delay, debug);
            }

            sentence = sentence.substr(splitPos + 1);
        }

        if ((!isEmote || !emoteTemplate.empty()) && !sentence.empty())
        {
            auto delay = sentence.size() * MsPerChar;
            if (timeDiff)
            {
                if (timeDiff >= delay)
                {
                    delay = 0;
                    sLog.outError("delay packet removed: %lu", delay);
                }
                else
                {
                    delay -= timeDiff;
                    sLog.outError("delay packet reduced to %lu", delay);
                }
                timeDiff = 0;
            }
            LineToPacket(delayedPackets, isEmote ? emoteTemplate : packetTemplate, sentence, delay, debug);
        }
    }
    return delayedPackets;
}

delayedPackets ChatReplyAction::GenerateResponsePackets(const std::string json
    , const WorldPacket chatTemplate, const WorldPacket emoteTemplate, const WorldPacket systemTemplate, const std::string startPattern, const std::string endPattern, const std::string deletePattern, const std::string splitPattern, bool debug, const std::string& fallbackText)
{
    std::vector<std::string> debugLines;

    if (debug)
        debugLines = { json };

    auto startTime = time(nullptr);

    std::string response = PlayerbotLLMInterface::Generate(json, sPlayerbotAIConfig.llmGenerationTimeout, sPlayerbotAIConfig.llmMaxSimultaniousGenerations, debugLines);

    auto timeAfter = time(nullptr);
    auto timeDiff = (timeAfter - startTime) * IN_MILLISECONDS;

    std::vector<std::string> lines = PlayerbotLLMInterface::ParseResponse(response, startPattern, endPattern, deletePattern, splitPattern, debugLines);

    delayedPackets packets, debugPackets;

    packets = LinesToPackets(lines, chatTemplate, false, 50, emoteTemplate, timeDiff);

    // Runtime LLM-failure fallback (connection refused, timeout, HTTP/error
    // status, or an empty/unparseable body all collapse to the same
    // observable symptom here: zero non-empty reply lines survived
    // ParseResponse()+LinesToPackets()). Only takes effect when the caller
    // supplied a non-empty fallbackText -- currently only the Phase-1
    // memory-command path does, so ordinary chat replies keep their
    // existing silent-drop-on-failure behaviour unchanged. No second
    // Generate() call, no retry -- this only formats already-known text
    // through the same LinesToPackets() used for a normal reply.
    if (packets.empty() && !fallbackText.empty())
        packets = LinesToPackets({ fallbackText }, chatTemplate, false, 50, emoteTemplate, timeDiff);

    if (!debugLines.empty())
    {
        debugPackets = LinesToPackets(debugLines, systemTemplate, true, 1);
        packets.insert(packets.begin(), std::make_move_iterator(debugPackets.begin()), std::make_move_iterator(debugPackets.end()));
    }

    return packets;
}

// Structural subject-context fix (2026-09-07). Pure label/formatting helpers
// over data that already flows through ChatReplyDo()/AppendPartyContextOnly()
// (type, chatChannelSource, chanName, guid1) -- neither function decides or
// discovers channel identity; that is still entirely GetChatChannelSource()'s
// and chanName's job. Only in scope: SAY/YELL/PARTY/GUILD and the named
// CHAT_MSG_CHANNEL channels (General/Trade/LocalDefense/WorldDefense/
// LookingForGroup/GuildRecruitment/World) -- these are the only sources that
// reach ChatReplyDo() today (HandleBotOutgoingPacket()'s msgtype switch in
// PlayerbotAI.cpp does not forward CHAT_MSG_RAID or CHAT_MSG_OFFICER, and
// there is no SRC_OFFICER; an unrecognised custom channel resolves to
// SRC_UNDEFINED, which ChatReplyDo()'s own gate below already excludes).
// RAID/OFFICER/custom-channel support is a separate, deliberately deferred
// follow-up -- not touched here.
static std::string SubjectChannelPrefix(ChatChannelSource src)
{
    switch (src)
    {
    case ChatChannelSource::SRC_SAY:        return "SAY";
    case ChatChannelSource::SRC_YELL:       return "YELL";
    case ChatChannelSource::SRC_PARTY:      return "PARTY";
    case ChatChannelSource::SRC_RAID:       return "RAID";       // not reachable today, see comment above
    case ChatChannelSource::SRC_GUILD:      return "GUILD";
    case ChatChannelSource::SRC_EMOTE:      return "EMOTE";
    case ChatChannelSource::SRC_TEXT_EMOTE: return "TEXT_EMOTE";
    default:
        // Same numeric encoding the existing shared llmChannel key already
        // uses (std::to_string(chatChannelSource)) -- no new mechanism, just
        // no literal name for this case.
        return std::to_string((int)src);
    }
}

static std::string BuildSubjectKey(uint32 type, ChatChannelSource src, const std::string& chanName, uint32 guid1)
{
    if (type == CHAT_MSG_CHANNEL)
    {
        // A non-empty chanName is the real, per-instance channel identity
        // (e.g. distinguishes "LocalDefense - Elwynn Forest" from
        // "LocalDefense - Westfall", which both resolve to the same
        // SRC_LOCAL_DEFENSE). An empty chanName here would be unusual, but
        // must NOT silently collapse into "CHANNEL::<guid>" -- two different
        // unnamed channels could then collide. Fall back to the numeric
        // source identity instead, same disjoint-from-llmChannel reasoning
        // as SubjectChannelPrefix()'s default case.
        if (!chanName.empty())
            return "CHANNEL:" + chanName + ":" + std::to_string(guid1);

        return "CHANNELSRC:" + std::to_string((int)src) + ":" + std::to_string(guid1);
    }

    return SubjectChannelPrefix(src) + ":" + std::to_string(guid1);
}

// Ticket 2 (named-subject persistent memory, CONSERVATIEVE resolver): one
// deduplicated candidate set (group members UNION this bot's own known
// memory-subject GUIDs) for possessive-only subject matching. Deliberately
// NOT staged (group-first, known-subjects-as-fallback) -- a bare-mentioned
// online group member must never be allowed to block a possessive-marked
// offline known-subject from being considered, and vice versa; both sources
// feed one flat, GUID-deduped list before any matching happens.
// No DB query, no realm-wide scan: GetActive() is the existing per-bot
// mutex-protected cache (PlayerbotMemoryStore.h), and the group walk is the
// same GetFirstMember()/getSource() pattern PlayerbotAI.cpp already uses for
// namedOtherMemberGuids. Event-driven, per human chat turn only.
struct MemorySubjectCandidate
{
    ObjectGuid guid;
    std::string name;
};

static std::vector<MemorySubjectCandidate> BuildMemorySubjectCandidates(Player* bot)
{
    std::vector<MemorySubjectCandidate> candidates;
    auto addCandidate = [&](ObjectGuid guid)
    {
        if (!guid || guid == bot->GetObjectGuid())
            return; // bot itself is never a memory subject candidate

        for (auto const& c : candidates)
            if (c.guid == guid)
                return; // already present -- dedupe by GUID

        std::string resolvedName;
        if (sObjectMgr.GetPlayerNameByGUID(guid, resolvedName) && !resolvedName.empty())
            candidates.push_back({ guid, resolvedName });
    };

    if (Group* group = bot->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->getSource())
                addCandidate(member->GetObjectGuid());

    for (auto const& mem : sPlayerbotMemoryStore.GetActive(bot->GetObjectGuid().GetRawValue()))
        addCandidate(ObjectGuid(mem.subjectGuid));

    return candidates;
}

// Ticket 2: the ONLY subject-switching signal in this first conservative
// version is explicit possessive syntax "<Name>'s" / "<Name>'s" (straight
// and curly apostrophe). Reuses ChatHelper::isNameMentioned() UNMODIFIED --
// its existing alnum-boundary check already treats the apostrophe as a
// boundary character, so composing "name + \"'s\"" as the match target is
// enough; no new low-level string-matching logic. Bare-name matching and a
// generic "van X" rule are deliberately NOT implemented here (the latter is
// provenance-unsafe: "Wat vindt Dyona van Naomanda?" must keep subject=Dyona,
// so a blanket "van <name>" rule would inject the wrong subject).
enum class MemorySubjectResolution
{
    Default,    // no possessive marker found -- caller stays at current speaker
    Resolved,   // exactly one unique possessive match -- switch to it
    Ambiguous   // 2+ distinct possessive matches -- caller must skip the memory block entirely
};

static MemorySubjectResolution ResolveMemorySubject(const std::string& message,
    const std::vector<MemorySubjectCandidate>& candidates, ObjectGuid& outGuid, std::string& outName)
{
    const MemorySubjectCandidate* match = nullptr;
    for (auto const& c : candidates)
    {
        if (ChatHelper::isNameMentioned(message, c.name + "'s") ||
            ChatHelper::isNameMentioned(message, c.name + "`s") ||
            ChatHelper::isNameMentioned(message, c.name + "’s"))
        {
            if (match)
                return MemorySubjectResolution::Ambiguous; // second distinct possessive candidate this turn
            match = &c;
        }
    }

    if (!match)
        return MemorySubjectResolution::Default;

    outGuid = match->guid;
    outName = match->name;
    return MemorySubjectResolution::Resolved;
}

void ChatReplyAction::ChatReplyDo(Player* bot, uint32 type, uint32 guid1, uint32 guid2, std::string msg, std::string chanName, std::string name)
{
    // if we're just commanding bots around, don't respond...
    // first one is for exact word matches
    if (noReplyMsgs.find(msg) != noReplyMsgs.end())
    {
        //ostringstream out;
        //out << "DEBUG ChatReplyDo decided to ignore exact blocklist match" << msg;
        //bot->Say(out.str(), LANG_UNIVERSAL);
        return;
    }

    // second one is for partial matches like + or - where we change strats
    if (std::any_of(noReplyMsgParts.begin(), noReplyMsgParts.end(), [&msg](const std::string& part) { return msg.find(part) != std::string::npos; }))
    {
        //ostringstream out;
        //out << "DEBUG ChatReplyDo decided to ignore partial blocklist match" << msg;
        //bot->Say(out.str(), LANG_UNIVERSAL);

        return;
    }

    if (std::any_of(noReplyMsgStarts.begin(), noReplyMsgStarts.end(), [&msg](const std::string& start) {
        return msg.find(start) == 0;  // Check if the start matches the beginning of msg
        }))
    {
        //ostringstream out;
        //out << "DEBUG ChatReplyDo decided to ignore start blocklist match" << msg;
        //bot->Say(out.str(), LANG_UNIVERSAL);
        return;
    }

    ChatChannelSource chatChannelSource = GetBotAI(bot)->GetChatChannelSource(bot, type, chanName);

    // Phase-1 persistent memory: checked before every other special-case
    // handler below. HandleMemoryCommand() itself re-derives the sender from
    // guid1, hard-requires IsRealPlayer(sender) == true, and requires the
    // message's leading vocative to name THIS bot specifically (see
    // ChatHelper::detectMemoryTrigger()'s addressedName) before it will touch
    // PlayerbotMemoryStore -- so a bot-authored "onthoud"/"remember" string,
    // and a command explicitly addressed to a DIFFERENT bot overheard on the
    // same broadcast channel, can never write or delete a memory here.
    // `memoryResult.handled == false` (not a memory command, rejected by
    // IsRealPlayer, or addressed to a different bot) falls straight through
    // to every handler below completely unchanged, exactly as before this
    // existed. `memoryResult.handled == true` means the DB write/delete (or
    // deliberate no-op) has ALREADY happened by this point -- what follows
    // only decides how to phrase the confirmation, never whether to act; see
    // the "<pre prompt>" directive further down and the fallback near the
    // end of this function.
    MemoryCommandResult memoryResult = HandleMemoryCommand(bot, chatChannelSource, msg, guid1, name);

    if ((boost::algorithm::istarts_with(msg, "LFG") || boost::algorithm::istarts_with(msg, "LFM"))
        && HandleLFGQuestsReply(bot, chatChannelSource, msg, name))
    {
        return;
    }

    if ((boost::algorithm::istarts_with(msg, "WTB"))
        && HandleWTBItemsReply(bot, chatChannelSource, msg, name))
    {
        return;
    }

    //toxic links
    if (boost::algorithm::istarts_with(msg, sPlayerbotAIConfig.toxicLinksPrefix)
        && (GetBotAI(bot)->GetChatHelper()->ExtractAllItemIds(msg).size() > 0 || GetBotAI(bot)->GetChatHelper()->ExtractAllQuestIds(msg).size() > 0))
    {
        HandleToxicLinksReply(bot, chatChannelSource, msg, name);
        return;
    }

    //thunderfury
    if (GetBotAI(bot)->GetChatHelper()->ExtractAllItemIds(msg).count(19019))
    {
        HandleThunderfuryReply(bot, chatChannelSource, msg, name);
        return;
    }

    if (GetBotAI(bot) && sPlayerbotAIConfig.llmEnabled > 0 && (GetBotAI(bot)->HasStrategy("ai chat", BotState::BOT_STATE_NON_COMBAT) || sPlayerbotAIConfig.llmEnabled == 3) && chatChannelSource != ChatChannelSource::SRC_UNDEFINED && sPlayerbotAIConfig.llmBlockedReplyChannels.find(chatChannelSource) == sPlayerbotAIConfig.llmBlockedReplyChannels.end()
        )
    {
        Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid1));

        PlayerbotAI* ai = GetBotAI(bot);
        AiObjectContext* context = ai->GetAiObjectContext();

        if (!chanName.empty() && !ai->ChannelHasRealPlayer(chanName))
            player = nullptr;

        std::string llmChannel;

        if (!sPlayerbotAIConfig.llmGlobalContext)
            llmChannel = ((chatChannelSource == ChatChannelSource::SRC_WHISPER) ? name : std::to_string(chatChannelSource));

        std::string llmContext = AI_VALUE(std::string, "manual string::llmcontext" + llmChannel);

        // Structural subject-context fix. llmContext above is left completely
        // alone -- same key, same reads/writes below -- kept for backward
        // compatibility and any future party-ambient use. subjectContext is a
        // SEPARATE local string, read from a disjoint key namespace ("manual
        // string::llmcontext_subject", never "manual string::llmcontext"), so
        // it can never collide with, or be accidentally saved over, llmContext.
        // Gate: real player only (bot-authored messages must not populate a
        // player's subject bucket) and non-whisper only (whisper's existing
        // llmChannel==name key is already per-speaker-isolated, so a second
        // bucket for it would be redundant -- left unchanged this round).
        bool useSubjectContext = player && IsRealPlayer(player) && chatChannelSource != ChatChannelSource::SRC_WHISPER;
        std::string subjectKey = BuildSubjectKey(type, chatChannelSource, chanName, guid1);
        std::string subjectContext = useSubjectContext
            ? AI_VALUE2(std::string, "manual string::llmcontext_subject", subjectKey)
            : std::string();

        if (player)
        {
            std::string playerName = player->GetName();

            if (player != bot && (IsRealPlayer(player) || (sPlayerbotAIConfig.llmBotToBotChatChance && urand(0, 99) < sPlayerbotAIConfig.llmBotToBotChatChance)))
            {
                std::map<std::string, std::string> placeholders;

                GetAIChatPlaceholders(placeholders, bot, player);
                GetAIChatPlaceholders(placeholders, bot, "bot");
                GetAIChatPlaceholders(placeholders, player, "other");

                // --- Authoritative identity context (race/class/faction/life-state) ---
                // Always present: these are fixed server-derived facts the model must
                // never reinterpret, translate as creature type, or contradict.
                placeholders["<authoritative identity context>"] =
                    placeholders["<bot name>"] + ": race=" + placeholders["<bot race>"] +
                    ", class=" + placeholders["<bot class>"] +
                    ", faction=" + placeholders["<bot faction>"] +
                    ", life-state=" + placeholders["<bot life state>"] + "; " +
                    placeholders["<other name>"] + ": race=" + placeholders["<other race>"] +
                    ", class=" + placeholders["<other class>"] +
                    ", faction=" + placeholders["<other faction>"] +
                    ", life-state=" + placeholders["<other life state>"] + ".";

                // --- Location context (conditional) ---
                // Only populated when the message requires CURRENT spatial state (see
                // ChatHelper::needsCurrentLocationContext); otherwise left empty so the
                // model receives no location data at all for this turn.
                placeholders["<location context>"] = "";
                if (ChatHelper::needsCurrentLocationContext(msg))
                {
                    placeholders["<location context>"] = "Current location: " + placeholders["<bot name>"] +
                        " is in " + placeholders["<bot subzone>"] + ", " + placeholders["<bot zone>"] + "; " +
                        placeholders["<other name>"] + " is in " + placeholders["<other subzone>"] + ", " +
                        placeholders["<other zone>"] + ".";
                }

                placeholders["<chat addressing note>"] = "";
                if (guid2)
                {
                    if (Player* addressed = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid2)))
                    {
                        std::string addressedName = addressed->GetName();
                        placeholders["<chat addressing note>"] =
                            "This message was addressed to " + addressedName + ", not to you. You are interjecting as a "
                            "third party -- do not treat statements, questions, insults, praise, or descriptions about " +
                            addressedName + " as if they were about you. Respond from the perspective of an outside "
                            "participant in their conversation.";
                    }
                }

                std::map<ChatChannelSource, std::string> sourceName;
                sourceName[ChatChannelSource::SRC_GUILD] = "in guild chat";
                sourceName[ChatChannelSource::SRC_WORLD] = "in world chat";
                sourceName[ChatChannelSource::SRC_GENERAL] = "in the general channel";
                sourceName[ChatChannelSource::SRC_TRADE] = "in the trade channel";
                sourceName[ChatChannelSource::SRC_LOOKING_FOR_GROUP] = "in looking for group";
                sourceName[ChatChannelSource::SRC_LOCAL_DEFENSE] = "in the local defence channel";
                sourceName[ChatChannelSource::SRC_WORLD_DEFENSE] = "in the world defence channel";
                sourceName[ChatChannelSource::SRC_GUILD_RECRUITMENT] = "in guild recruitement";
                sourceName[ChatChannelSource::SRC_SAY] = "directly";
                sourceName[ChatChannelSource::SRC_WHISPER] = "in private message";
                sourceName[ChatChannelSource::SRC_EMOTE] = "with body language";
                sourceName[ChatChannelSource::SRC_TEXT_EMOTE] = "with an emote";
                sourceName[ChatChannelSource::SRC_YELL] = "with a yell";
                sourceName[ChatChannelSource::SRC_PARTY] = "in party chat";
                sourceName[ChatChannelSource::SRC_RAID] = "in raid chat";

                placeholders["<channel name>"] = sourceName[chatChannelSource];


                placeholders["<initial message>"] = msg;

                std::string llmPromptCustom = AI_VALUE(std::string, "manual saved string::llmdefaultprompt");

                std::map<std::string, std::string> jsonFill;
                jsonFill["<pre prompt>"] = sPlayerbotAIConfig.llmPrePrompt + " " + llmPromptCustom;

                // Ronde 8b/10/11: per-turn current-speaker / language
                // framing (unconditional part). Injected on EVERY
                // real-player (or bot-to-bot) turn that reaches this branch.
                // Ronde 11: the certainty/provenance rule that used to live
                // here unconditionally ("Be certain only from ... No match
                // ... means you don't know") is REMOVED from this
                // unconditional block -- proven live to override a correct,
                // already-injected persistent-memory fact (an English
                // question with a matching memory still got "I don't know
                // that for sure"). That rule now lives ONLY inside the
                // memories-conditional branches below (see the
                // GetActiveForSubject()/persistent-memory block just after
                // this), so the model never reads an unconditional
                // "say you don't know" instruction before it knows whether a
                // matching fact exists. Current-speaker naming and the
                // ronde-10 language rules are unaffected by this change.
                // This is deliberately a "<pre prompt>" addition (read by
                // the model BEFORE "<context>", per the
                // "<pre prompt><context><prompt><post prompt>" template
                // order) -- no DB/schema change, no extra LLM call, and
                // never leaks back into llmContext since only "<prompt>" is
                // folded back in.
                jsonFill["<pre prompt>"] += " Current speaker: " + playerName +
                    " (\"I\"/\"my\"/\"ik\"/\"mijn\" in their message below = " + playerName + "). "
                    "Reply in the SAME language " + playerName + " just used in their message below -- Dutch message -> Dutch reply, English message -> English reply. "
                    "Earlier context, your own earlier replies, and your character/personality text are NOT reasons to switch language -- only THIS message decides. "
                    "Keep your personality and character exactly the same; only the language of your words follows " + playerName + "'s message.";

                // Ticket 4a: chat itemlink grounding. Independent of the
                // persistent-memory blocks below (Ticket 2/3a/3b1, which are
                // untouched by this) -- an itemlink is grounded whenever the
                // message contains one, not only when the message looks
                // like a question. ChatHelper::BuildItemContextBlock() does
                // all resolution/formatting itself; this call site only
                // appends its result when non-empty, so SayAction.cpp does
                // not contain any item-field-specific logic.
                std::string itemContextBlock = ChatHelper::BuildItemContextBlock(msg);
                if (!itemContextBlock.empty())
                {
                    jsonFill["<pre prompt>"] += " " + itemContextBlock +
                        " itemdata is authoritative; verzin geen ingrediënten, lore, effecten of item-acties die niet in deze data/context staan.";
                }

                // Phase-1 persistent memory read-flow. Only for a genuine
                // real-player conversation partner -- never for a bot-to-bot
                // LLM reply, even on the rare path where
                // AiPlayerbot.LLMBotToBotChatChance > 0 let one reach this
                // branch above (player != bot && (IsRealPlayer(player) ||
                // bot-to-bot roll)) -- matches the "wanneer een ECHTE speler
                // een LLM-bot aanspreekt" scoping requirement exactly.
                // sPlayerbotMemoryStore.GetActiveForSubject() is a cached,
                // mutex-protected read (see PlayerbotMemoryStore.h); this
                // call happens on the bot's own tick thread, same as
                // everything else in ChatReplyDo().
                // Appended directly here instead of via a literal
                // "<persistent memory>" token in AiPlayerbot.LLMPrePrompt, so
                // Phase 1 works without any config edit or reload. If a
                // future config revision adds that token to LLMPrePrompt
                // itself, remove this append first -- otherwise the block
                // would be included twice.
                // Ticket 2 (named-subject persistent memory, CONSERVATIEVE
                // resolver): memorySubjectGuid/memorySubjectName default to
                // the current speaker (unchanged Phase-1 behavior) and only
                // switch on an explicit, unambiguous possessive match --
                // see ResolveMemorySubject() above. playerName itself is
                // left completely untouched: it still drives the separate
                // "Current speaker: ..." language-framing text elsewhere in
                // this function, which is about who is physically talking
                // right now, not about whose memory this block is about.
                bool skipMemoryBlock = false;
                ObjectGuid memorySubjectGuid;
                std::string memorySubjectName;
                if (IsRealPlayer(player))
                {
                    memorySubjectGuid = player->GetObjectGuid();
                    memorySubjectName = playerName;

                    auto memorySubjectCandidates = BuildMemorySubjectCandidates(bot);
                    ObjectGuid resolvedGuid;
                    std::string resolvedName;
                    switch (ResolveMemorySubject(msg, memorySubjectCandidates, resolvedGuid, resolvedName))
                    {
                    case MemorySubjectResolution::Resolved:
                        memorySubjectGuid = resolvedGuid;
                        memorySubjectName = resolvedName;
                        break;
                    case MemorySubjectResolution::Ambiguous:
                        skipMemoryBlock = true;
                        break;
                    case MemorySubjectResolution::Default:
                        break;
                    }
                }

                // Ticket 3a: coarse, deterministic retrieval gate -- neither
                // the fact-block nor the "no stored facts" branch below may
                // run at all unless the message itself looks like SOME kind
                // of information request (see ChatHelper::needsPersistentMemoryContext()
                // and GetMemoryGatePhraseProfiles() in ChatHelper.cpp). This
                // stops casual/status/progress chat ("ik heb nu 15 [Small
                // Egg]") from spontaneously surfacing unrelated stored facts.
                // Deliberately coarse -- not a personal-info classifier and
                // not per-fact relevance/ranking; see Ticket 3b for that.
                if (IsRealPlayer(player) && !skipMemoryBlock &&
                    ChatHelper::needsPersistentMemoryContext(msg))
                {
                    // Ticket 3b1 (isolation pass): SayAction.cpp asks
                    // ChatHelper exactly ONE generic question -- "which of
                    // this subject's stored facts are relevant to this
                    // message?" -- via FilterRelevantMemories(). It never
                    // touches concept/topic-specific logic itself; that
                    // stays entirely inside ChatHelper.cpp (see
                    // ChatHelper::FilterRelevantMemories() and its
                    // declaration in ChatHelper.h for what the three
                    // possible results mean, why this boundary exists, and
                    // where a future, more general relevance mechanism would
                    // plug in later without this call site changing).
                    auto allMemories = sPlayerbotMemoryStore.GetActiveForSubject(
                        bot->GetObjectGuid().GetRawValue(), memorySubjectGuid.GetRawValue());

                    std::vector<PlayerbotMemoryEntry> memories;
                    ChatHelper::MemoryRelevanceResult relevance =
                        ChatHelper::FilterRelevantMemories(msg, allMemories, memories);

                    if (relevance == ChatHelper::MemoryRelevanceResult::FACTS_FOUND)
                    {
                        // Ronde 11: this intro sentence is now the ONLY place
                        // the certainty/provenance rule for persistent memory
                        // lives (moved out of the unconditional block above,
                        // see the comment there) -- authoritative, must-use,
                        // explicitly forbids "I don't know"/guessing when a
                        // fact below already answers the question, and keeps
                        // the existing warning that other speakers' facts and
                        // the bot's own earlier replies are still unreliable.
                        // fact_original itself is never rewritten, only
                        // quoted verbatim in the loop below.
                        std::string memoryBlock = "<persistent memory>\nAuthoritative known facts about " + memorySubjectName +
                            " (not about anyone else, not about you). \"I\"/\"my\"/\"ik\"/\"mijn\" in these facts means " + memorySubjectName +
                            ". If a fact below answers the current question, use it directly -- do not say you don't know and do not guess. "
                            "Facts about a different person, and your own earlier replies, are still not reliable for " + memorySubjectName + ":\n";

                        const size_t maxRows = 12; // keep the block small -- Phase 1 uses a simple bounded selection, not a full dump (see design doc section I). Applied AFTER Ticket 3b1's relevance filter (point 7).
                        size_t rows = 0;
                        for (auto const& mem : memories)
                        {
                            if (rows >= maxRows)
                                break;
                            memoryBlock += "- " + mem.factOriginal + "\n";
                            ++rows;
                        }
                        memoryBlock += "</persistent memory>";

                        jsonFill["<pre prompt>"] += " " + memoryBlock;
                    }
                    else if (relevance == ChatHelper::MemoryRelevanceResult::KNOWN_CONCEPT_NO_FACTS)
                    {
                        // Ticket 3b1: the message DID look on-topic to
                        // ChatHelper, but no active fact for this subject
                        // was judged relevant -- deliberately NOT the old
                        // generic "You have no stored facts about X" text,
                        // which would be misleading here (the subject may
                        // well have OTHER, unrelated facts). Topic-specific
                        // framing instead, same authoritative/no-guessing
                        // intent as the branch above.
                        jsonFill["<pre prompt>"] += " You have no stored fact relevant to this question about " + memorySubjectName + ". "
                            "Do not infer or invent the answer from unrelated memories.";
                    }
                    // else: relevance == NO_KNOWN_CONCEPT -- the message did
                    // not look on-topic to ChatHelper at all, so inject
                    // nothing (neither a fact block nor any "no facts"
                    // text). See ChatHelper::FilterRelevantMemories()'s
                    // conservative-by-default design note.
                }

                // Phase-1 persistent memory RESPONSE flow: reuse THIS single
                // LLM call to phrase a natural confirmation -- no second LLM
                // call. HandleMemoryCommand() already performed the DB
                // write/delete (or deliberate no-op) synchronously before
                // ChatReplyDo() ever reached this branch, so `memoryResult`
                // only carries an already-decided outcome for the LLM to
                // phrase; the LLM cannot see or influence whether the DB
                // operation itself succeeded. Appended to "<pre prompt>"
                // only (never "<prompt>"), same reasoning as the
                // <persistent memory> block just above: only "<prompt>" is
                // folded back into llmContext further down (see
                // `llmContext += " " + jsonFill["<prompt>"];`), so this
                // ephemeral, single-turn directive never leaks into
                // llmContext, Option-A context, or the ai_playerbot_memory
                // table -- it exists for this one reply only.
                if (memoryResult.handled)
                {
                    std::string memoryDirective = "[Internal note -- do not quote this verbatim, just acknowledge it briefly and naturally in your own character and in the same language the player just used: ";
                    switch (memoryResult.outcome)
                    {
                    case MemoryCommandOutcome::REMEMBER_SUCCESS:
                        memoryDirective += "you just permanently committed this fact to memory: \"" + memoryResult.factText + "\".]";
                        break;
                    case MemoryCommandOutcome::ALREADY_KNOWN:
                        memoryDirective += "the player asked you to remember something you already had stored: \"" + memoryResult.factText + "\". Let them know you already knew this.]";
                        break;
                    case MemoryCommandOutcome::FORGET_SUCCESS:
                        memoryDirective += "you just permanently removed this from memory: \"" + memoryResult.factText + "\".]";
                        break;
                    case MemoryCommandOutcome::FORGET_NOT_FOUND:
                        memoryDirective += "the player asked you to forget something you did not have stored: \"" + memoryResult.factText + "\". Let them know there was nothing to forget.]";
                        break;
                    case MemoryCommandOutcome::FORGET_AMBIGUOUS:
                        memoryDirective += "the player's forget request was not specific enough for you to safely identify a single memory to remove, so you left your memory unchanged: \"" + memoryResult.factText + "\". Let them know you are not sure exactly what to remove.]";
                        break;
                    }
                    jsonFill["<pre prompt>"] += " " + memoryDirective;
                }

                jsonFill["<prompt>"] = sPlayerbotAIConfig.llmPrompt;
                jsonFill["<post prompt>"] = sPlayerbotAIConfig.llmPostPrompt;

                for (auto& prompt : jsonFill)
                {
                    prompt.second = BOT_TEXT2(prompt.second, placeholders);
                }

                uint32 currentLength = jsonFill["<pre prompt>"].size() + jsonFill["<context>"].size() + jsonFill["<prompt>"].size() + llmContext.size();
                PlayerbotLLMInterface::LimitContext(llmContext, currentLength);

                if (useSubjectContext)
                {
                    uint32 subjectLength = jsonFill["<pre prompt>"].size() + jsonFill["<context>"].size() + jsonFill["<prompt>"].size() + subjectContext.size();
                    PlayerbotLLMInterface::LimitContext(subjectContext, subjectLength);
                    jsonFill["<context>"] = subjectContext;   // real, non-whisper player: only their own subject-context
                }
                else
                {
                    jsonFill["<context>"] = llmContext;       // unchanged behaviour: whisper, or no real player resolved
                }

                llmContext += " " + jsonFill["<prompt>"];         // UNCHANGED: shared bucket still records this line
                if (useSubjectContext)
                    subjectContext += " " + jsonFill["<prompt>"]; // NEW: same human line, subject-only bucket

                for (auto& prompt : jsonFill)
                {
                    prompt.second = PlayerbotLLMInterface::SanitizeForJson(prompt.second);
                }

                for (auto& prompt : placeholders) //Sanitize now instead of earlier to prevent double Sanitation
                {
                    prompt.second = PlayerbotLLMInterface::SanitizeForJson(prompt.second);
                }

                std::string startPattern, endPattern, deletePattern, splitPattern;
                startPattern = PlayerbotTextMgr::GetReplacePlaceholders(sPlayerbotAIConfig.llmResponseStartPattern, placeholders);
                endPattern = PlayerbotTextMgr::GetReplacePlaceholders(sPlayerbotAIConfig.llmResponseEndPattern, placeholders);
                deletePattern = PlayerbotTextMgr::GetReplacePlaceholders(sPlayerbotAIConfig.llmResponseDeletePattern, placeholders);
                splitPattern = PlayerbotTextMgr::GetReplacePlaceholders(sPlayerbotAIConfig.llmResponseSplitPattern, placeholders);

                std::string json = PlayerbotTextMgr::GetReplacePlaceholders(sPlayerbotAIConfig.llmApiJson, jsonFill);

                json = PlayerbotTextMgr::GetReplacePlaceholders(json, placeholders);

                uint32 type = CHAT_MSG_WHISPER;
                std::string channelName;

                switch (chatChannelSource)
                {
                case ChatChannelSource::SRC_WHISPER:
                {
                    type = CHAT_MSG_WHISPER;
                    break;
                }
                case ChatChannelSource::SRC_SAY:
                {
                    type = CHAT_MSG_SAY;
                    break;
                }
                case ChatChannelSource::SRC_YELL:
                {
                    type = CHAT_MSG_YELL;
                    break;
                }
                case ChatChannelSource::SRC_PARTY:
                {
                    type = CHAT_MSG_PARTY;
                    break;
                }
                case ChatChannelSource::SRC_GUILD:
                {
                    type = CHAT_MSG_GUILD;
                    break;
                }
                case ChatChannelSource::SRC_WORLD:
                case ChatChannelSource::SRC_GENERAL:
                case ChatChannelSource::SRC_TRADE:
                case ChatChannelSource::SRC_LOCAL_DEFENSE:
                case ChatChannelSource::SRC_WORLD_DEFENSE:
                case ChatChannelSource::SRC_LOOKING_FOR_GROUP:
                case ChatChannelSource::SRC_GUILD_RECRUITMENT:
                {
                    type = CHAT_MSG_CHANNEL;
                    channelName = chanName;
                }
                }

                bool debug = GetBotAI(bot)->HasStrategy("debug llm", BotState::BOT_STATE_NON_COMBAT);

                WorldSession* session = bot->GetSession();

                WorldPacket chatTemplate = GetPacketTemplate(CMSG_MESSAGECHAT, type, bot, player, channelName);
                WorldPacket emoteTemplate = (type == CHAT_MSG_SAY || type == CHAT_MSG_WHISPER) ? GetPacketTemplate(CMSG_MESSAGECHAT, CHAT_MSG_EMOTE, bot, player) : WorldPacket();
                WorldPacket systemTemplate = GetPacketTemplate(CMSG_MESSAGECHAT, CHAT_MSG_WHISPER, bot, player);

                // If this turn is a handled Phase-1 memory command, precompute
                // the same non-LLM fallback text BuildMemoryFallbackText()
                // would produce (cheap -- one BOT_TEXT2 lookup, no I/O) and
                // hand it to GenerateResponsePackets() so a runtime LLM
                // failure (Ollama unreachable/timeout/HTTP error/empty
                // response) still confirms the already-completed DB
                // write/forget instead of silently dropping it. Computed
                // here on the tick thread and passed by value into the
                // async call below -- still exactly one Generate() call,
                // no retry.
                std::string memoryLlmFallbackText = memoryResult.handled ? BuildMemoryFallbackText(memoryResult) : std::string();

                futurePackets futPackets = std::async(std::launch::async, ChatReplyAction::GenerateResponsePackets, json, chatTemplate, emoteTemplate, systemTemplate, startPattern, endPattern, deletePattern, splitPattern, debug, memoryLlmFallbackText);

                ai->SendDelayedPacket(session, std::move(futPackets));
            }
            else if (player != bot || sPlayerbotAIConfig.llmBotToBotChatChance)
            {
                if (msg.find("d:") != std::string::npos)
                    return;

                llmContext = llmContext + " " + playerName + ":" + msg;   // UNCHANGED
                PlayerbotLLMInterface::LimitContext(llmContext, llmContext.size());

                if (useSubjectContext)   // false for bot-authored senders -- IsRealPlayer already in the gate above
                {
                    subjectContext = subjectContext + " " + playerName + ":" + msg;
                    PlayerbotLLMInterface::LimitContext(subjectContext, subjectContext.size());
                }
            }
            SET_AI_VALUE(std::string, "manual string::llmcontext" + llmChannel, llmContext);   // UNCHANGED
            if (useSubjectContext)
                SET_AI_VALUE2(std::string, "manual string::llmcontext_subject", subjectKey, subjectContext);   // NEW

            return;
        }
    }

    // Phase-1 persistent memory fallback: reached only when a memory command
    // WAS handled (DB write/delete already done) but the LLM-reply branch
    // above was not eligible for this message (LLM chat disabled/blocked for
    // this bot/channel, or the conversation partner could not be resolved)
    // and therefore never phrased a confirmation. Uses a non-LLM,
    // BOT_TEXT2-backed text (see BuildMemoryFallbackText()) instead of
    // GenerateReplyMessage()'s generic "did not understand" reply, so the
    // player still gets an accurate confirmation even when the LLM path is
    // unavailable -- still no hardcoded Dutch/English sentence literals in
    // this file, only symbolic BOT_TEXT2 keys.
    if (memoryResult.handled)
    {
        SendGeneralResponse(bot, chatChannelSource, BuildMemoryFallbackText(memoryResult), name);
        return;
    }

    SendGeneralResponse(bot, chatChannelSource, GenerateReplyMessage(bot, msg, guid1, name), name);
}

// Option-A party-context patch (2026-09-06): records a party message into
// `bot`'s own "manual string::llmcontext<channel>" bucket without ever
// generating a reply -- used for messages PlayerbotAI::HandleBotOutgoingPacket()
// decided `bot` may not answer (addressed to a different party member and no
// won interjection; or sent by another free/random bot), but that `bot`
// should still silently hear/remember. Deliberately a narrow subset of
// ChatReplyDo() above: same channel gate, same llmChannel key formula, same
// PlayerbotLLMInterface::LimitContext() budget accounting as the existing
// context-only branch there -- so a line recorded here looks exactly like one
// recorded through the normal reply path, and the two can never both fire for
// the same message (see PlayerbotAI::UpdateAIInternal(), which calls exactly
// one of ChatReplyDo()/AppendPartyContextOnly() per queued entry).
// Scope: party chat only (SRC_PARTY), enforced here again defensively even
// though today's only two call sites in PlayerbotAI.cpp already guarantee it.
// Only ever called from PlayerbotAI::UpdateAIInternal()'s chatReplies dequeue
// loop (the bot's own tick thread) -- never call this, or touch
// AiObjectContext/AI_VALUE/SET_AI_VALUE in any other new code, from
// PlayerbotAI::HandleBotOutgoingPacket() directly.
void ChatReplyAction::AppendPartyContextOnly(Player* bot, uint32 type, uint32 guid1, std::string msg, std::string chanName, std::string name)
{
    if (!GetBotAI(bot))
        return;

    ChatChannelSource chatChannelSource = GetBotAI(bot)->GetChatChannelSource(bot, type, chanName);

    if (sPlayerbotAIConfig.llmEnabled <= 0
        || !(GetBotAI(bot)->HasStrategy("ai chat", BotState::BOT_STATE_NON_COMBAT) || sPlayerbotAIConfig.llmEnabled == 3)
        || chatChannelSource != ChatChannelSource::SRC_PARTY
        || sPlayerbotAIConfig.llmBlockedReplyChannels.find(chatChannelSource) != sPlayerbotAIConfig.llmBlockedReplyChannels.end())
        return;

    Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid1));

    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!chanName.empty() && !ai->ChannelHasRealPlayer(chanName))
        player = nullptr;

    if (!player || player == bot)
        return;

    if (msg.find("d:") != std::string::npos)
        return;

    std::string playerName = player->GetName();

    std::string llmChannel;
    if (!sPlayerbotAIConfig.llmGlobalContext)
        llmChannel = ((chatChannelSource == ChatChannelSource::SRC_WHISPER) ? name : std::to_string(chatChannelSource));

    std::string llmContext = AI_VALUE(std::string, "manual string::llmcontext" + llmChannel);
    llmContext = llmContext + " " + playerName + ":" + msg;                              // UNCHANGED
    PlayerbotLLMInterface::LimitContext(llmContext, llmContext.size());
    SET_AI_VALUE(std::string, "manual string::llmcontext" + llmChannel, llmContext);      // UNCHANGED

    // Structural subject-context fix -- same gate/key formula as ChatReplyDo()
    // above. IsRealPlayer() excludes bot-authored senders (free/random bots
    // named by another party member never reach a player-subject bucket).
    // The SRC_PARTY-only guard at the top of this function already excludes
    // whisper today; the explicit check here is defensive against that guard
    // ever being loosened later. type == CHAT_MSG_PARTY here, so
    // BuildSubjectKey() always takes its non-channel branch -- no chanName
    // logic needed, matching this function's PARTY-only scope.
    if (IsRealPlayer(player) && chatChannelSource != ChatChannelSource::SRC_WHISPER)
    {
        std::string subjectKey = BuildSubjectKey(type, chatChannelSource, chanName, guid1);
        std::string subjectContext = AI_VALUE2(std::string, "manual string::llmcontext_subject", subjectKey);
        subjectContext = subjectContext + " " + playerName + ":" + msg;
        PlayerbotLLMInterface::LimitContext(subjectContext, subjectContext.size());
        SET_AI_VALUE2(std::string, "manual string::llmcontext_subject", subjectKey, subjectContext);
    }
}

// Phase-1 persistent LLM memory ("<bot>, remember/onthoud that ..." /
// "<bot>, forget/vergeet that ..."). See PlayerbotMemoryStore.h for the full
// design rationale and the thread-safety guarantee this relies on: this
// function -- like every other Handle*Reply helper in this file -- is only
// ever called from ChatReplyDo(), which is only ever called from
// PlayerbotAI::UpdateAIInternal()'s chatReplies queue-drain loop, i.e.
// always on the bot's own tick thread. The write/delete itself
// (PlayerbotMemoryStore::Remember()/Forget()) therefore also always runs on
// that same tick thread -- never from PlayerbotAI::HandleBotOutgoingPacket()
// or any other possibly-async packet-handler path. This function performs
// the write/delete itself and returns the authoritative result; it never
// sends a chat response -- see ChatReplyDo() for how the confirmation gets
// phrased (existing LLM call when eligible, BuildMemoryFallbackText()
// otherwise).
//
// ABSOLUTE GUARD 1 (who): bot-authored chat can never reach
// Remember()/Forget() through this function, enforced here in code, not by
// prompt wording -- IsRealPlayer(sender) is re-checked from guid1
// independently of whatever gauntlet already let this message reach
// ChatReplyDo(), and any failure to resolve a real player leaves
// `result.handled == false` (falls through to ordinary handling) before
// either store method is ever called.
//
// ABSOLUTE GUARD 2 (which bot): a broadcast-channel message (party/guild/
// raid/say/yell) can independently reach every bot's own ChatReplyDo() call
// for the exact same text -- each bot decides for itself whether to reply.
// Without an addressing check, EVERY such bot would treat "Ravanne, onthoud
// dat ..." as addressed to itself (confirmed live: both Ravanne and Malurith
// wrote the same memory from one guild message). So for every channel
// except whisper (already bot-specific by construction -- only the
// addressed bot's client, and therefore only that bot's ChatReplyDo(), ever
// sees a given whisper), this bot's own name must exactly match the
// message's LEADING VOCATIVE as already parsed by
// ChatHelper::detectMemoryTrigger() (trigger.addressedName) -- never a scan
// for the bot's name anywhere in the message, which would also match a bot
// name mentioned mid-sentence ("Ravanne, onthoud dat Malurith mijn beste
// vriend is." must resolve to Ravanne only, never Malurith). A broadcast
// message with no leading vocative at all (addressedName empty) is left
// unhandled for every bot -- conservative by design, never guessed.
MemoryCommandResult ChatReplyAction::HandleMemoryCommand(Player* bot, ChatChannelSource chatChannelSource, std::string msg, uint32 guid1, std::string name)
{
    MemoryCommandResult result;

    ChatHelper::MemoryTriggerResult trigger = ChatHelper::detectMemoryTrigger(msg);
    if (trigger.action == ChatHelper::MemoryTriggerAction::NONE)
        return result; // Not a memory command -- result.handled stays false.

    Player* sender = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid1));
    if (!sender || !IsRealPlayer(sender))
        return result; // Hard guard 1 -- see the function comment above.

    if (chatChannelSource != ChatChannelSource::SRC_WHISPER
        && (trigger.addressedName.empty() || !boost::iequals(trigger.addressedName, bot->GetName())))
        return result; // Hard guard 2 -- see the function comment above.

    if (trigger.factText.empty())
        return result; // e.g. "Ravanne, onthoud" with nothing after it -- nothing explicit to store, treat as an ordinary message rather than guessing.

    result.factText = trigger.factText;
    result.language = trigger.language;

    uint64 botGuid = bot->GetObjectGuid().GetRawValue();
    uint64 subjectGuid = sender->GetObjectGuid().GetRawValue();

    if (trigger.action == ChatHelper::MemoryTriggerAction::REMEMBER)
    {
        bool wasNew = false;
        sPlayerbotMemoryStore.Remember(botGuid, subjectGuid, subjectGuid, trigger.factText, &wasNew);

        result.handled = true;
        result.outcome = wasNew ? MemoryCommandOutcome::REMEMBER_SUCCESS : MemoryCommandOutcome::ALREADY_KNOWN;
        return result;
    }

    // FORGET
    PlayerbotMemoryForgetResult forgetResult = sPlayerbotMemoryStore.Forget(botGuid, subjectGuid, trigger.factText);

    result.handled = true;
    switch (forgetResult)
    {
        case PlayerbotMemoryForgetResult::Forgotten:
            result.outcome = MemoryCommandOutcome::FORGET_SUCCESS;
            break;
        case PlayerbotMemoryForgetResult::NotFound:
            result.outcome = MemoryCommandOutcome::FORGET_NOT_FOUND;
            break;
        case PlayerbotMemoryForgetResult::Ambiguous:
            result.outcome = MemoryCommandOutcome::FORGET_AMBIGUOUS;
            break;
    }
    return result;
}

// Non-LLM fallback confirmation text, used both when HandleMemoryCommand()
// handled a memory command but ChatReplyDo()'s ordinary LLM-reply branch was
// not eligible for this message, and when that branch WAS eligible but the
// runtime Generate() call itself failed (see the fallbackText plumbing into
// GenerateResponsePackets() below). Deliberately contains no hardcoded
// Dutch/English sentence literals -- only symbolic BOT_TEXT2 keys.
//
// Language selection: PlayerbotTextMgr's text/text_loc1..8 + GetLocalePriority()
// mechanism (used by every other BOT_TEXT/BOT_TEXT2 call in this file,
// e.g. "thunderfury_spam") cannot be used here -- text_loc1..8 are indexed by
// WorldSession's LocaleConstant, i.e. real WoW client locales the game
// client itself negotiates at login (confirmed: LOCALE_enUS/LOCALE_zhCN
// exist, PlayerbotTextMgr loops 8 slots via MAX_LOCALE). Dutch/nlNL has
// never been a WoW client locale, so no text_locN slot for it can ever
// exist or be selected -- GetLocalePriority() has structurally no way to
// pick "Dutch". This function therefore selects between two keys per
// outcome (5 outcomes x 2 languages = 10 keys total) using result.language
// ("nl"/"en", set by ChatHelper::detectMemoryTrigger() from which trigger
// phrase matched the player's own message) directly, instead. Still no
// PlayerbotTextMgr change and no hardcoded reply text in this file -- only
// the symbolic key name differs per language; the actual Dutch/English
// sentences live in ai_playerbot_texts.
// If a key has no row yet in ai_playerbot_texts, BOT_TEXT2 falls back to
// showing the key text itself (PlayerbotTextMgr::GetBotText's documented
// behaviour) -- a visible but harmless degraded state, never a hardcoded
// sentence added here to paper over it.
std::string ChatReplyAction::BuildMemoryFallbackText(const MemoryCommandResult& result)
{
    bool dutch = (result.language == "nl");

    std::string key;
    switch (result.outcome)
    {
        case MemoryCommandOutcome::REMEMBER_SUCCESS:
            key = dutch ? "memory_remember_success_nl" : "memory_remember_success_en";
            break;
        case MemoryCommandOutcome::ALREADY_KNOWN:
            key = dutch ? "memory_already_known_nl" : "memory_already_known_en";
            break;
        case MemoryCommandOutcome::FORGET_SUCCESS:
            key = dutch ? "memory_forget_success_nl" : "memory_forget_success_en";
            break;
        case MemoryCommandOutcome::FORGET_NOT_FOUND:
            key = dutch ? "memory_forget_not_found_nl" : "memory_forget_not_found_en";
            break;
        case MemoryCommandOutcome::FORGET_AMBIGUOUS:
            key = dutch ? "memory_forget_ambiguous_nl" : "memory_forget_ambiguous_en";
            break;
    }

    std::map<std::string, std::string> placeholders;
    placeholders["%fact%"] = result.factText;

    return BOT_TEXT2(key, placeholders);
}

bool ChatReplyAction::HandleThunderfuryReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name)
{
    std::map<std::string, std::string> placeholders;
    ItemPrototype const* thunderfuryProto = sObjectMgr.GetItemPrototype(19019);
    placeholders["%thunderfury_link"] = GetBotAI(bot)->GetChatHelper()->formatItem(thunderfuryProto);

    std::string responseMessage = BOT_TEXT2("thunderfury_spam", placeholders);

    switch (chatChannelSource)
    {
        case ChatChannelSource::SRC_WORLD:
        {
            GetBotAI(bot)->SayToWorld(responseMessage);
            break;
        }
        case ChatChannelSource::SRC_GENERAL:
        {
            GetBotAI(bot)->SayToGeneral(responseMessage);
            break;
        }
        case ChatChannelSource::SRC_YELL:
        {
            GetBotAI(bot)->Yell(responseMessage);
            break;
        }
    }

    GetBotAI(bot)->GetAiObjectContext()->GetValue<time_t>("last said", "chat")->Set(time(0) + urand(5, 25));

    return true;
}

bool ChatReplyAction::HandleToxicLinksReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name)
{
    //quests
    std::vector<uint32> incompleteQuests;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_NONE)
            incompleteQuests.push_back(questId);
    }

    //items
    std::vector<Item*> botItems = GetBotAI(bot)->GetInventoryAndEquippedItems();

    //spells
    //?

    std::map<std::string, std::string> placeholders;

    placeholders["%random_inventory_item_link"] = botItems.size() > 0 ? GetBotAI(bot)->GetChatHelper()->formatItem(botItems[rand() % botItems.size()]) : BOT_TEXT("string_empty_link");
    placeholders["%prefix"] = sPlayerbotAIConfig.toxicLinksPrefix;

    if (incompleteQuests.size() > 0)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(incompleteQuests[rand() % incompleteQuests.size()]);
        placeholders["%random_taken_quest_or_item_link"] = GetBotAI(bot)->GetChatHelper()->formatQuest(quest);
    }
    else
    {
        placeholders["%random_taken_quest_or_item_link"] = placeholders["%random_inventory_item_link"];
    }

    placeholders["%my_role"] = GetBotAI(bot)->GetChatHelper()->formatClass(bot, AiFactory::GetPlayerSpecTab(bot));
    AreaTableEntry const* current_area = GetBotAI(bot)->GetCurrentArea();
    AreaTableEntry const* current_zone = GetBotAI(bot)->GetCurrentZone();
    placeholders["%area_name"] = current_area ? GetBotAI(bot)->GetLocalizedAreaName(current_area) : BOT_TEXT("string_unknown_area");
    placeholders["%zone_name"] = current_zone ? GetBotAI(bot)->GetLocalizedAreaName(current_zone) : BOT_TEXT("string_unknown_area");
    placeholders["%my_class"] = GetBotAI(bot)->GetChatHelper()->formatClass(bot->getClass());
    placeholders["%my_race"] = GetBotAI(bot)->GetChatHelper()->formatRace(bot->getRace());
    placeholders["%my_level"] = std::to_string(bot->GetLevel());

    switch (chatChannelSource)
    {
        case ChatChannelSource::SRC_WORLD:
        {
            GetBotAI(bot)->SayToWorld(BOT_TEXT2("suggest_toxic_links", placeholders));
            break;
        }
        case ChatChannelSource::SRC_GENERAL:
        {
            GetBotAI(bot)->SayToGeneral(BOT_TEXT2("suggest_toxic_links", placeholders));
            break;
        }
        case ChatChannelSource::SRC_GUILD:
        {
            GetBotAI(bot)->SayToGuild(BOT_TEXT2("suggest_toxic_links", placeholders));
            break;
        }
        case ChatChannelSource::SRC_SAY:
        {
            GetBotAI(bot)->Say(BOT_TEXT2("suggest_toxic_links", placeholders));
            break;
        }
        case ChatChannelSource::SRC_YELL:
        {
            GetBotAI(bot)->Yell(BOT_TEXT2("suggest_toxic_links", placeholders));
            break;
        }
        case ChatChannelSource::SRC_PARTY:
        {
            GetBotAI(bot)->SayToParty(BOT_TEXT2("suggest_toxic_links", placeholders));
            break;
        }
    }

    GetBotAI(bot)->GetAiObjectContext()->GetValue<time_t>("last said", "chat")->Set(time(0) + urand(5, 60));

    return true;
}

/*
* @return true if message contained item ids
*/
bool ChatReplyAction::HandleWTBItemsReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name)
{
    auto messageItemIds = GetBotAI(bot)->GetChatHelper()->ExtractAllItemIds(msg);

    if (messageItemIds.empty())
    {
        return false;
    }

    std::set<uint32> matchingItemIds;

    for (auto messageItemId : messageItemIds)
    {
        if (GetBotAI(bot)->HasItemInInventory(messageItemId))
        {
            matchingItemIds.insert(messageItemId);
        }
    }

    if (!matchingItemIds.empty())
    {
        std::map<std::string, std::string> placeholders;
        placeholders["%other_name"] = name;
        AreaTableEntry const* current_area = GetBotAI(bot)->GetCurrentArea();
        AreaTableEntry const* current_zone = GetBotAI(bot)->GetCurrentZone();
        placeholders["%area_name"] = current_area ? GetBotAI(bot)->GetLocalizedAreaName(current_area) : BOT_TEXT("string_unknown_area");
        placeholders["%zone_name"] = current_zone ? GetBotAI(bot)->GetLocalizedAreaName(current_zone) : BOT_TEXT("string_unknown_area");
        placeholders["%my_class"] = GetBotAI(bot)->GetChatHelper()->formatClass(bot->getClass());
        placeholders["%my_race"] = GetBotAI(bot)->GetChatHelper()->formatRace(bot->getRace());
        placeholders["%my_level"] = std::to_string(bot->GetLevel());
        placeholders["%my_role"] = GetBotAI(bot)->GetChatHelper()->formatClass(bot, AiFactory::GetPlayerSpecTab(bot));
        placeholders["%formatted_item_links"] = "";

        for (auto matchingItemId : matchingItemIds)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(matchingItemId);
            placeholders["%formatted_item_links"] += GetBotAI(bot)->GetChatHelper()->formatItem(proto, GetBotAI(bot)->GetInventoryItemsCountWithId(matchingItemId));
            placeholders["%formatted_item_links"] += " ";
        }

        switch (chatChannelSource)
        {
            case ChatChannelSource::SRC_WORLD:
            {
                //may reply to the same channel or whisper
                if (urand(0, 1))
                {
                    std::string responseMessage = BOT_TEXT2("response_wtb_items_channel", placeholders);
                    GetBotAI(bot)->SayToWorld(responseMessage);
                }
                else
                {
                    std::string responseMessage = BOT_TEXT2("response_wtb_items_whisper", placeholders);
                    GetBotAI(bot)->Whisper(responseMessage, name);
                }
                break;
            }
            case ChatChannelSource::SRC_GENERAL:
            {
                //may reply to the same channel or whisper
                if (urand(0, 1))
                {
                    std::string responseMessage = BOT_TEXT2("response_wtb_items_channel", placeholders);
                    GetBotAI(bot)->SayToGeneral(responseMessage);
                }
                else
                {
                    std::string responseMessage = BOT_TEXT2("response_wtb_items_whisper", placeholders);
                    GetBotAI(bot)->Whisper(responseMessage, name);
                }
                break;
            }
            case ChatChannelSource::SRC_TRADE:
            {
                //may reply to the same channel or whisper
                if (urand(0, 1))
                {
                    std::string responseMessage = BOT_TEXT2("response_wtb_items_channel", placeholders);
                    GetBotAI(bot)->SayToTrade(responseMessage);
                }
                else
                {
                    std::string responseMessage = BOT_TEXT2("response_wtb_items_whisper", placeholders);
                    GetBotAI(bot)->Whisper(responseMessage, name);
                }
                break;
            }
        }
        GetBotAI(bot)->GetAiObjectContext()->GetValue<time_t>("last said", "chat")->Set(time(0) + urand(5, 60));
    }

    return true;
}

/*
* @return true if message contained quest ids
*/
bool ChatReplyAction::HandleLFGQuestsReply(Player* bot, ChatChannelSource chatChannelSource, std::string msg, std::string name)
{
    auto messageQuestIds = GetBotAI(bot)->GetChatHelper()->ExtractAllQuestIds(msg);

    if (messageQuestIds.empty())
    {
        return false;
    }

    auto botQuestIds = GetBotAI(bot)->GetAllCurrentQuestIds();

    std::set<uint32> matchingQuestIds;
    for (auto botQuestId : botQuestIds)
    {
        if (messageQuestIds.count(botQuestId) != 0)
        {
            matchingQuestIds.insert(botQuestId);
        }
    }

    if (!matchingQuestIds.empty())
    {
        std::map<std::string, std::string> placeholders;
        placeholders["%other_name"] = name;
        AreaTableEntry const* current_area = GetBotAI(bot)->GetCurrentArea();
        AreaTableEntry const* current_zone = GetBotAI(bot)->GetCurrentZone();
        placeholders["%area_name"] = current_area ? GetBotAI(bot)->GetLocalizedAreaName(current_area) : BOT_TEXT("string_unknown_area");
        placeholders["%zone_name"] = current_zone ? GetBotAI(bot)->GetLocalizedAreaName(current_zone) : BOT_TEXT("string_unknown_area");
        placeholders["%my_class"] = GetBotAI(bot)->GetChatHelper()->formatClass(bot->getClass());
        placeholders["%my_race"] = GetBotAI(bot)->GetChatHelper()->formatRace(bot->getRace());
        placeholders["%my_level"] = std::to_string(bot->GetLevel());
        placeholders["%my_role"] = GetBotAI(bot)->GetChatHelper()->formatClass(bot, AiFactory::GetPlayerSpecTab(bot));
        placeholders["%quest_links"] = "";
        for (auto matchingQuestId : matchingQuestIds)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(matchingQuestId);
            placeholders["%quest_links"] += GetBotAI(bot)->GetChatHelper()->formatQuest(quest);
        }

        switch (chatChannelSource)
        {
            case ChatChannelSource::SRC_WORLD:
            {
                //may reply to the same channel or whisper
                if (urand(0, 1))
                {
                    std::string responseMessage = BOT_TEXT2(bot->GetGroup() ? "response_lfg_quests_channel_in_group" : "response_lfg_quests_channel", placeholders);
                    GetBotAI(bot)->SayToWorld(responseMessage);
                }
                else
                {
                    std::string responseMessage = BOT_TEXT2(bot->GetGroup() ? "response_lfg_quests_whisper_in_group" : "response_lfg_quests_whisper", placeholders);
                    GetBotAI(bot)->Whisper(responseMessage, name);
                }
                break;
            }
            case ChatChannelSource::SRC_GENERAL:
            {
                //may reply to the same channel or whisper
                if (urand(0, 1))
                {
                    std::string responseMessage = BOT_TEXT2(bot->GetGroup() ? "response_lfg_quests_channel_in_group" : "response_lfg_quests_channel", placeholders);
                    GetBotAI(bot)->SayToGeneral(responseMessage);
                }
                else
                {
                    std::string responseMessage = BOT_TEXT2(bot->GetGroup() ? "response_lfg_quests_whisper_in_group" : "response_lfg_quests_whisper", placeholders);
                    GetBotAI(bot)->Whisper(responseMessage, name);
                }
                break;
            }
            case ChatChannelSource::SRC_LOOKING_FOR_GROUP:
            {
                //do not reply to the chat
                //may whisper
                std::string responseMessage = BOT_TEXT2(bot->GetGroup() ? "response_lfg_quests_whisper_in_group" : "response_lfg_quests_whisper", placeholders);
                GetBotAI(bot)->Whisper(responseMessage, name);
                break;
            }
        }
        GetBotAI(bot)->GetAiObjectContext()->GetValue<time_t>("last said", "chat")->Set(time(0) + urand(5, 25));
    }

    return true;
}

bool ChatReplyAction::SendGeneralResponse(Player* bot, ChatChannelSource chatChannelSource, std::string responseMessage, std::string name)
{
    // send responds
    switch (chatChannelSource)
    {
    case ChatChannelSource::SRC_WORLD:
    {
        //may reply to the same channel or whisper
        GetBotAI(bot)->SayToWorld(responseMessage);
        break;
    }
    case ChatChannelSource::SRC_GENERAL:
    {
        //may reply to the same channel or whisper
        //GetBotAI(bot)->SayToGeneral(responseMessage);
        GetBotAI(bot)->Whisper(responseMessage, name);
        break;
    }
    case ChatChannelSource::SRC_TRADE:
    {
        //do not reply to the chat
        //may whisper
        break;
    }
    case ChatChannelSource::SRC_LOCAL_DEFENSE:
    {
        //may reply to the same channel or whisper
        GetBotAI(bot)->SayToLocalDefense(responseMessage);
        break;
    }
    case ChatChannelSource::SRC_WORLD_DEFENSE:
    {
        //may reply only if rank 11+ for MANGOSBOT_ZERO, may always reply for others
        //may whisper
        break;
    }
    case ChatChannelSource::SRC_LOOKING_FOR_GROUP:
    {
        //do not reply to the chat
        //may whisper
        break;
    }
    case ChatChannelSource::SRC_GUILD_RECRUITMENT:
    {
        //do not reply to the chat
        //may whisper
        break;
    }
    case ChatChannelSource::SRC_WHISPER:
    {
        GetBotAI(bot)->Whisper(responseMessage, name);
        break;
    }
    case ChatChannelSource::SRC_SAY:
    {
        GetBotAI(bot)->Say(responseMessage);
        break;
    }
    case ChatChannelSource::SRC_YELL:
    {
        GetBotAI(bot)->Yell(responseMessage);
        break;
    }
    case ChatChannelSource::SRC_GUILD:
    {
        GetBotAI(bot)->SayToGuild(responseMessage);
        break;
    }
    case ChatChannelSource::SRC_PARTY:
    {
        GetBotAI(bot)->SayToParty(responseMessage);
        break;
    }
    case ChatChannelSource::SRC_RAID:
    {
        GetBotAI(bot)->SayToRaid(responseMessage);
        break;
    }
    default:
        break;
    }
    GetBotAI(bot)->GetAiObjectContext()->GetValue<time_t>("last said", "chat")->Set(time(0) + urand(5, 25));

    return true;
}

std::string ChatReplyAction::GenerateReplyMessage(Player* bot, std::string incomingMessage, uint32 guid1, std::string name)
{
    ChatReplyType replyType = REPLY_NOT_UNDERSTAND; // default not understand

    std::string respondsText = "";

    // Chat Logic
    int32 verb_pos = -1;
    int32 verb_type = -1;
    int32 is_quest = 0;
    bool found = false;
    std::stringstream text(incomingMessage);
    std::string segment;
    std::vector<std::string> word;
    while (std::getline(text, segment, ' '))
    {
        word.push_back(segment);
    }

    for (uint32 i = 0; i < 15; i++)
    {
        if (word.size() < i)
            word.push_back("");
    }

    if (incomingMessage.find("?") != std::string::npos)
        is_quest = 1;
    if (word[0].find("what") != std::string::npos)
        is_quest = 2;
    else if (word[0].find("who") != std::string::npos)
        is_quest = 3;
    else if (word[0] == "when")
        is_quest = 4;
    else if (word[0] == "where")
        is_quest = 5;
    else if (word[0] == "why")
        is_quest = 6;

    // Responds
    for (uint32 i = 0; i < 8; i++)
    {
        // blame gm with chat tag
        if (Player* plr = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guid1)))
        {
            if (plr->isGMChat())
            {
                replyType = REPLY_ADMIN_ABUSE;
                found = true;
                break;
            }
        }

        if (word[i] == "hi" || word[i] == "hey" || word[i] == "hello" || word[i] == "wazzup")
        {
            replyType = REPLY_HELLO;
            found = true;
            break;
        }

        if (verb_type < 4)
        {
            if (word[i] == "am" || word[i] == "are" || word[i] == "is")
            {
                verb_pos = i;
                verb_type = 2; // present
                if (verb_pos == 0)
                    is_quest = 1;
            }
            else if (word[i] == "will")
            {
                verb_pos = i;
                verb_type = 3; // future
            }
            else if (word[i] == "was" || word[i] == "were")
            {
                verb_pos = i;
                verb_type = 1; // past
            }
            else if (word[i] == "shut" || word[i] == "noob")
            {
                if (incomingMessage.find(bot->GetName()) == std::string::npos)
                {
                    continue; // not react
                    uint32 rnd = urand(0, 2);
                    std::string msg = "";
                    if (rnd == 0)
                        msg = "sorry %s, ill shut up now";
                    if (rnd == 1)
                        msg = "ok ok %s";
                    if (rnd == 2)
                        msg = "fine, i wont talk to you anymore %s";

                    msg = std::regex_replace(msg, std::regex("%s"), name);
                    respondsText = msg;
                    found = true;
                    break;
                }
                else
                {
                    replyType = REPLY_GRUDGE;
                    found = true;
                    break;
                }
            }
        }
    }
    if (verb_type < 4 && is_quest && !found)
    {
        switch (is_quest)
        {
        case 2:
        {
            uint32 rnd = urand(0, 3);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "i dont know what";
                break;
            case 1:
                msg = "i dont know %s";
                break;
            case 2:
                msg = "who cares";
                break;
            case 3:
                msg = "afraid that was before i was around or paying attention";
                break;
            }

            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        case 3:
        {
            uint32 rnd = urand(0, 4);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "nobody";
                break;
            case 1:
                msg = "we all do";
                break;
            case 2:
                msg = "perhaps its you, %s";
                break;
            case 3:
                msg = "dunno %s";
                break;
            case 4:
                msg = "is it me?";
                break;
            }

            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        case 4:
        {
            uint32 rnd = urand(0, 6);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "soon perhaps %s";
                break;
            case 1:
                msg = "probably later";
                break;
            case 2:
                msg = "never";
                break;
            case 3:
                msg = "what do i look like, a psychic?";
                break;
            case 4:
                msg = "a few minutes, maybe an hour ... years?";
                break;
            case 5:
                msg = "when? good question %s";
                break;
            case 6:
                msg = "dunno %s";
                break;
            }

            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        case 5:
        {
            uint32 rnd = urand(0, 6);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "really want me to answer that?";
                break;
            case 1:
                msg = "on the map?";
                break;
            case 2:
                msg = "who cares";
                break;
            case 3:
                msg = "afk?";
                break;
            case 4:
                msg = "none of your buisiness where";
                break;
            case 5:
                msg = "yeah, where?";
                break;
            case 6:
                msg = "dunno %s";
                break;
            }

            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        case 6:
        {
            uint32 rnd = urand(0, 6);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "dunno %s";
                break;
            case 1:
                msg = "why? just because %s";
                break;
            case 2:
                msg = "why is the sky blue?";
                break;
            case 3:
                msg = "dont ask me %s, im just a bot";
                break;
            case 4:
                msg = "your asking the wrong person";
                break;
            case 5:
                msg = "who knows?";
                break;
            case 6:
                msg = "dunno %s";
                break;
            }
            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        default:
        {
            switch (verb_type)
            {
            case 1:
            {
                uint32 rnd = urand(0, 3);
                std::string msg = "";

                switch (rnd)
                {
                case 0:
                    msg = "its true, " + word[verb_pos + 1] + " " + word[verb_pos] + " " + word[verb_pos + 2] + " " + word[verb_pos + 3] + " " + word[verb_pos + 4] + " " + word[verb_pos + 4];
                    break;
                case 1:
                    msg = "ya %s but thats in the past";
                    break;
                case 2:
                    msg = "nah, but " + word[verb_pos + 1] + " will " + word[verb_pos + 3] + " again though %s";
                    break;
                case 3:
                    msg = "afraid that was before i was around or paying attention";
                    break;
                }
                msg = std::regex_replace(msg, std::regex("%s"), name);
                respondsText = msg;
                found = true;
                break;
            }
            case 2:
            {
                uint32 rnd = urand(0, 6);
                std::string msg = "";

                switch (rnd)
                {
                case 0:
                    msg = "its true, " + word[verb_pos + 1] + " " + word[verb_pos] + " " + word[verb_pos + 2] + " " + word[verb_pos + 3] + " " + word[verb_pos + 4] + " " + word[verb_pos + 5];
                    break;
                case 1:
                    msg = "ya %s thats true";
                    break;
                case 2:
                    msg = "maybe " + word[verb_pos + 1] + " " + word[verb_pos] + " " + word[verb_pos + 2] + " " + word[verb_pos + 3] + " " + word[verb_pos + 4] + " " + word[verb_pos + 5];
                    break;
                case 3:
                    msg = "dunno %s";
                    break;
                case 4:
                    msg = "i dont think so %s";
                    break;
                case 5:
                    msg = "yes";
                    break;
                case 6:
                    msg = "no";
                    break;
                }
                msg = std::regex_replace(msg, std::regex("%s"), name);
                respondsText = msg;
                found = true;
                break;
            }
            case 3:
            {
                uint32 rnd = urand(0, 8);
                std::string msg = "";

                switch (rnd)
                {
                case 0:
                    msg = "dunno %s";
                    break;
                case 1:
                    msg = "beats me %s";
                    break;
                case 2:
                    msg = "how should i know %s";
                    break;
                case 3:
                    msg = "dont ask me %s, im just a bot";
                    break;
                case 4:
                    msg = "your asking the wrong person";
                    break;
                case 5:
                    msg = "what do i look like, a psychic?";
                    break;
                case 6:
                    msg = "sure %s";
                    break;
                case 7:
                    msg = "i dont think so %s";
                    break;
                case 8:
                    msg = "maybe";
                    break;
                }
                msg = std::regex_replace(msg, std::regex("%s"), name);
                respondsText = msg;
                found = true;
                break;
            }
            }
        }
        }
    }
    else if (!found)
    {
        switch (verb_type)
        {
        case 1:
        {
            uint32 rnd = urand(0, 2);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "yeah %s, the key word being " + word[verb_pos] + " " + word[verb_pos + 1];
                break;
            case 1:
                msg = "ya %s but thats in the past";
                break;
            case 2:
                msg = word[verb_pos ? verb_pos - 1 : verb_pos + 1] + " will " + word[verb_pos + 1] + " again though %s";
                break;
            }
            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        case 2:
        {
            uint32 rnd = urand(0, 2);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "%s, what do you mean " + word[verb_pos + 1] + "?";
                break;
            case 1:
                msg = "%s, what is a " + word[verb_pos + 1] + "?";
                break;
            case 2:
                msg = "yeah i know " + word[verb_pos ? verb_pos - 1 : verb_pos + 1] + " is a " + word[verb_pos + 1];
                break;
            }
            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        case 3:
        {
            uint32 rnd = urand(0, 1);
            std::string msg = "";

            switch (rnd)
            {
            case 0:
                msg = "are you sure thats going to happen %s?";
                break;
            case 1:
                msg = "%s, what will happen %s?";
                break;
            case 2:
                msg = "are you saying " + word[verb_pos ? verb_pos - 1 : verb_pos + 1] + " will " + word[verb_pos + 1] + " " + word[verb_pos + 2] + " %s?";
                break;
            }
            msg = std::regex_replace(msg, std::regex("%s"), name);
            respondsText = msg;
            found = true;
            break;
        }
        }
    }

    if (!found || urand(0, 4))
    {
        // Name Responds
        if (incomingMessage.find(bot->GetName()) != std::string::npos)
        {
            replyType = REPLY_NAME;
            found = true;
        }
        else if (!found) // Does not understand
        {
            replyType = REPLY_NOT_UNDERSTAND;
            found = true;
        }
    }

    // load text if needed
    if (respondsText.empty())
    {
        respondsText = BOT_TEXT2(replyType, name);
    }

    if (respondsText.size() > 255)
    {
        respondsText.resize(255);
    }

    return respondsText;

}

bool ChatReplyAction::isUseful()
{
    return !ai->HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT);
}

bool SpeakAction::Execute(Event& event)
{
    bool botsTalkLikePlayers = true;

    std::string text = event.getParam();
    if (text.find("/y ") == 0)
        ai->Yell(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/p ") == 0)
        ai->SayToParty(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/r ") == 0)
        ai->SayToRaid(text.substr(3));
    else if (text.find("/g ") == 0)
        ai->SayToGuild(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/s ") == 0)
        ai->Say(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/1 ") == 0)
        ai->SayToGeneral(text.substr(3));
    else if (text.find("/2 ") == 0)
        ai->SayToTrade(text.substr(3));
    else if (text.find("/3 ") == 0)
        ai->SayToLocalDefense(text.substr(3));
    else if (text.find("/4 ") == 0)
        ai->SayToLFG(text.substr(3));
    else
        ai->Say(text, botsTalkLikePlayers);

    return true;
}