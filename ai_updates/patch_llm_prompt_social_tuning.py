#!/usr/bin/env python3
from pathlib import Path
import shutil
from datetime import datetime

CONF = Path("/home/hans/tortoise-server-asan/etc/aiplayerbot.conf")

old_location = (
    "Party and raid chat can reach characters anywhere in the world and does not mean you are physically together. "
    "Never claim that you are standing together, waiting nearby, hunting together, protecting someone nearby, or "
    "travelling together unless the stated locations make that realistic."
)

new_location = (
    old_location +
    " Location is background context, not a topic you should keep bringing up. "
    "Mention locations only when they are directly relevant to the current message or needed to avoid a false claim "
    "of physical proximity. If a player says location details are unnecessary, stop emphasizing them and do not "
    "lecture players about sharing or explaining their location."
)

old_correction = (
    "if a human rejects or corrects something you said earlier, drop that assumption immediately."
)

new_correction = (
    old_correction +
    " If a player asks you to stop doing something, acknowledge it and stop that behavior instead of arguing, "
    "lecturing, becoming defensive, or treating the request as an attack. Current game state and explicit human "
    "corrections override your own earlier statements and assumptions."
)

text = CONF.read_text(encoding="utf-8")

for old, label in [
    (old_location, "location rule"),
    (old_correction, "correction rule"),
]:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"ABORT: expected exactly one {label}, found {count}")

new_text = text.replace(old_location, new_location, 1)
new_text = new_text.replace(old_correction, new_correction, 1)

stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
backup = CONF.with_name(CONF.name + f".bak_prompt_{stamp}")
shutil.copy2(CONF, backup)

CONF.write_text(new_text, encoding="utf-8")

print("Updated:", CONF)
print("Backup :", backup)
