import re

def patch_file(path, pattern, repl, label):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    matches = list(re.finditer(pattern, content))
    if len(matches) != 1:
        print(f"SKIP {label}: {len(matches)} matches (expected 1) in {path}")
        return
    content = re.sub(pattern, repl, content, count=1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"OK {label} in {path}")

cpp = "modules/mod-playerbots/src/playerbot/PlayerbotAI.cpp"
h   = "modules/mod-playerbots/src/playerbot/PlayerbotAI.h"

patch_file(
    cpp,
    r'(void PlayerbotAI::UpdateAI\(uint32 elapsed, bool minimal\)\n\{\n)([ \t]*)(AiObjectContext\* context = aiObjectContext;)',
    r'\1\2std::lock_guard<std::mutex> updateLock(aiUpdateMutex);\n\n\2\3',
    "aiUpdateMutex lock_guard in UpdateAI()",
)

patch_file(
    h,
    r'([ \t]*std::mutex chatRepliesMutex;\n)([ \t]*)(PacketHandlingHelper botOutgoingPacketHandlers;)',
    r'\1\2std::mutex aiUpdateMutex;\n\2\3',
    "aiUpdateMutex member declaration",
)
