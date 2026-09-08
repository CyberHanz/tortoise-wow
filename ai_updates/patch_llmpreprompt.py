#!/usr/bin/env python3
"""
patch_llmpreprompt.py -- targeted, anchor-based patch for AiPlayerbot.LLMPrePrompt
in a live aiplayerbot.conf, implementing revision items T1-T7 of the approved
PlayerBots LLM chat-quality proposal.

WHAT THIS DOES
  - Backs up the whole config file (timestamped copy) before touching anything.
  - Finds the single active "AiPlayerbot.LLMPrePrompt = ..." line.
  - Applies up to seven small, independent textual transformations (T1-T7) to
    THAT LINE'S VALUE ONLY. Every other line in the file, including
    "AiPlayerbot.LLMDefaultPromptsFile = llm_character_card.txt" and its
    [AiPlayerbotConf] section placement, is left completely untouched --
    the script never even reads those lines' content, only copies them through
    verbatim.
      T1: identity/zone/life-state sentence cluster -> authoritative identity block
      T2: togetherness/proximity sentence -> conditional location-context semantics
      T3-T5: additive rules (non-disclosure compliance, residence, hypothetical dest.)
      T6: raw <bot zone>/<bot subzone>/<other zone>/<other subzone> placeholder
          sentence -> generic "authoritative game context" phrasing (these raw
          placeholders would otherwise be substituted -- and therefore leak current
          zone/subzone -- on every turn, even when <location context> is empty)
      T7: removes the hardcoded "Being in Sentinel Hill does not mean you live in
          Sentinel Hill" example sentence entirely (superseded by the generic
          current-location != home/residence/birthplace rule from T4)
  - Each transformation looks for an literal anchor substring before changing
    anything. If an anchor is not found AND the transformation does not look
    already-applied (idempotency marker), the script ABORTS with no write at
    all and prints the current live value so a human can decide what to do.
  - Re-running the script on an already-patched file is a no-op for whatever
    parts were already applied (checked via marker substrings), and only adds
    whatever is still missing.
  - After all transformations, two final safety validations run on the
    resulting value regardless of which individual transforms fired:
      (a) none of the raw placeholders <bot zone>, <bot subzone>, <other zone>,
          <other subzone> may remain anywhere in the value (<location context>
          itself is fine -- it's the one allowed location-carrying token);
      (b) the literal text "Sentinel Hill" may not remain anywhere in the value.
    Either failing aborts with no write and reports exactly which token(s) or
    text were found.

WHAT THIS DOES NOT DO
  - Does not touch WorldSession, routing, chat command queueing, participation/
    interjection selection, Ollama transport, JSON/parser code, hunter quest
    code, persistent memory, or LLMDefaultPromptsFile placement.
  - Does not run mangosd, does not compile anything.
  - Does not invent or guess text for spans it cannot find -- it only ever
    replaces exact anchor substrings supplied below, and only ever appends
    exact new sentences supplied below.

USAGE
  python3 patch_llmpreprompt.py /path/to/aiplayerbot.conf           # dry run (default)
  python3 patch_llmpreprompt.py /path/to/aiplayerbot.conf --apply  # write for real

Review the dry-run output before passing --apply.
"""

import argparse
import datetime
import re
import sys


CONFIG_KEY = "AiPlayerbot.LLMPrePrompt"

# --- T1: identity / zone / life-state sentence cluster -----------------------
# Anchor is the phrasing we last saw in our source-side mirror. Your live text
# may already differ (that's the whole reason this is anchor-checked rather
# than a blind whole-line replace).
T1_ANCHOR = (
    "You are <bot name>, a <bot gender> <bot race> <bot class> (level <bot level>) "
    "in World of Warcraft: <expansion name>. You are currently in <bot subzone>, <bot zone>. "
    "<other name> is currently in <other subzone>, <other zone> and is speaking to you <channel name>. "
    "You are currently <bot life state>; <other name> is currently <other life state>."
)
T1_NEW = (
    "You are <bot name>, a level <bot level> <bot gender> <bot class> in World of Warcraft: "
    "<expansion name>, speaking to <other name> <channel name>. <authoritative identity context> "
    "These are fixed authoritative facts about each of you -- never infer a different race, class, "
    "faction, or life-state from story, mood, class, wording, or earlier conversation, and never "
    "contradict the supplied values. Official race/class/faction names must retain their supplied "
    "game names; life-state may be phrased naturally but its meaning must not change."
)
# Presence of this token means T1 has already been applied.
T1_MARKER = "<authoritative identity context>"

