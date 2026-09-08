#!/usr/bin/env python3
"""
Patch: Phase-1 social participation (party OTHER_MEMBER_ADDRESSED interjection)
       + alive/dead/ghost life-state placeholders.

Run this on the Linux box, from anywhere (paths below are absolute).

WHAT THIS TOUCHES
------------------
Source (under /home/hans/tortoise-wow):
    modules/mod-playerbots/src/playerbot/PlayerbotAI.cpp
    modules/mod-playerbots/src/playerbot/PlayerbotAIConfig.h
    modules/mod-playerbots/src/playerbot/PlayerbotAIConfig.cpp
    modules/mod-playerbots/src/playerbot/strategy/actions/SayAction.cpp
    modules/mod-playerbots/src/playerbot/ChatHelper.h
    modules/mod-playerbots/src/playerbot/ChatHelper.cpp

Live runtime config:
    /home/hans/tortoise-server-asan/etc/aiplayerbot.conf
    /home/hans/tortoise-server-asan/etc/aiplayerbot.conf.dist

Read-only cross-check (never written):
    /home/hans/tortoise-wow/runtime-configs/aiplayerbot.conf

WHAT THIS DOES NOT TOUCH (verified unchanged before/after)
------------------------------------------------------------
    WorldSession.cpp packet routing
    Ollama HTTP/native API transport (LLMApiEndpoint, LLMModel, LLMApiJson)
    Response parser / start / end / delete / split patterns
    The 50ms typing delay in SayAction.cpp's LinesToPackets(...) call
    SendDelayedPacket lifetime/UAF code
    llm_character_card.txt / character-card contents

SAFETY MODEL -- TRUE TWO-PHASE PATCHING
----------------------------------------
PHASE 1 (preflight): every file is read and every edit is computed AND
validated ENTIRELY IN MEMORY. This includes all exact-match anchor checks,
the mirror/live LLMPrePrompt equality check, the protected-settings
before/after snapshot, the 50ms typing-delay check, and the final report
values -- all computed from prepared in-memory content, not from re-reading
disk. Nothing is backed up or written during this phase. If ANY check fails
anywhere, the script aborts here and it is literally true that no file on
disk has been touched.

PHASE 2 (backup): only after every single file has passed preflight, a
timestamped backup is made of every file that will change. If any backup
fails, the script aborts before writing anything.

PHASE 3 (write): only after every backup succeeded, the prepared new
content is written to each file. If a write fails partway through, the
script attempts a best-effort rollback of every file it already wrote in
this run, from the backups just created.

Default mode is --dry-run (no files are touched, only reports what would
change and runs every validation, including backup-phase and write-phase
dry-run/skip). Pass --apply to actually write.

    python3 patch_social_participation_and_lifestate.py            # dry run
    python3 patch_social_participation_and_lifestate.py --apply    # writes
"""

import argparse
import datetime
import shutil
import sys

SOURCE_ROOT = "/home/hans/tortoise-wow/modules/mod-playerbots/src/playerbot"
LIVE_CONF = "/home/hans/tortoise-server-asan/etc/aiplayerbot.conf"
LIVE_DIST = "/home/hans/tortoise-server-asan/etc/aiplayerbot.conf.dist"
MIRROR_CONF = "/home/hans/tortoise-wow/runtime-configs/aiplayerbot.conf"

TIMESTAMP = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")

PLAYERBOT_AI_CPP = f"{SOURCE_ROOT}/PlayerbotAI.cpp"
PLAYERBOT_AICONFIG_H = f"{SOURCE_ROOT}/PlayerbotAIConfig.h"
PLAYERBOT_AICONFIG_CPP = f"{SOURCE_ROOT}/PlayerbotAIConfig.cpp"
SAY_ACTION_CPP = f"{SOURCE_ROOT}/strategy/actions/SayAction.cpp"
CHAT_HELPER_H = f"{SOURCE_ROOT}/ChatHelper.h"
CHAT_HELPER_CPP = f"{SOURCE_ROOT}/ChatHelper.cpp"

TYPING_DELAY_MARKER = "packets = LinesToPackets(lines, chatTemplate, false, 50, emoteTemplate, timeDiff);"

