#!/bin/bash
set -euo pipefail

SRC="/home/hans/tortoise-server-asan/etc"
DST="/home/hans/tortoise-wow/runtime-configs"

mkdir -p "$DST"

echo "Runtime configs synchroniseren..."

cp -av "$SRC/aiplayerbot.conf"      "$DST/aiplayerbot.conf"
cp -av "$SRC/aiplayerbot.conf.dist" "$DST/aiplayerbot.conf.dist"

cp -av "/home/hans/tortoise-server-asan/bin/llm_character_card.txt" \
       "/home/hans/tortoise-wow/runtime-configs/llm_character_card.txt"



echo
echo "Klaar:"
ls -lh "$DST/aiplayerbot.conf" "$DST/aiplayerbot.conf.dist"
