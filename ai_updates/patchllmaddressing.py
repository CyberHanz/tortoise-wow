#!/usr/bin/env python3
"""
Bron-patch voor de "wie hoort te antwoorden" fix (addressing/mention-suppressie),
strikt beperkt tot party-chat.

- Doelbestanden zijn de BRONBESTANDEN (moeten opnieuw gecompileerd worden),
  niet de live/lopende server. Dit is dus NIET hot-reloadable zoals de
  eerdere conf/character-card patch.
- Alle exact-string-validatie gebeurt eerst, voor er iets geschreven wordt.
  Bij een mismatch stopt het script zonder iets te wijzigen. Dit is
  tegelijk de garantie die gevraagd is: als de source ook maar iets afwijkt
  van wat hieronder verwacht wordt, gebeurt er niets.
- Extra, read-only sanity check: bevestigt dat SayAction.cpp de al-geteste
  50ms typing-delay bevat (LinesToPackets(..., 50, ...)), als signaal dat dit
  echt de bijgewerkte VM-source is en niet een verouderde kopie. Deze regel
  wordt zelf NIET aangeraakt.
- Backups krijgen een tijdstip in de bestandsnaam, nooit een vaste naam.
- Raakt niet aan: de 50ms typing-delay, WorldSession.cpp, LLM prompt/config,
  response parsing/splitting, of de SendDelayedPacket lifetime-kwestie.
- Beperkt de nieuwe suppressie-logica tot chatChannelSource == SRC_PARTY
  (CHAT_MSG_PARTY, en op MANGOSBOT_TWO-builds ook CHAT_MSG_PARTY_LEADER --
  GetChatChannelSource() mapt beide al naar SRC_PARTY, dus dat hoeft hier
  niet apart herhaald te worden). Guild/say/yell/whisper/channel-chat en
  raid-chat blijven volledig ongemoeid. CHAT_MSG_RAID wordt daarnaast, zoals
  eerder vastgesteld, al eerder in de pipeline weggefilterd (PlayerbotAI.cpp,
  msgtype-filter rond r.1829-1844) en dus niet apart in deze patch behandeld.

Gebruik:
    python3 patch_llm_addressing.py
"""

import os
import shutil
import sys
from datetime import datetime

ROOT = "/home/hans/tortoise-wow/modules/mod-playerbots/src/playerbot"

CHATHELPER_H = os.path.join(ROOT, "ChatHelper.h")
CHATHELPER_CPP = os.path.join(ROOT, "ChatHelper.cpp")
PLAYERBOTAI_CPP = os.path.join(ROOT, "PlayerbotAI.cpp")
SAYACTION_CPP = os.path.join(ROOT, "strategy", "actions", "SayAction.cpp")

# Read-only: alleen gebruikt om te bevestigen dat dit de bijgewerkte VM-source
# is (de al-geteste 50ms delay). Wordt nergens vervangen of geschreven.
EXPECTED_TYPING_DELAY_MARKER = "LinesToPackets(lines, chatTemplate, false, 50, emoteTemplate, timeDiff);"

# ---------------------------------------------------------------------------
# Edits per bestand: lijst van (old, new) tuples, elk old-string moet exact
# 1x voorkomen.
# ---------------------------------------------------------------------------