# --- T2: togetherness / proximity sentence(s) --------------------------------
T2_ANCHOR = (
    "Party and raid chat can reach characters anywhere in the world and does not mean you are "
    "physically together. Never claim that you are standing together, waiting nearby, hunting "
    "together, protecting someone nearby, or travelling together unless the stated locations make "
    "that realistic."
)
T2_NEW = (
    "<location context> Never assume physical proximity, shared travel, a shared route, or shared "
    "combat between speakers -- chat of any kind (party, raid, guild, world, whisper) can reach "
    "characters anywhere and does not imply they are near each other. If location context was given "
    "above, treat it as the only authoritative statement of where either of you is; it overrides "
    "anything said about location earlier in this conversation. If none was given, you do not know "
    "where either of you is relative to the other -- do not guess, and do not describe standing "
    "together, waiting nearby, someone coming to collect you, being visible to each other, hunting "
    "together, protecting someone nearby, or travelling onward together. Older location statements "
    "in this conversation may be stale; without a current location context above, do not treat them "
    "as your or the other person's current position."
)
T2_MARKER = "<location context>"

# --- T3, T4, T5: additive sentences, each independently idempotent ----------
# Inserted (in this order) immediately after the T2 span, only if their marker
# is not already present anywhere in the value.
ADDITIVE_RULES = [
    (
        "comply silently",  # marker substring
        "If asked not to reveal your current location, comply silently -- do not mention that you "
        "are withholding it, simply answer the rest of the question.",
    ),
    (
        "never your home, residence",  # marker substring
        "Current location is only where you are right now, never your home, residence, birthplace, "
        "or permanent base unless the conversation has explicitly established that. Wanting to "
        "visit, see, or someday go to a place is a preference, not a plan to move there, live there, "
        "or make it your home.",
    ),
    (
        "hypothetical/someday destination",  # marker substring
        "A question about your favorite place or a hypothetical/someday destination asks for your "
        "opinion, not your current position -- answer it directly, preferring a real known place if "
        "you name one, without inventing prior visits, activities, dangers, or reasons tied to "
        "demons, quests, or lore that were not already established in this conversation.",
    ),
]

# --- T6: raw location-placeholder sentence -----------------------------------
# This sentence substitutes <bot zone>/<bot subzone>/<other zone>/<other subzone>
# directly into the prompt on every turn -- a leak independent of, and in
# addition to, the conditional <location context> mechanism from T2.
T6_ANCHOR = (
    "When referring to a location from <bot zone>, <bot subzone>, <other zone>, or "
    "<other subzone>, copy that name exactly as given."
)
T6_NEW = (
    "When referring to a location supplied by authoritative game context, copy "
    "that official name exactly as given."
)
# No broad marker substring for T6 -- "already applied" is checked by looking for
# the exact T6_NEW sentence itself (see apply_transformations), since a fragment
# like "supplied by authoritative game context" can already occur elsewhere in a
# live prompt and would falsely report "already applied" while the old raw-
# placeholder sentence is still present.

# --- T7: hardcoded Sentinel Hill example sentence -- removed, not replaced ---
T7_ANCHOR = "Being in Sentinel Hill does not mean you live in Sentinel Hill."

# Tokens/text that must never remain in the final value, checked after every
# transformation attempt regardless of which ones fired. <location context> is
# deliberately not in this list -- it is the one allowed location-carrying token.
LEAK_PLACEHOLDER_TOKENS = ["<bot zone>", "<bot subzone>", "<other zone>", "<other subzone>"]
SENTINEL_HILL_LITERAL = "Sentinel Hill"

