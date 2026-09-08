#!/usr/bin/env python3
"""
Patcht de live aiplayerbot.conf op de server (LLM prompt/response-parsing) en
maakt llm_character_card.txt aan.

- ALLE exact-string-match-validatie gebeurt eerst, voor er iets aangemaakt of
  weggeschreven wordt. Als een old-string niet EXACT 1x gevonden wordt, stopt
  het script met een foutmelding en wijzigt/schrijft het NIETS.
- Backups krijgen een tijdstip in de bestandsnaam (nooit een vaste naam), dus
  een backup wordt nooit overschreven -- ook niet bij een tweede run nadat de
  eerste run al is toegepast (die zou dan sowieso al bij de validatiestap
  stoppen, voor er een nieuwe backup gemaakt wordt).
- Als een verwacht backup-pad onverhoopt toch al bestaat, stopt het script
  in plaats van het te overschrijven.

Gebruik:
    python3 patch_llm_prompt_config.py
"""

import os
import shutil
import sys
from datetime import datetime

CONF_PATH = "/home/hans/tortoise-server-asan/etc/aiplayerbot.conf"
# mangosd draait met working directory .../bin (bevestigd via
# readlink -f /proc/$(pgrep -f mangosd)/cwd), en LLMDefaultPromptsFile wordt
# als relatief pad geladen -- dus dit bestand moet in bin/ staan, niet in etc/.
CARD_PATH = "/home/hans/tortoise-server-asan/bin/llm_character_card.txt"

# ---------------------------------------------------------------------------
# 1. aiplayerbot.conf edits
# ---------------------------------------------------------------------------

EDITS = [
    (
        'AiPlayerbot.LLMApiJson = {"model":"qwen3.5:9b","messages":[{"role":"system","content":"<pre prompt> <context>"},{"role":"user","content":"<prompt>"}],"think":false,"stream":false,"options":{"num_predict":60}}',
        'AiPlayerbot.LLMApiJson = {"model":"qwen3.5:9b","messages":[{"role":"system","content":"<pre prompt> <context>"},{"role":"user","content":"<prompt>"}],"think":false,"stream":false,"options":{"num_predict":60,"temperature":0.45,"top_p":0.9,"top_k":40,"repeat_penalty":1.1,"repeat_last_n":128}}',
    ),
    (
        "AiPlayerbot.LLMBotToBotChatChance = 0\nAiPlayerbot.LLMRpgAIChatChance = 0",
        "AiPlayerbot.LLMBotToBotChatChance = 0\nAiPlayerbot.LLMRpgAIChatChance = 0\n\n"
        "AiPlayerbot.LLMPrePrompt = You are <bot name>, a <bot gender> <bot race> <bot class> "
        "(level <bot level>) in World of Warcraft: <expansion name>, currently in <bot subzone>, "
        "<bot zone>. <other name> is speaking to you <channel name>. Answer the current message "
        "directly and coherently first; normally give one short reply, not several. Do not repeat "
        "or paraphrase the question before answering. Do not invent relationships (daughter, "
        "mother, sister, friend, enemy, lover, etc.) between players unless the humans themselves "
        "explicitly established it in this conversation; if a human rejects or corrects something "
        "you said earlier, drop that assumption immediately. Jokes from players are not facts; do "
        "not invent personal history or background for human players. Your own earlier statements "
        "in this conversation may have been mistaken, playful, or speculative -- do not treat them "
        "as established facts. Keep each speaker's identity separate. Reply in the same language "
        "the current speaker used. Use simple natural language; do not invent fantasy-sounding "
        "words. Official WoW class names (warlock, hunter, rogue, mage, priest, warrior, paladin, "
        "druid, shaman) must always stay in English exactly as given, even in a Dutch reply -- "
        "never translate them. Only occasionally mention your class, race, zone or other flavor, "
        "and only when relevant. Avoid dramatic prose or *action*-style text unless the "
        "conversation calls for it. If uncertain, give a normal neutral answer instead of "
        "inventing something.",
    ),
    (
        " AiPlayerbot.LLMResponseSplitPattern = (\\*.*?\\*)|(\\[.*?\\])|(\\'.*\\')|([^\\*\\[\\] ][^\\*\\[\\]]+?[.?!])",
        "AiPlayerbot.LLMResponseSplitPattern =",
    ),
]