EDITS = {
    CHATHELPER_H: [
        (
            "        static std::string formatFactionName(uint32 factionId);\n"
            "\n"
            "        static std::string getSkillName(uint32 skill);",
            "        static std::string formatFactionName(uint32 factionId);\n"
            "\n"
            "        // Case-insensitive, alphanumeric-boundary-aware check whether `name` is\n"
            "        // addressed inside `message` (e.g. \"Ravanne, ...\" / \"ravanne?\" match,\n"
            "        // \"Ravannestad\" does not). No regex: a plain scan, safe to call once per\n"
            "        // group member per incoming chat message.\n"
            "        static bool isNameMentioned(const std::string& message, const std::string& name);\n"
            "\n"
            "        static std::string getSkillName(uint32 skill);",
        ),
    ],
    CHATHELPER_CPP: [
        (
            "#include <numeric>\n"
            "#include <iomanip>\n"
            "#include <regex>\n"
            "#include <boost/algorithm/string.hpp>",
            "#include <numeric>\n"
            "#include <iomanip>\n"
            "#include <regex>\n"
            "#include <cctype>\n"
            "#include <boost/algorithm/string.hpp>",
        ),
        (
            "    return name;\n"
            "}\n"
            "\n"
            "uint32 ChatHelper::parseSkillName(const std::string& text)",
            "    return name;\n"
            "}\n"
            "\n"
            "bool ChatHelper::isNameMentioned(const std::string& message, const std::string& name)\n"
            "{\n"
            "    if (name.empty() || message.size() < name.size())\n"
            "        return false;\n"
            "\n"
            "    auto isBoundaryChar = [](unsigned char c) { return std::isalnum(c) == 0; };\n"
            "\n"
            "    for (size_t pos = 0; pos + name.size() <= message.size(); ++pos)\n"
            "    {\n"
            "        bool match = true;\n"
            "        for (size_t i = 0; i < name.size(); ++i)\n"
            "        {\n"
            "            if (std::tolower(static_cast<unsigned char>(message[pos + i])) != std::tolower(static_cast<unsigned char>(name[i])))\n"
            "            {\n"
            "                match = false;\n"
            "                break;\n"
            "            }\n"
            "        }\n"
            "\n"
            "        if (!match)\n"
            "            continue;\n"
            "\n"
            "        bool leftOk = (pos == 0) || isBoundaryChar(message[pos - 1]);\n"
            "        size_t afterPos = pos + name.size();\n"
            "        bool rightOk = (afterPos == message.size()) || isBoundaryChar(message[afterPos]);\n"
            "\n"
            "        if (leftOk && rightOk)\n"
            "            return true;\n"
            "    }\n"
            "\n"
            "    return false;\n"
            "}\n"
            "\n"
            "uint32 ChatHelper::parseSkillName(const std::string& text)",
        ),
    ],
    PLAYERBOTAI_CPP: [
        (
            "                bool isMentioned = message.find(bot->GetName()) != std::string::npos;",
            "                bool isMentioned = ChatHelper::isNameMentioned(message, bot->GetName());",
        ),
        (
            "ChatChannelSource chatChannelSource = GetChatChannelSource(bot, msgtype, chanName);\n"
            "\n"
            "                if (!isAiChat || isFromFreeBot)",
            "ChatChannelSource chatChannelSource = GetChatChannelSource(bot, msgtype, chanName);\n"
            "\n"
            "                // Addressing-suppression is scoped to party chat only. CHAT_MSG_PARTY\n"
            "                // (and, on builds where it exists, CHAT_MSG_PARTY_LEADER) are the only\n"
            "                // message types GetChatChannelSource() maps to SRC_PARTY, so checking\n"
            "                // SRC_PARTY here already covers both without repeating the ifdef.\n"
            "                // Guild/say/yell/whisper/channel chat are intentionally left untouched.\n"
            "                // Raid chat (SRC_RAID) is out of scope here too -- CHAT_MSG_RAID is\n"
            "                // already filtered out earlier in this function and never reaches this\n"
            "                // point today.\n"
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
            "\n"
            "                if (!isAiChat || isFromFreeBot)",
        ),
        (
            "                MANGOS_ASSERT(!message.empty());     \n"
            "                QueueChatResponse(msgtype, guid1, ObjectGuid(), message, chanName, name, isAiChat);",
            "                // otherMemberMentioned can only be true for a party message where this\n"
            "                // bot itself was not named (see the SRC_PARTY-scoped check above).\n"
            "                if (otherMemberMentioned)\n"
            "                    return;\n"
            "\n"
            "                MANGOS_ASSERT(!message.empty());     \n"
            "                QueueChatResponse(msgtype, guid1, ObjectGuid(), message, chanName, name, isAiChat);",
        ),
    ],
}


def timestamped_backup_path(path):
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = f"{path}.bak_llm_addressing_{ts}"
    if os.path.exists(backup_path):
        print(f"ABORT: backup-pad bestaat al onverwacht: {backup_path}")
        sys.exit(1)
    return backup_path


def check_typing_delay_marker():
    if not os.path.isfile(SAYACTION_CPP):
        print(f"ABORT: bestand niet gevonden: {SAYACTION_CPP}")
        sys.exit(1)

    with open(SAYACTION_CPP, "r", encoding="utf-8") as f:
        content = f.read()

    if EXPECTED_TYPING_DELAY_MARKER not in content:
        print("ABORT: sanity check gefaald -- SayAction.cpp bevat niet de verwachte")
        print(f"       50ms typing-delay regel:\n       {EXPECTED_TYPING_DELAY_MARKER!r}")
        print("       Dit lijkt een verouderde/niet-gesyncte source-kopie te zijn.")
        print("       Niets is aangeraakt (deze regel wordt sowieso nergens door dit")
        print("       script gewijzigd -- dit is puur een vers-heid-check).")
        sys.exit(1)

    print("Sanity check OK: SayAction.cpp bevat de verwachte 50ms typing-delay "
          "(niet gewijzigd, alleen gecontroleerd).")


def validate_all():
    contents = {}
    for path, edits in EDITS.items():
        if not os.path.isfile(path):
            print(f"ABORT: bestand niet gevonden: {path}")
            sys.exit(1)

        with open(path, "r", encoding="utf-8") as f:
            content = f.read()

        for i, (old, new) in enumerate(edits, start=1):
            count = content.count(old)
            if count != 1:
                print(f"ABORT: {path} edit {i}/{len(edits)} - old-string {count}x gevonden (verwacht 1x).")
                print("Niets is aangeraakt. Old-string:")
                print(repr(old[:200]))
                sys.exit(1)

        contents[path] = content
        print(f"Validatie OK: {path} ({len(edits)} edit(s) exact 1x gevonden).")

    return contents


def apply_all(contents):
    for path, edits in EDITS.items():
        backup_path = timestamped_backup_path(path)
        shutil.copyfile(path, backup_path)
        print(f"Backup geschreven: {backup_path}")

        content = contents[path]
        for i, (old, new) in enumerate(edits, start=1):
            content = content.replace(old, new)
            print(f"OK: {path} edit {i}/{len(edits)} toegepast.")

        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

        print(f"Klaar: {path} bijgewerkt.")


if __name__ == "__main__":
    check_typing_delay_marker()
    contents = validate_all()
    apply_all(contents)
    print("\nKlaar. Dit zijn BRONBESTANDEN: mangosd moet opnieuw gebouwd en naar de "
          "draaiende server gedeployed worden voor dit effect heeft. Een 'rndbot reload' "
          "volstaat hier NIET, in tegenstelling tot de eerdere conf/character-card patch.")