# Lines this script must never inspect the content of, let alone touch, beyond
# copying them through unchanged. Purely a belt-and-suspenders guard used to
# fail loudly if something unexpected is going on with the file.
PROTECTED_KEY = "AiPlayerbot.LLMDefaultPromptsFile"


def find_active_line(lines, key):
    """Return (index, value) of the single active 'key = value' line, or (None, None)."""
    matches = []
    pattern = re.compile(r"^\s*" + re.escape(key) + r"\s*=\s*(.*)$")
    for i, line in enumerate(lines):
        if line.lstrip().startswith("#"):
            continue
        m = pattern.match(line.rstrip("\n"))
        if m:
            matches.append((i, m.group(1)))
    if len(matches) == 1:
        return matches[0]
    return None, None


def apply_transformations(value):
    """Return (new_value, applied_list, warnings_list) -- never raises; caller decides
    whether to abort based on the returned state."""
    applied = []
    warnings = []

    # T1
    if T1_MARKER in value:
        applied.append("T1: already applied (marker present) -- skipped")
    elif T1_ANCHOR in value:
        value = value.replace(T1_ANCHOR, T1_NEW, 1)
        applied.append("T1: anchor found -- replaced")
    else:
        warnings.append(
            "T1: anchor not found and marker not present -- identity/zone/life-state span "
            "could not be safely located. NO CHANGE MADE for T1."
        )

    # T2
    if T2_MARKER in value:
        applied.append("T2: already applied (marker present) -- skipped")
    elif T2_ANCHOR in value:
        value = value.replace(T2_ANCHOR, T2_NEW, 1)
        applied.append("T2: anchor found -- replaced")
    else:
        warnings.append(
            "T2: anchor not found and marker not present -- togetherness/proximity span "
            "could not be safely located. NO CHANGE MADE for T2."
        )

    # T3-T5: only insertable at a deterministic point -- immediately after the exact
    # T2_NEW string. No heuristic fallback (e.g. searching for "current position.")
    # -- if <location context> is present but T2_NEW is not found verbatim, this is
    # an unknown state and must not be guessed at.
    if T2_MARKER in value:
        missing_rules = [(marker, sentence) for marker, sentence in ADDITIVE_RULES if marker not in value]
        if not missing_rules:
            applied.append("additive rules: all already present -- skipped")
        elif T2_NEW in value:
            split_at = value.index(T2_NEW) + len(T2_NEW)
            before, after = value[:split_at], value[split_at:]
            for marker, sentence in ADDITIVE_RULES:
                if marker in value:
                    applied.append(f"additive rule '{marker[:30]}...': already applied -- skipped")
                    continue
                before = before + " " + sentence
                applied.append(f"additive rule '{marker[:30]}...': inserted")
            value = before + after
        else:
            warnings.append(
                "T3-T5 additive rules: <location context> marker is present but the exact "
                "T2_NEW text is not -- insertion point cannot be determined safely. NO CHANGE "
                "MADE for T3-T5. Apply the missing rule(s) by hand, or update T2_ANCHOR/T2_NEW "
                "in this script to match the live wording and re-run."
            )
    else:
        warnings.append(
            "T3-T5 additive rules skipped: no <location context> marker present yet "
            "(T2 must succeed first, in this run or a previous one)."
        )

    # T6: raw location-placeholder sentence -> generic "authoritative game context"
    if T6_NEW in value:
        applied.append("T6: already applied (exact T6_NEW sentence present) -- skipped")
    elif T6_ANCHOR in value:
        value = value.replace(T6_ANCHOR, T6_NEW, 1)
        applied.append("T6: anchor found -- replaced")
    else:
        warnings.append(
            "T6: neither the exact T6_NEW sentence nor T6_ANCHOR was found -- raw "
            "location-placeholder sentence could not be safely located. NO CHANGE MADE for T6."
        )

    # T7: remove the hardcoded Sentinel Hill example sentence entirely. Removes the
    # trailing space along with the sentence when one follows it (so no double-space
    # is left behind), falling back to removing just the sentence itself when it's
    # the last thing in the value. Purely local to this one sentence -- the rest of
    # the prompt is left byte-for-byte alone.
    if T7_ANCHOR in value:
        value = value.replace(T7_ANCHOR + " ", "", 1)
        if T7_ANCHOR in value:
            value = value.replace(T7_ANCHOR, "", 1)
        applied.append("T7: anchor found -- removed")
    elif SENTINEL_HILL_LITERAL not in value:
        applied.append("T7: already absent -- skipped")
    else:
        warnings.append(
            "T7: literal 'Sentinel Hill' text is present but not in the expected anchor "
            "sentence -- cannot safely remove. NO CHANGE MADE for T7."
        )

    # --- Final safety validations -- run regardless of which transforms fired ---
    offending_tokens = [t for t in LEAK_PLACEHOLDER_TOKENS if t in value]
    if offending_tokens:
        warnings.append(
            "SAFETY: resulting LLMPrePrompt still contains raw location placeholder(s): "
            + ", ".join(offending_tokens) +
            ". <location context> is the only location-carrying token allowed. "
            "ABORTING -- no file will be written."
        )

    if SENTINEL_HILL_LITERAL in value:
        warnings.append(
            "SAFETY: resulting LLMPrePrompt still contains the literal text "
            "'Sentinel Hill'. ABORTING -- no file will be written."
        )

    return value, applied, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("config_path", help="Path to the live aiplayerbot.conf")
    ap.add_argument("--apply", action="store_true", help="Write changes. Without this flag, dry-run only.")
    args = ap.parse_args()

    with open(args.config_path, "r", encoding="utf-8") as f:
        original_text = f.read()
    lines = original_text.splitlines(keepends=True)

    idx, current_value = find_active_line(lines, CONFIG_KEY)
    if idx is None:
        print(f"ABORT: could not find exactly one active '{CONFIG_KEY} = ...' line. "
              f"No changes made.", file=sys.stderr)
        sys.exit(1)

    # Sanity guard: confirm the protected key still exists somewhere, unmodified by us.
    # (We never touch it -- this just proves it was present before we wrote anything,
    # so a human can compare after the fact if desired.)
    protected_present = any(
        not l.lstrip().startswith("#") and l.lstrip().startswith(PROTECTED_KEY) for l in lines
    )
    if not protected_present:
        print(f"WARNING: '{PROTECTED_KEY}' line not found active in this file at all. "
              f"This script does not touch it either way, but flagging since you said it "
              f"must remain under [AiPlayerbotConf].", file=sys.stderr)

    new_value, applied, warnings = apply_transformations(current_value)

    print("=== Current AiPlayerbot.LLMPrePrompt (line %d) ===" % (idx + 1))
    print(current_value)
    print()
    print("=== Proposed AiPlayerbot.LLMPrePrompt ===")
    print(new_value)
    print()
    print("=== Actions ===")
    for a in applied:
        print(" -", a)
    for w in warnings:
        print(" ! ", w)

    if warnings:
        print()
        print("ABORT: one or more anchors could not be safely located (see '!' lines above). "
              "No file was written. Inspect the current live value printed above and adjust "
              "T1_ANCHOR/T2_ANCHOR in this script to match, or apply that specific span by hand.",
              file=sys.stderr)
        sys.exit(2)

    if new_value == current_value:
        print()
        print("Nothing to do -- value already matches the fully-patched form. No file written.")
        sys.exit(0)

    if not args.apply:
        print()
        print("Dry run only (no --apply given). No file written.")
        sys.exit(0)

    # Timestamped backup before writing.
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_path = f"{args.config_path}.bak-{ts}"
    with open(backup_path, "w", encoding="utf-8") as f:
        f.write(original_text)
    print(f"Backup written: {backup_path}")

    new_line_text = f"{CONFIG_KEY} = {new_value}"
    if not lines[idx].endswith("\n"):
        # preserve lack of trailing newline on the very last line of the file, if applicable
        pass
    else:
        new_line_text += "\n"
    lines[idx] = new_line_text

    with open(args.config_path, "w", encoding="utf-8") as f:
        f.write("".join(lines))

    print(f"Patched: {args.config_path}")


if __name__ == "__main__":
    main()
