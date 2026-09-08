#!/usr/bin/env python3

import argparse
import datetime
import shutil
from pathlib import Path

CONF = Path("/home/hans/tortoise-server-asan/etc/aiplayerbot.conf")

OLD = (
    "Party and raid chat can reach characters anywhere in the world and does not mean you are physically together. "
    "Never claim that you are standing together, waiting nearby, hunting together, protecting someone nearby, or "
    "travelling together unless the stated locations make that realistic."
)

NEW = (
    "Party and raid chat can reach characters anywhere in the world and does not mean you are physically together. "
    "Never claim that you are standing together, waiting nearby, hunting together, protecting someone nearby, or "
    "travelling together unless the stated locations make that realistic. "
    "Treat location as silent background context. Do not mention, explain, compare, or remind players of locations "
    "unless the current message specifically asks about location, travel, meeting, distance, or physical proximity. "
    "Use location internally to avoid impossible claims, but otherwise talk normally. "
    "Do not introduce your own zone or another player's zone merely as conversational flavor. "
    "If a player says location details are unnecessary, stop mentioning them and do not lecture them about location."
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()

    text = CONF.read_text(encoding="utf-8")

    # Make sure there is exactly one active tuned prompt.
    active = [
        line for line in text.splitlines()
        if line.startswith("AiPlayerbot.LLMPrePrompt = ")
    ]

    if len(active) != 1:
        raise SystemExit(
            f"ABORT: expected exactly 1 active AiPlayerbot.LLMPrePrompt, found {len(active)}"
        )

    count = text.count(OLD)
    if count != 1:
        raise SystemExit(
            f"ABORT: expected old location rule exactly once, found {count}. "
            "Nothing changed."
        )

    # Don't accidentally apply twice.
    if "Treat location as silent background context." in text:
        raise SystemExit(
            "ABORT: new silent-location rule is already present. Nothing changed."
        )

    new_text = text.replace(OLD, NEW, 1)

    # Verify we didn't lose important recent placeholders.
    required = [
        "<bot life state>",
        "<other life state>",
        "<chat addressing note>",
        "Named players and party members are real player characters",
        "Official WoW class names",
        "official WoW race names",
    ]

    for marker in required:
        if marker not in new_text:
            raise SystemExit(
                f"ABORT: required existing prompt marker disappeared: {marker}"
            )

    print("Preflight OK.")
    print()
    print("Will replace the current location rule with:")
    print(NEW)
    print()

    if not args.apply:
        print("DRY RUN: nothing written.")
        print("Run again with --apply to make the change.")
        return

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = Path(str(CONF) + f".bak_location_{stamp}")

    shutil.copy2(CONF, backup)
    CONF.write_text(new_text, encoding="utf-8")

    print(f"Updated: {CONF}")
    print(f"Backup : {backup}")
    print()
    print("Location is now treated as silent background context.")


if __name__ == "__main__":
    main()