PROTECTED_LIVE_CONF_PREFIXES = [
    "AiPlayerbot.LLMApiEndpoint",
    "AiPlayerbot.LLMModel",
    "AiPlayerbot.LLMApiJson",
    "AiPlayerbot.LLMResponseStartPattern",
    "AiPlayerbot.LLMResponseEndPattern",
    "AiPlayerbot.LLMResponseDeletePattern",
    "AiPlayerbot.LLMResponseSplitPattern",
    "AiPlayerbot.LLMContextLength",
    "AiPlayerbot.LLMGenerationTimeout",
    "AiPlayerbot.LLMMaxSimultaniousGenerations",
    "AiPlayerbot.LLMBotToBotChatChance",
    "AiPlayerbot.LLMRpgAIChatChance",
    "AiPlayerbot.ChatChance",
    "AiPlayerbot.RandomBotNonCombatStrategies",
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class AbortPatch(SystemExit):
    pass


def die(msg):
    raise AbortPatch("\nABORT (preflight): " + msg + "\n\nNo files have been modified.\n")


def read(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    except FileNotFoundError:
        die(f"expected file does not exist: {path}")
    except OSError as e:
        die(f"could not read {path}: {e}")


def apply_exact(content, old, new, label):
    """Replace exactly one occurrence of `old` with `new`. Aborts otherwise.
    Pure function: takes content, returns new content. No I/O."""
    count = content.count(old)
    if count != 1:
        die(
            f"expected exactly 1 occurrence of anchor '{label}', found {count}.\n"
            f"Anchor text was:\n-----\n{old}\n-----"
        )
    return content.replace(old, new, 1)


def snapshot_lines(text, prefixes):
    """Capture the full line for every line starting with one of `prefixes`
    (commented or not), keyed by prefix, in order. Used to prove protected
    settings are byte-identical before and after our edits -- whatever is
    actually live right now, not a hardcoded guess of its content."""
    snap = {p: [] for p in prefixes}
    for line in text.splitlines():
        bare = line.lstrip("#").lstrip()
        for p in prefixes:
            if bare.startswith(p + " ") or bare.startswith(p + "="):
                snap[p].append(line)
    return snap


def find_active_line(text, key_prefix, label):
    """Find the single UNCOMMENTED line 'key_prefix = ...' in text."""
    matches = [ln for ln in text.splitlines() if ln.startswith(key_prefix + " = ")]
    if len(matches) != 1:
        die(
            f"expected exactly 1 active (uncommented) '{key_prefix} = ...' line in "
            f"{label}, found {len(matches)}. Reporting instead of guessing:\n"
            + "\n".join(f"  {m[:200]}{'...' if len(m) > 200 else ''}" for m in matches)
        )
    return matches[0]


class PreparedChange:
    """One file's edit, fully computed and validated in memory. Nothing here
    touches disk -- backup_path is filled in only during the backup phase."""

    def __init__(self, path, original, new, label):
        self.path = path
        self.original = original
        self.new = new
        self.label = label
        self.backup_path = None


# ---------------------------------------------------------------------------
# PHASE 1 -- preflight: pure content transforms, no I/O beyond the initial
# read. Each function takes content in and returns validated new content.
# Every apply_exact() call inside these will die() (raise AbortPatch) on any
# mismatch, which unwinds straight out of preflight before anything is ever
# backed up or written.
# ---------------------------------------------------------------------------

def preflight_chat_helper_h(content):
    old = (
        "        static uint32 parseRace(const std::string& text);\n"
        "        static std::string formatRace(uint8 race);\n"
        "\n"
        "        static std::string formatFactionName(uint32 factionId);\n"
    )
    new = (
        "        static uint32 parseRace(const std::string& text);\n"
        "        static std::string formatRace(uint8 race);\n"
        "        static std::string formatLifeState(Unit* unit);\n"
        "\n"
        "        static std::string formatFactionName(uint32 factionId);\n"
    )
    return apply_exact(content, old, new, "ChatHelper.h: formatLifeState declaration")


def preflight_chat_helper_cpp(content):
    old = (
        "std::string ChatHelper::formatRace(uint8 race)\n"
        "{\n"
        "    return races[race];\n"
        "}\n"
        "\n"
        "std::string ChatHelper::formatFactionName(uint32 factionId)\n"
    )
    # Every DeathState value is handled explicitly. An unknown/future state
    # falls through the switch's default to "unknown", never silently
    # becoming an authoritative "alive".
    new = (
        "std::string ChatHelper::formatRace(uint8 race)\n"
        "{\n"
        "    return races[race];\n"
        "}\n"
        "\n"
        "std::string ChatHelper::formatLifeState(Unit* unit)\n"
        "{\n"
        "    if (!unit)\n"
        "        return \"unknown\"; // missing state must never be reported as an authoritative \"alive\"\n"
        "\n"
        "    switch (unit->GetDeathState())\n"
        "    {\n"
        "        case JUST_DIED:\n"
        "        case CORPSE:\n"
        "        case CORPSE_FALLING:\n"
        "            return \"dead\";\n"
        "        case DEAD:\n"
        "            return \"ghost\";\n"
        "        case ALIVE:\n"
        "        case JUST_ALIVED:\n"
        "            return \"alive\";\n"
        "        default:\n"
        "            return \"unknown\"; // unhandled/future DeathState value -- never guess \"alive\"\n"
        "    }\n"
        "}\n"
        "\n"
        "std::string ChatHelper::formatFactionName(uint32 factionId)\n"
    )
    return apply_exact(content, old, new, "ChatHelper.cpp: formatLifeState implementation")


def preflight_say_action_cpp(content):
    # 1) <bot life state> / <other life state> via the existing generic
    #    per-unit placeholder builder -- no new call sites needed.
    old_life = (
        '    placeholders["<" + preFix + " race>"] = ChatHelper::formatRace(unit->getRace());\n'
        "\n"
        "    FactionTemplateEntry const* factionTemplate = unit->GetFactionTemplateEntry();\n"
    )
    new_life = (
        '    placeholders["<" + preFix + " race>"] = ChatHelper::formatRace(unit->getRace());\n'
        '    placeholders["<" + preFix + " life state>"] = ChatHelper::formatLifeState(unit);\n'
        "\n"
        "    FactionTemplateEntry const* factionTemplate = unit->GetFactionTemplateEntry();\n"
    )
    content = apply_exact(content, old_life, new_life, "SayAction.cpp: <bot/other life state> placeholder")

    # 2) <chat addressing note>, built from guid2 -- empty for ordinary
    #    replies, populated only when the routing layer picked this bot to
    #    interject on behalf of a third party.
    old_note = (
        "                GetAIChatPlaceholders(placeholders, bot, player);\n"
        '                GetAIChatPlaceholders(placeholders, bot, "bot");\n'
        '                GetAIChatPlaceholders(placeholders, player, "other");\n'
        "\n"
        "                std::map<ChatChannelSource, std::string> sourceName;\n"
    )
    new_note = (
        "                GetAIChatPlaceholders(placeholders, bot, player);\n"
        '                GetAIChatPlaceholders(placeholders, bot, "bot");\n'
        '                GetAIChatPlaceholders(placeholders, player, "other");\n'
        "\n"
        '                placeholders["<chat addressing note>"] = "";\n'
        "                if (guid2)\n"
        "                {\n"
        "                    if (Player* addressed = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid2)))\n"
        "                    {\n"
        "                        std::string addressedName = addressed->GetName();\n"
        '                        placeholders["<chat addressing note>"] =\n'
        '                            "This message was addressed to " + addressedName + ", not to you. You are interjecting as a "\n'
        '                            "third party -- do not treat statements, questions, insults, praise, or descriptions about " +\n'
        '                            addressedName + " as if they were about you. Respond from the perspective of an outside "\n'
        '                            "participant in their conversation.";\n'
        "                    }\n"
        "                }\n"
        "\n"
        "                std::map<ChatChannelSource, std::string> sourceName;\n"
    )
    content = apply_exact(content, old_note, new_note, "SayAction.cpp: <chat addressing note> from guid2")

    # Sanity check on the PREPARED content (not a re-read of the untouched
    # original): the 50ms typing delay must still be present unchanged.
    if content.count(TYPING_DELAY_MARKER) != 1:
        die(
            "the 50ms typing-delay marker is not present exactly once in the PREPARED "
            f"SayAction.cpp content: '{TYPING_DELAY_MARKER}'. This patch must never "
            "touch that value."
        )

    return content


def preflight_playerbot_ai_config_h(content):
    old = (
        "    uint32 broadcastChanceGuildManagement;\n"
        "\n"
        "    uint32 guildRepliesRate;\n"
    )
    new = (
        "    uint32 broadcastChanceGuildManagement;\n"
        "\n"
        "    uint32 guildRepliesRate;\n"
        "\n"
        "    uint32 partyInterjectionChance;\n"
    )
    return apply_exact(content, old, new, "PlayerbotAIConfig.h: partyInterjectionChance field")


def preflight_playerbot_ai_config_cpp(content):
    # 1) Load the new config key, default 10, right after guildRepliesRate.
    old_load = (
        '    toxicLinksRepliesChance = config.GetIntDefault("AiPlayerbot.ToxicLinksRepliesChance", 30); //0-100\n'
        '    thunderfuryRepliesChance = config.GetIntDefault("AiPlayerbot.ThunderfuryRepliesChance", 40); //0-100\n'
        '    guildRepliesRate = config.GetIntDefault("AiPlayerbot.GuildRepliesRate", 100); //0-100\n'
    )
    new_load = (
        '    toxicLinksRepliesChance = config.GetIntDefault("AiPlayerbot.ToxicLinksRepliesChance", 30); //0-100\n'
        '    thunderfuryRepliesChance = config.GetIntDefault("AiPlayerbot.ThunderfuryRepliesChance", 40); //0-100\n'
        '    guildRepliesRate = config.GetIntDefault("AiPlayerbot.GuildRepliesRate", 100); //0-100\n'
        '    partyInterjectionChance = config.GetIntDefault("AiPlayerbot.PartyInterjectionChance", 10); //0-100\n'
    )
    content = apply_exact(content, old_load, new_load, "PlayerbotAIConfig.cpp: partyInterjectionChance load")

    # 2) Update the compiled-in SHORT default LLMPrePrompt (used only when no
    #    conf override is set at all) so fresh installs get both new
    #    placeholders too. This is NOT Hans's tuned live prompt.
    old_default = (
        'llmPrePrompt = config.GetStringDefault("AiPlayerbot.LLMPrePrompt", "You are a roleplaying character in '
        "World of Warcraft: <expansion name>. Your name is <bot name>. The <other type> <other name> is speaking "
        "to you <channel name> and is an <other gender> <other race> <other class> of level <other level>. You "
        "are level <bot level> and play as a <bot gender> <bot race> <bot class> that is currently in <bot "
        'subzone> <bot zone>. Answer as a roleplaying character. Limit responses to 100 characters.");\n'
    )
    new_default = (
        'llmPrePrompt = config.GetStringDefault("AiPlayerbot.LLMPrePrompt", "You are a roleplaying character in '
        "World of Warcraft: <expansion name>. Your name is <bot name>. The <other type> <other name> is speaking "
        "to you <channel name> and is an <other gender> <other race> <other class> of level <other level>. You "
        "are level <bot level> and play as a <bot gender> <bot race> <bot class> that is currently in <bot "
        "subzone> <bot zone>. You are <bot life state>; <other name> is <other life state>. If you are dead or a "
        "ghost, do not describe yourself as currently fighting, questing, or traveling normally -- you may talk "
        "about your death, your corpse run, or your plans once resurrected. <chat addressing note> Answer as a "
        'roleplaying character. Limit responses to 100 characters.");\n'
    )
    content = apply_exact(content, old_default, new_default, "PlayerbotAIConfig.cpp: compiled-in default LLMPrePrompt")
    return content


def preflight_playerbot_ai_cpp(content):
    # 0) Explicit <functional> include for std::hash<std::string> -- do not
    #    rely on it arriving transitively via ObjectGuid.h.
    old_include = (
        '#include "PlayerbotMgr.h"\n'
        '#include "playerbot/playerbot.h"\n'
        '#include "playerbot/AiContextAugment.h"\n'
        '#include "playerbot/PerformanceMonitor.h"\n'
        "#include <stdarg.h>\n"
        "#include <iomanip>\n"
    )
    new_include = (
        '#include "PlayerbotMgr.h"\n'
        '#include "playerbot/playerbot.h"\n'
        '#include "playerbot/AiContextAugment.h"\n'
        '#include "playerbot/PerformanceMonitor.h"\n'
        "#include <stdarg.h>\n"
        "#include <iomanip>\n"
        "#include <functional>\n"
    )
    content = apply_exact(content, old_include, new_include, "PlayerbotAI.cpp: #include <functional>")

    # EDIT A: replace the otherMemberMentioned bool-scan with the
    # namedOtherMemberGuids computation + full interjection decision
    # (identical candidate set, message-level gate, deterministic winner,
    # this-bot-only cooldown).
    old_a = (
        "                bool otherMemberMentioned = false;\n"
        "                if (isAiChat && !isFromFreeBot && !isMentioned && chatChannelSource == ChatChannelSource::SRC_PARTY)\n"
        "                {\n"
        "                    if (Group* group = bot->GetGroup())\n"
        "                    {\n"
        "                        for (GroupReference* ref = group->GetFirstMember(); ref && !otherMemberMentioned; ref = ref->next())\n"
        "                        {\n"
        "                            Player* member = ref->getSource();\n"
        "                            if (!member || member == bot)\n"
        "                                continue;\n"
        "\n"
        "                            if (ChatHelper::isNameMentioned(message, member->GetName()))\n"
        "                                otherMemberMentioned = true;\n"
        "                        }\n"
        "                    }\n"
        "                }\n"
    )
    new_a = (
        "                // Scoped to party chat only. CHAT_MSG_PARTY (and, on builds where\n"
        "                // it exists, CHAT_MSG_PARTY_LEADER) are the only message types\n"
        "                // GetChatChannelSource() maps to SRC_PARTY. Guild/say/yell/whisper/\n"
        "                // channel chat are intentionally left untouched. Raid chat (SRC_RAID)\n"
        "                // is out of scope too -- CHAT_MSG_RAID is already filtered out earlier\n"
        "                // in this function and never reaches this point today.\n"
        "                std::vector<ObjectGuid> namedOtherMemberGuids;\n"
        "                if (isAiChat && !isFromFreeBot && !isMentioned && chatChannelSource == ChatChannelSource::SRC_PARTY)\n"
        "                {\n"
        "                    if (Group* group = bot->GetGroup())\n"
        "                    {\n"
        "                        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())\n"
        "                        {\n"
        "                            Player* member = ref->getSource();\n"
        "                            if (!member || member == bot)\n"
        "                                continue;\n"
        "\n"
        "                            if (ChatHelper::isNameMentioned(message, member->GetName()))\n"
        "                                namedOtherMemberGuids.push_back(member->GetObjectGuid());\n"
        "                        }\n"
        "                    }\n"
        "                }\n"
        "\n"
        "                ObjectGuid addressedGuid;\n"
        "                bool wonInterjection = false;\n"
        "\n"
        "                // Exactly one other party member was explicitly addressed: this bot\n"
        "                // (itself not named) may be selected to interject as a third party.\n"
        "                // Multiple named members falls through unchanged and is suppressed\n"
        "                // further down below, same as the old otherMemberMentioned behavior.\n"
        "                if (namedOtherMemberGuids.size() == 1)\n"
        "                {\n"
        "                    addressedGuid = namedOtherMemberGuids.front();\n"
        "\n"
        "                    // Identical candidate set for every bot evaluating this message:\n"
        "                    // all machine-controlled group members, INCLUDING this bot itself,\n"
        "                    // excluding only the sender and the explicitly addressed member.\n"
        "                    // No cross-bot HasStrategy() or other mutable strategy reads.\n"
        "                    std::vector<ObjectGuid> candidateGuids;\n"
        "                    if (Group* group = bot->GetGroup())\n"
        "                    {\n"
        "                        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())\n"
        "                        {\n"
        "                            Player* member = ref->getSource();\n"
        "                            if (!member || !GetBotAI(member))\n"
        "                                continue;\n"
        "                            if (member->GetObjectGuid() == guid1 || member->GetObjectGuid() == addressedGuid)\n"
        "                                continue;\n"
        "                            candidateGuids.push_back(member->GetObjectGuid());\n"
        "                        }\n"
        "                    }\n"
        "\n"
        "                    if (!candidateGuids.empty())\n"
        "                    {\n"
        "                        // Captured once and reused for both the gate and the winner\n"
        "                        // hash below -- never re-queried mid-calculation.\n"
        "                        // Group::BroadcastPacket() dispatches to every group member\n"
        "                        // synchronously on the same thread, so in practice every bot\n"
        "                        // evaluating this message sees the same second here. That is\n"
        "                        // a pragmatic Phase-1 property, not a mathematical guarantee.\n"
        "                        const time_t messageEpoch = time(nullptr);\n"
        "\n"
        '                        std::string gateKey = std::to_string(guid1.GetCounter()) + "|" + message + "|" +\n'
        "                            std::to_string(static_cast<uint32>(chatChannelSource)) + \"|\" +\n"
        "                            std::to_string(messageEpoch) + \"|gate\";\n"
        "                        uint32 gateRoll = static_cast<uint32>(std::hash<std::string>()(gateKey) % 100);\n"
        "\n"
        "                        if (gateRoll < sPlayerbotAIConfig.partyInterjectionChance)\n"
        "                        {\n"
        "                            ObjectGuid winnerGuid = candidateGuids.front();\n"
        '                            std::string firstWinnerKey = std::to_string(guid1.GetCounter()) + "|" + message + "|" +\n'
        "                                std::to_string(static_cast<uint32>(chatChannelSource)) + \"|\" +\n"
        "                                std::to_string(messageEpoch) + \"|\" + std::to_string(winnerGuid.GetCounter()) + \"|winner\";\n"
        "                            uint64 bestScore = std::hash<std::string>()(firstWinnerKey);\n"
        "\n"
        "                            for (size_t i = 1; i < candidateGuids.size(); ++i)\n"
        "                            {\n"
        "                                ObjectGuid const& candidate = candidateGuids[i];\n"
        '                                std::string winnerKey = std::to_string(guid1.GetCounter()) + "|" + message + "|" +\n'
        "                                    std::to_string(static_cast<uint32>(chatChannelSource)) + \"|\" +\n"
        "                                    std::to_string(messageEpoch) + \"|\" + std::to_string(candidate.GetCounter()) + \"|winner\";\n"
        "                                uint64 score = std::hash<std::string>()(winnerKey);\n"
        "                                if (score > bestScore)\n"
        "                                {\n"
        "                                    bestScore = score;\n"
        "                                    winnerGuid = candidate;\n"
        "                                }\n"
        "                            }\n"
        "\n"
        "                            if (winnerGuid == bot->GetObjectGuid())\n"
        "                            {\n"
        "                                // This-bot-only cooldown -- never inspects another bot's state.\n"
        '                                time_t lastInterjection = GetAiObjectContext()->GetValue<time_t>("last said", "interjection")->Get();\n'
        "                                if (time(0) >= lastInterjection)\n"
        "                                {\n"
        '                                    GetAiObjectContext()->GetValue<time_t>("last said", "interjection")->Set(time(0) + urand(45, 90));\n'
        "                                    wonInterjection = true;\n"
        "                                }\n"
        "                            }\n"
        "                        }\n"
        "                    }\n"
        "                }\n"
    )
    content = apply_exact(content, old_a, new_a, "PlayerbotAI.cpp: interjection decision block")

    # EDIT B: replace the old suppression check + its comment.
    old_b = (
        "                // otherMemberMentioned can only be true for a party message where this\n"
        "                // bot itself was not named (see the SRC_PARTY-scoped check above).\n"
        "                if (otherMemberMentioned)\n"
        "                    return;\n"
    )
    new_b = (
        "                // Multiple-other-addressee case lands here too (namedOtherMemberGuids\n"
        "                // stayed non-empty, wonInterjection stays false), unconditionally\n"
        "                // suppressed. Directly addressed bots (isMentioned == true) never\n"
        "                // populated namedOtherMemberGuids and are unaffected.\n"
        "                if (!namedOtherMemberGuids.empty() && !wonInterjection)\n"
        "                    return;\n"
    )
    content = apply_exact(content, old_b, new_b, "PlayerbotAI.cpp: suppression check")

    # EDIT C: guid2 now carries the addressed player's guid on a won interjection.
    old_c = "QueueChatResponse(msgtype, guid1, ObjectGuid(), message, chanName, name, isAiChat);"
    new_c = "QueueChatResponse(msgtype, guid1, wonInterjection ? addressedGuid : ObjectGuid(), message, chanName, name, isAiChat);"
    content = apply_exact(content, old_c, new_c, "PlayerbotAI.cpp: QueueChatResponse guid2")

    return content


def preflight_live_conf(mirror_content, live_content):
    mirror_line = find_active_line(mirror_content, "AiPlayerbot.LLMPrePrompt",
                                    "mirror (tortoise-wow/runtime-configs/aiplayerbot.conf)")
    live_line = find_active_line(live_content, "AiPlayerbot.LLMPrePrompt",
                                  "live (tortoise-server-asan/etc/aiplayerbot.conf)")

    if mirror_line != live_line:
        die(
            "the mirrored runtime-config snapshot does not match the live config's "
            "AiPlayerbot.LLMPrePrompt line -- the mirror is stale relative to the live "
            "file (or vice versa). Re-run Hans's sync script and retry rather than "
            "editing blind.\n\n--- mirror ---\n" + mirror_line[:4000] +
            "\n\n--- live ---\n" + live_line[:4000]
        )

    before_protected = snapshot_lines(live_content, PROTECTED_LIVE_CONF_PREFIXES)

    content = live_content

    # 1) Life-state sentence, spliced right after the existing location/speaker
    #    context sentence and before the "Party and raid chat..." sentence.
    old_life = (
        "is speaking to you <channel name>. Party and raid chat can reach characters "
        "anywhere in the world and does not mean you are physically together."
    )
    new_life = (
        "is speaking to you <channel name>. You are currently <bot life state>; <other name> "
        "is currently <other life state>. If you are dead or a ghost, do not describe yourself "
        "as currently fighting, protecting someone, questing, or travelling normally -- you may "
        "talk about your death, your corpse run, or your plans once you return to life. Party "
        "and raid chat can reach characters anywhere in the world and does not mean you are "
        "physically together."
    )
    content = apply_exact(content, old_life, new_life, "live conf: LLMPrePrompt life-state insertion")

    # 2) <chat addressing note>, spliced immediately before the existing
    #    "Answer the current message directly..." instruction.
    old_note = (
        "travelling together unless the stated locations make that realistic. Answer the "
        "current message directly and coherently first;"
    )
    new_note = (
        "travelling together unless the stated locations make that realistic. <chat addressing "
        "note> Answer the current message directly and coherently first;"
    )
    content = apply_exact(content, old_note, new_note, "live conf: <chat addressing note> insertion")

    # 3) New tunable, placed right before the (now-modified) LLMPrePrompt line.
    old_chance = "AiPlayerbot.LLMRpgAIChatChance = 0\n\nAiPlayerbot.LLMPrePrompt = "
    new_chance = "AiPlayerbot.LLMRpgAIChatChance = 0\n\nAiPlayerbot.PartyInterjectionChance = 10\n\nAiPlayerbot.LLMPrePrompt = "
    content = apply_exact(content, old_chance, new_chance, "live conf: AiPlayerbot.PartyInterjectionChance")

    after_protected = snapshot_lines(content, PROTECTED_LIVE_CONF_PREFIXES)
    if before_protected != after_protected:
        die(
            "a protected live-config setting (Ollama endpoint/model/json, response "
            "patterns, context/timeout/generation limits, bot-to-bot/rpg chat chance) "
            "would change as a side effect of this edit. This must never happen -- "
            f"aborting.\nbefore: {before_protected}\nafter:  {after_protected}"
        )

    return content


def preflight_live_dist(content):
    old_default = (
        "# AiPlayerbot.LLMPrePrompt = You are a roleplaying character in World of Warcraft: "
        "<expansion name>. Your name is <bot name>. The <other type> <other name> is speaking "
        "to you <channel name> and is an <other gender> <other race> <other class> of level "
        "<other level>. You are level <bot level> and play as a <bot gender> <bot race> <bot "
        "class> that is currently in <bot subzone> <bot zone>. Answer as a roleplaying "
        "character. Limit responses to 100 characters."
    )
    new_default = (
        "# AiPlayerbot.LLMPrePrompt = You are a roleplaying character in World of Warcraft: "
        "<expansion name>. Your name is <bot name>. The <other type> <other name> is speaking "
        "to you <channel name> and is an <other gender> <other race> <other class> of level "
        "<other level>. You are level <bot level> and play as a <bot gender> <bot race> <bot "
        "class> that is currently in <bot subzone> <bot zone>. You are <bot life state>; <other "
        "name> is <other life state>. If you are dead or a ghost, do not describe yourself as "
        "currently fighting, questing, or traveling normally -- you may talk about your death, "
        "your corpse run, or your plans once resurrected. <chat addressing note> Answer as a "
        "roleplaying character. Limit responses to 100 characters."
    )
    content = apply_exact(content, old_default, new_default, "live dist: default LLMPrePrompt documentation")

    old_doc = (
        "# Channels that bots are not allowed to use ai-chat to reply on. Channels: "
        "guild,world,general,trade,lfg,ldefence,wdefence,grecruitement,say,whisper,emote,"
        "temote,yell,party,raid\n"
        "# AiPlayerbot.LLMBlockedReplyChannels = \n"
        "\n"
        "# OPEN-AI (chat completions) example:"
    )
    new_doc = (
        "# Channels that bots are not allowed to use ai-chat to reply on. Channels: "
        "guild,world,general,trade,lfg,ldefence,wdefence,grecruitement,say,whisper,emote,"
        "temote,yell,party,raid\n"
        "# AiPlayerbot.LLMBlockedReplyChannels = \n"
        "\n"
        "# The chance (0-100) a non-addressed party member may spontaneously interject when\n"
        "# exactly one OTHER party member was explicitly addressed by name in party chat.\n"
        "# This is a single message-level roll shared by all bots, not a per-bot roll.\n"
        "# AiPlayerbot.PartyInterjectionChance = 10\n"
        "\n"
        "# OPEN-AI (chat completions) example:"
    )
    content = apply_exact(content, old_doc, new_doc, "live dist: PartyInterjectionChance documentation")

    return content


def run_preflight():
    """Reads every file and computes every edit, entirely in memory. Returns
    the list of PreparedChange. Raises AbortPatch (via die()) on the first
    problem found, before anything has been backed up or written."""
    changes = []

    def prep(path, fn, label, *extra_args):
        original = read(path)
        new = fn(original, *extra_args) if extra_args else fn(original)
        changes.append(PreparedChange(path, original, new, label))
        return new

    prep(CHAT_HELPER_H, preflight_chat_helper_h, "ChatHelper.h")
    prep(CHAT_HELPER_CPP, preflight_chat_helper_cpp, "ChatHelper.cpp")
    prep(SAY_ACTION_CPP, preflight_say_action_cpp, "SayAction.cpp")
    prep(PLAYERBOT_AICONFIG_H, preflight_playerbot_ai_config_h, "PlayerbotAIConfig.h")
    prep(PLAYERBOT_AICONFIG_CPP, preflight_playerbot_ai_config_cpp, "PlayerbotAIConfig.cpp")
    prep(PLAYERBOT_AI_CPP, preflight_playerbot_ai_cpp, "PlayerbotAI.cpp")

    mirror_content = read(MIRROR_CONF)  # read-only cross-check, never in `changes`
    live_conf_content = read(LIVE_CONF)
    new_live_conf = preflight_live_conf(mirror_content, live_conf_content)
    changes.append(PreparedChange(LIVE_CONF, live_conf_content, new_live_conf, "live aiplayerbot.conf"))

    live_dist_content = read(LIVE_DIST)
    new_live_dist = preflight_live_dist(live_dist_content)
    changes.append(PreparedChange(LIVE_DIST, live_dist_content, new_live_dist, "live aiplayerbot.conf.dist"))

    return changes


# ---------------------------------------------------------------------------
# PHASE 2 -- backup. Only reached after every file above passed preflight.
# ---------------------------------------------------------------------------

def run_backup_phase(changes):
    for c in changes:
        bak = f"{c.path}.bak_{TIMESTAMP}"
        try:
            shutil.copy2(c.path, bak)
        except OSError as e:
            die(
                f"failed to create backup for {c.path} -> {bak}: {e}. "
                "No source/config file content has been modified; any backups "
                "created earlier in this run for other files are harmless extra "
                "copies, not changes to your working files."
            )
        c.backup_path = bak


# ---------------------------------------------------------------------------
# PHASE 3 -- write. Only reached after every backup above succeeded. On a
# write failure partway through, roll back every file already written in
# this run from the backup just made for it.
# ---------------------------------------------------------------------------

def run_write_phase(changes):
    written = []
    try:
        for c in changes:
            with open(c.path, "w", encoding="utf-8") as f:
                f.write(c.new)
            written.append(c)
    except OSError as e:
        print(f"\nWRITE FAILED partway through ({e}).", file=sys.stderr)
        print("Attempting best-effort rollback of files already written this run...", file=sys.stderr)
        for c in written:
            try:
                shutil.copy2(c.backup_path, c.path)
                print(f"  rolled back: {c.path}  (from {c.backup_path})", file=sys.stderr)
            except OSError as e2:
                print(
                    f"  ROLLBACK FAILED for {c.path}: {e2} -- MANUAL RECOVERY NEEDED "
                    f"from backup {c.backup_path}", file=sys.stderr
                )
        raise


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--apply", action="store_true", help="Actually write files. Default is dry-run (validate only).")
    args = parser.parse_args()
    dry_run = not args.apply

    print(f"Mode: {'APPLY (writing files)' if not dry_run else 'DRY RUN (validate only, no files written)'}")
    print()
    print("Phase 1: preflight (read + validate + compute every edit in memory)...")
    changes = run_preflight()
    print(f"  all {len(changes)} files passed preflight validation.")

    if not dry_run:
        print()
        print("Phase 2: creating timestamped backups...")
        run_backup_phase(changes)
        for c in changes:
            print(f"  backup: {c.backup_path}")

        print()
        print("Phase 3: writing prepared content...")
        run_write_phase(changes)
        for c in changes:
            print(f"  wrote: {c.path}")

    # Final report values, always pulled from the PREPARED in-memory content
    # (changes[...].new), never from re-reading disk -- so the dry-run report
    # reflects what the edit actually produces, not the untouched original.
    by_path = {c.path: c for c in changes}
    resulting_chance_line = find_active_line(by_path[LIVE_CONF].new, "AiPlayerbot.PartyInterjectionChance", "prepared live conf")
    resulting_preprompt_line = find_active_line(by_path[LIVE_CONF].new, "AiPlayerbot.LLMPrePrompt", "prepared live conf")
    typing_delay_ok = TYPING_DELAY_MARKER in by_path[SAY_ACTION_CPP].new

    print()
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"Files {'that would change' if dry_run else 'changed'} ({len(changes)}):")
    for c in changes:
        print(f"  {c.path}  [{c.label}]")
    if not dry_run:
        print(f"\nBackups created ({len(changes)}):")
        for c in changes:
            print(f"  {c.backup_path}")
    print(f"\nResulting AiPlayerbot.PartyInterjectionChance line:\n  {resulting_chance_line}")
    print(f"\nResulting AiPlayerbot.LLMPrePrompt line:\n  {resulting_preprompt_line}")
    print(f"\nTyping delay still 50ms in prepared content: {'YES' if typing_delay_ok else 'NO -- INVESTIGATE'}")
    print("Protected Ollama endpoint/model/json + response patterns: unchanged (verified in preflight; would have aborted otherwise)")
    print()
    if dry_run:
        print("This was a DRY RUN. No files were written. Re-run with --apply to write.")
    else:
        print(f"Files written. Review a git diff (or the .bak_{TIMESTAMP} backups) before restarting the server.")


if __name__ == "__main__":
    try:
        main()
    except AbortPatch as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)