# LLMResponseEndPattern en LLMResponseStartPattern blijven INGESCHAKELD/onveranderd.
# ("$) bleek fout: extractBeforePattern() (PlayerbotLLMInterface.cpp) gebruikt
# regex_search zonder anker op de HELE resterende body na de start-match, en "$"
# matcht alleen het allerlaatste teken van die body -- na Ollama's
# "content":\s*" volgt nog },"done":true,...} , dus die "$" zou nooit matchen
# en de hele JSON-staart zou in de WoW-chat belanden. De huidige
# EndPattern ("|\b(?!<sender name>\b)(\w+):) matcht gewoon het eerste kale "
# -- bewezen werkend met de live Ollama-response, dus met rust gelaten.

# ---------------------------------------------------------------------------
# 2. llm_character_card.txt content
#    (Alleen Ravanne is hier concreet ingevuld -- haar klasse (warlock) staat
#    vast op basis van de echte chatlogs. Vul de rest zelf aan / vraag Hans
#    welke bots een kaart nodig hebben; de resterende bots vallen anders
#    gewoon terug op de generieke LLMPrePrompt hierboven, wat ook prima is.)
# ---------------------------------------------------------------------------

CHARACTER_CARD = (
    "Ravanne::Warlock, praktisch en droog. Noemt haar demonen af en toe, maar niet in elke zin. "
    "Blijft kalm en direct, geen dramatiek.\n"
)


def timestamped_backup_path(path):
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = f"{path}.bak_llm_prompt_quality_{ts}"
    if os.path.exists(backup_path):
        print(f"ABORT: backup-pad bestaat al onverwacht: {backup_path}")
        print("Niets is aangeraakt. Draai het script opnieuw (andere seconde) of ruim dit pad op.")
        sys.exit(1)
    return backup_path


def validate_conf_edits(content):
    for i, (old, new) in enumerate(EDITS, start=1):
        count = content.count(old)
        if count != 1:
            print(f"ABORT: edit {i}/{len(EDITS)} - old-string {count}x gevonden (verwacht 1x).")
            print("Niets is aangeraakt (geen backup, geen schrijfactie). Deze old-string kon niet exact gematcht worden:")
            print(repr(old[:200]))
            sys.exit(1)
    print(f"Validatie OK: alle {len(EDITS)} old-strings exact 1x gevonden in {CONF_PATH}.")


def patch_conf():
    with open(CONF_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. Eerst ALLE validatie -- pas als dit volledig slaagt, gaan we verder.
    validate_conf_edits(content)

    # 2. Pas na succesvolle validatie: backup maken (unieke, tijdgestempelde naam).
    backup_path = timestamped_backup_path(CONF_PATH)
    shutil.copyfile(CONF_PATH, backup_path)
    print(f"Backup geschreven: {backup_path}")

    # 3. Nu pas de wijzigingen toepassen en wegschrijven.
    for i, (old, new) in enumerate(EDITS, start=1):
        content = content.replace(old, new)
        print(f"OK: edit {i}/{len(EDITS)} toegepast.")

    with open(CONF_PATH, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"Klaar: {CONF_PATH} bijgewerkt ({len(EDITS)} wijzigingen).")


def write_character_card():
    if os.path.exists(CARD_PATH):
        backup_path = timestamped_backup_path(CARD_PATH)
        shutil.copyfile(CARD_PATH, backup_path)
        print(f"Bestaand bestand gevonden, backup geschreven: {backup_path}")

    with open(CARD_PATH, "w", encoding="utf-8") as f:
        f.write(CHARACTER_CARD)
    print(f"Geschreven: {CARD_PATH}")


if __name__ == "__main__":
    patch_conf()
    write_character_card()
    print("\nKlaar. Herstart mangosd (of herlaad de config, indien de server dat ondersteunt) "
          "om de wijzigingen actief te maken.")

