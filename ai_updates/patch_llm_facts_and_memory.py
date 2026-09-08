#!/usr/bin/env python3

import argparse
import datetime
import shutil
from pathlib import Path

CONF = Path("/home/hans/tortoise-server-asan/etc/aiplayerbot.conf")

OLD_LOCATION = (
    "Treat location as silent background context. "
    "Do not mention, explain, compare, or remind players of locations unless the current message specifically asks "
    "about location, travel, meeting, distance, or physical proximity. "
    "Use location internally to avoid impossible claims, but otherwise talk normally. "
    "Do not introduce your own zone or another player's zone merely as conversational flavor. "
    "If a player says location details are unnecessary, stop mentioning them and do not lecture them about location."
)

NEW_LOCATION = (
    "Treat location as silent background context. "
    "Do not mention your own location or another player's location unless the current message directly asks where "
    "someone is, asks about travel, meeting, distance, or physical proximity, or the location is essential to answer "
    "the question correctly. Merely knowing a location is never a reason to mention it. "
    "Use location internally to avoid impossible claims, but otherwise talk normally. "
    "Do not introduce, repeat, compare, or remind players of zone or subzone names as conversational flavor. "
    "If a player says location details are unnecessary, stop mentioning them and do not lecture them about location."
)

OLD_NAMES = (
    "Official WoW and Turtle-WoW place names, zone names, subzone names, city names, settlement names, dungeon names, "
    "faction names, class names and race names must stay exactly as provided by the game data. "
    "Never translate, localize, paraphrase, or invent Dutch versions of them. "
    "When referring to a location from <bot zone>, <bot subzone>, <other zone>, or <other subzone>, "
    "copy that name exactly as given."
)

NEW_NAMES = (
    "Official WoW and Turtle-WoW place names, zone names, subzone names, city names, settlement names, dungeon names, "
    "faction names, class names and race names must stay exactly as provided by the game data. "
    "Copy official names character-for-character exactly as given. Never translate, localize, paraphrase, respell, "
    "phonetically rewrite, pluralize, inflect, or invent altered versions of them. "
    "When referring to a location from <bot zone>, <bot subzone>, <other zone>, or <other subzone>, "
    "copy that name exactly as given. "
    "Do not invent nonexistent places, cities, regions, factions, organizations, or game lore. "
    "If a player corrects a place or lore claim you invented and current game context does not support your claim, "
    "drop it immediately instead of defending or expanding it."
)

OLD_HISTORY = (
    "Jokes from players are not facts; do not invent personal history or background for human players."
)

NEW_HISTORY = (
    "Jokes from players are not facts; do not invent personal history or background for human players. "
    "Do not invent your own birthplace, hometown, childhood, family, training history, former residence, previous "
    "occupation, affiliations, or personal history unless it is explicitly supplied by your character card or game "
    "context. If asked about personal history you do not actually know, say that you do not know or do not remember "
    "instead of inventing an answer. "
    "A player's claim about a shared past event is a conversational claim, not proof that you personally remember it. "
    "Do not say that you remember, witnessed, experienced, or took part in a past event merely because a player says "
    "that it happened. Only claim personal memory when that event was already established independently in the "
    "conversation or is provided by your character card or game context."
)

REQUIRED = [
    "<bot life state>",
    "<other life state>",
    "<chat addressing note>",
    "Named players and party members are real player characters",
    "Current game state and explicit human corrections override your own earlier statements and assumptions.",
]


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"ABORT: expected exactly 1 occurrence of {label}, found {count}. Nothing changed."
        )
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    text = CONF.read_text(encoding="utf-8")

    active = [
        line for line in text.splitlines()
        if line.startswith("AiPlayerbot.LLMPrePrompt = ")
    ]

    if len(active) != 1:
        raise SystemExit(
            f"ABORT: expected exactly 1 active AiPlayerbot.LLMPrePrompt, found {len(active)}."
        )

    for marker in REQUIRED:
        if marker not in text:
            raise SystemExit(
                f"ABORT: required existing prompt marker missing: {marker}"
            )

    if "A player's claim about a shared past event is a conversational claim" in text:
        raise SystemExit(
            "ABORT: new facts/memory rules already appear to be installed."
        )

    new_text = text

    new_text = replace_once(
        new_text,
        OLD_LOCATION,
        NEW_LOCATION,
        "current silent-location rule"
    )

    new_text = replace_once(
        new_text,
        OLD_NAMES,
        NEW_NAMES,
        "current exact-name rule"
    )

    new_text = replace_once(
        new_text,
        OLD_HISTORY,
        NEW_HISTORY,
        "current player-history rule"
    )

    new_markers = [
        "Merely knowing a location is never a reason to mention it.",
        "Copy official names character-for-character exactly as given.",
        "Do not invent nonexistent places",
        "Do not invent your own birthplace",
        "A player's claim about a shared past event is a conversational claim",
        "Do not say that you remember, witnessed, experienced, or took part",
    ]

    for marker in new_markers:
        if marker not in new_text:
            raise SystemExit(
                f"ABORT: prepared prompt is missing new marker: {marker}"
            )

    for marker in REQUIRED:
        if marker not in new_text:
            raise SystemExit(
                f"ABORT: existing prompt marker disappeared: {marker}"
            )

    print("Preflight OK.")
    print()
    print("Changes:")
    print("  - stronger silent-location rule")
    print("  - exact character-for-character WoW/Turtle-WoW names")
    print("  - no invented places/lore")
    print("  - no invented bot biography")
    print("  - player claims about shared history are not automatic memories")
    print()

    if not args.apply:
        print("DRY RUN: nothing written.")
        print("Run again with --apply after review.")
        return

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = Path(str(CONF) + f".bak_facts_memory_{stamp}")

    shutil.copy2(CONF, backup)
    CONF.write_text(new_text, encoding="utf-8")

    print(f"Updated: {CONF}")
    print(f"Backup : {backup}")


if __name__ == "__main__":
    main()
