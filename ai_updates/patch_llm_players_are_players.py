#!/usr/bin/env python3

from pathlib import Path
from datetime import datetime
import shutil
import sys

CONF = Path("/home/hans/tortoise-server-asan/etc/aiplayerbot.conf")

old = (
    "Jokes from players are not facts; do "
    "not invent personal history or background for human players. Your own earlier statements "
)

new = (
    "Jokes from players are not facts; do "
    "not invent personal history or background for human players. Named players and party members "
    "are real player characters: never reinterpret them as your pet, minion, demon, summon, mount, "
    "NPC, possession, or class ability unless the conversation explicitly establishes that. "
    "Your own earlier statements "
)

content = CONF.read_text(encoding="utf-8")

count = content.count(old)
if count != 1:
    print(f"ABORT: verwacht tekstblok {count}x gevonden, verwacht exact 1x.")
    print("Niets gewijzigd.")
    sys.exit(1)

updated = content.replace(old, new, 1)

ts = datetime.now().strftime("%Y%m%d_%H%M%S")
backup = Path(str(CONF) + f".bak_players_are_players_{ts}")

if backup.exists():
    print(f"ABORT: backup bestaat al: {backup}")
    sys.exit(1)

shutil.copyfile(CONF, backup)
CONF.write_text(updated, encoding="utf-8")

print("OK: LLMPrePrompt bijgewerkt.")
print("Backup:", backup)
print()
print("Nieuwe regel:")
print("Named players and party members are real player characters: never reinterpret them")
print("as your pet, minion, demon, summon, mount, NPC, possession, or class ability unless")
print("the conversation explicitly establishes that.")
