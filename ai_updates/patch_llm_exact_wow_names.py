#!/usr/bin/env python3

import argparse
import datetime
import shutil
from pathlib import Path

CONF = Path("/home/hans/tortoise-server-asan/etc/aiplayerbot.conf")

OLD = (
    "Official WoW class names (warlock, hunter, rogue, mage, priest, warrior, paladin, druid, shaman) "
    "and official WoW race names (Human, Dwarf, Night Elf, Gnome, Orc, Troll, Tauren, Undead) "
    "must stay in English exactly as given, even inside a Dutch reply -- never translate them or replace "
    "them with descriptive synonyms."
)

NEW = (
    OLD +
    " Official WoW and Turtle-WoW place names, zone names, subzone names, city names, settlement names, "
    "dungeon names, faction names, class names and race names must stay exactly as provided by the game data. "
    "Never translate, localize, paraphrase, or invent Dutch versions of them. "
    "When referring to a location from <bot zone>, <bot subzone>, <other zone>, or <other subzone>, "
    "copy that name exactly as given."
)

REQUIRED_MARKERS = [
    "<bot life state>",
    "<other life state>",
    "<chat addressing note>",
    "Treat location as silent background context.",
    "Named players and party members are real player characters",
    "official WoW race names",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Actually modify the config. Default is dry-run."
    )
    args = parser.parse_args()

    text = CONF.read_text(encoding="utf-8")

    active_prompts = [
        line for line in text.splitlines()
        if line.startswith("AiPlayerbot.LLMPrePrompt = ")
    ]

    if len(active_prompts) != 1:
        raise SystemExit(
            f"ABORT: expected exactly 1 active AiPlayerbot.LLMPrePrompt, "
            f"found {len(active_prompts)}. Nothing changed."
        )

    if "copy that name exactly as given." in text:
        raise SystemExit(
            "ABORT: exact WoW/Turtle-WoW naming rule already appears to be present. "
            "Nothing changed."
        )

    count = text.count(OLD)
    if count != 1:
        raise SystemExit(
            f"ABORT: expected exact class/race anchor once, found {count}. "
            "Nothing changed."
        )

    for marker in REQUIRED_MARKERS:
        if marker not in text:
            raise SystemExit(
                f"ABORT: required existing prompt marker missing: {marker}. "
                "Nothing changed."
            )

    new_text = text.replace(OLD, NEW, 1)

    # Final sanity checks on prepared content.
    for marker in REQUIRED_MARKERS:
        if marker not in new_text:
            raise SystemExit(
                f"ABORT: existing marker disappeared after prepared edit: {marker}"
            )

    required_new = [
        "Official WoW and Turtle-WoW place names",
        "Never translate, localize, paraphrase, or invent Dutch versions of them.",
        "<bot zone>",
        "<bot subzone>",
        "<other zone>",
        "<other subzone>",
        "copy that name exactly as given.",
    ]

    for marker in required_new:
        if marker not in new_text:
            raise SystemExit(
                f"ABORT: new rule failed sanity check: {marker}"
            )

    print("Preflight OK.")
    print()
    print("New rule:")
    print(
        "Official WoW and Turtle-WoW place names, zone names, subzone names, "
        "city names, settlement names, dungeon names, faction names, class names "
        "and race names must stay exactly as provided by the game data. "
        "Never translate, localize, paraphrase, or invent Dutch versions of them. "
        "When referring to a location from <bot zone>, <bot subzone>, <other zone>, "
        "or <other subzone>, copy that name exactly as given."
    )
    print()

    if not args.apply:
        print("DRY RUN: nothing written.")
        print("Run again with --apply after review.")
        return

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = Path(str(CONF) + f".bak_exact_names_{stamp}")

    shutil.copy2(CONF, backup)
    CONF.write_text(new_text, encoding="utf-8")

    print(f"Updated: {CONF}")
    print(f"Backup : {backup}")


if __name__ == "__main__":
    main()
