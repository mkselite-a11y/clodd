#!/bin/sh
# Builds both versions (Global Events Mod and Global Events Mod + Cheats), the DLL
# and the relay, into dist/.
#   RECOMP_MOD_TOOL: path to N64Recomp's RecompModTool (default: on PATH)
#   DLL_CC: a Windows cross compiler (default: x86_64-w64-mingw32-gcc)
set -e
cd "$(dirname "$0")"
TOOL="${RECOMP_MOD_TOOL:-RecompModTool}"
CC_WIN="${DLL_CC:-x86_64-w64-mingw32-gcc}"
mkdir -p dist
rm -rf build
make EXTRA_DEFS=-DGE_CHEATS=1 >/dev/null
"$TOOL" mod_cheats.toml build
cp build/CUSTOM_global_events_cheats_v3.nrm dist/
rm -rf build
make >/dev/null
"$TOOL" mod.toml build
cp build/CUSTOM_global_events_v3.nrm dist/
"$CC_WIN" -O2 -Wall -Wno-stringop-truncation -shared -o remote/native/GlobalEventsRemote.dll remote/native/ge_remote.c -lwinhttp -s
cp remote/native/GlobalEventsRemote.dll dist/
(cd remote/relay && python3 sync_tables.py >/dev/null && python3 build_relay.py >/dev/null)
cp remote/relay/worker.js dist/
ls -la dist
