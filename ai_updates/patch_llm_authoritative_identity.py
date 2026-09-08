#!/usr/bin/env python3

import argparse
import datetime
import shutil
from pathlib import Path

CONF = Path("/home/hans/tortoise-server-asan/etc/aiplayerbot.conf")

ANCHOR = (
    "Current game state and explicit human corrections override your own earlier statements and assumptions."
)

INSERT = (
    " Current location is only where you are now. Never turn a current zone or subzone into your home, residence, "
    "birthplace, origin, hometown, or permanent base unless that is explicitly supplied by authoritative game context "
    "or your character card. Being in Sentinel Hill does not mean you live in Sentinel Hill. "
    "Treat your race, class, and life state as separate authoritative game facts. Your class never changes your race "
    "or life state. A Human warlock is still Human; being a warlock does not make you Undead, undead, a living dead "
    "person, a ghost, a demon, or otherwise deceased. Only <bot life state> determines whether you are alive, dead, "
    "or a ghost. Likewise, another player's class does not change their race or life state. "
    "When a human points out that something you previously said was wrong, contradictory, confusing, or invented, "
    "acknowledge the mistake and drop the false claim. Do not preserve it by inventing an explanation, backstory, "
    "metaphor, magical condition, class-related excuse, or additional lore. "
    "Answer corrections about your previous statement directly. Do not respond to a correction by changing the "
    "subject or inventing unrelated events, wars, demons, journeys, or history."
)

MARKERS = [
    "Current location is only where you are now.",
    "Treat your race, class, and life state as separate authoritative game facts.",
    "being a warlock does not make you Undead",
    "Do not preserve it by inventing an explanation",
    "Answer corrections about your previous statement directly.",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    text = CONF.read_text(encoding="utf-8")

    active = [
        line for line in text.splitlines()
        if line.startswith("AiPlayerbot.LLMPrePrompt = ")
    ]

    if len(active) != 1:
        raise SystemExit(
            f"ABORT: expected exactly 1 active AiPlayerbot.LLMPrePrompt, found {len(active)}"
        )

    if text.count(ANCHOR) != 1:
        raise SystemExit(
            f"ABORT: expected exactly 1 anchor, found {text.count(ANCHOR)}"
        )

    if any(marker in text for marker in MARKERS):
        raise SystemExit("ABORT: authoritative identity rules already appear to be installed")

    new_text = text.replace(ANCHOR, ANCHOR + INSERT, 1)

    for marker in MARKERS:
        if marker not in new_text:
            raise SystemExit(f"ABORT: prepared prompt missing marker: {marker}")

    # Make sure earlier important rules survive.
    required_existing = [
        "<bot life state>",
        "<other life state>",
        "<chat addressing note>",
        "A player's claim about a shared past event is a conversational claim",
        "Copy official names character-for-character exactly as given.",
    ]

    for marker in required_existing:
        if marker not in new_text:
            raise SystemExit(f"ABORT: existing prompt marker missing: {marker}")

    print("Preflight OK.")
    print()
    print("Adds:")
    print("  - current location cannot become home/origin")
    print("  - race/class/life-state remain separate authoritative facts")
    print("  - warlock does not imply Undead/dead/ghost/demon")
    print("  - corrections must replace false claims, not generate rescue-lore")
    print("  - corrections must be answered directly")
    print()

    if not args.apply:
        print("DRY RUN: nothing written.")
        print("Run again with --apply after review.")
        return

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = Path(str(CONF) + f".bak_identity_{stamp}")

    shutil.copy2(CONF, backup)
    CONF.write_text(new_text, encoding="utf-8")

    print(f"Updated: {CONF}")
    print(f"Backup : {backup}")


if __name__ == "__main__":
    main()
