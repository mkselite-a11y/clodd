#!/bin/sh
# Builds both versions: Global Events Mod and Global Events Mod + Cheats.
set -e
cd /root/RainbowCarpenter
mkdir -p dist
rm -rf /root/RainbowCarpenter/build
make EXTRA_DEFS=-DGE_CHEATS=1 >/dev/null
/root/N64Recomp/build/RecompModTool mod_cheats.toml build
cp build/CUSTOM_global_events_cheats_v2.nrm dist/
rm -rf /root/RainbowCarpenter/build
make >/dev/null
/root/N64Recomp/build/RecompModTool mod.toml build
cp build/CUSTOM_global_events_v2.nrm dist/
ls -la dist
