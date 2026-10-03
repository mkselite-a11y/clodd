"""Rebuilds tables.json from the mod's C source, so the panel always matches the game.

Run from remote/relay:  python3 sync_tables.py && python3 build_relay.py
Sounds are kept from the existing tables.json (they're short and
hand-written); curse descriptions too, minus the curses the game retired.
"""
import json
import re

SRC = "../../src/"
common = open(SRC + "common.h").read()
events_c = open(SRC + "events.c").read()
v11 = open(SRC + "ev_v11.inc").read()
v2 = open(SRC + "ev_v2.inc").read()
v3 = open(SRC + "ev_v3.inc").read()
old = json.load(open("tables.json"))


def body(src, signature):
    """The text of a C function, from its signature to the closing brace at column 0."""
    i = src.index(signature)
    j = src.index("\n}\n", i)
    return src[i:j]


def strings(block):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', block)


def switch_map(block):
    return dict(re.findall(r'case (\w+): return "((?:[^"\\]|\\.)*)";', block))


def cases_returning(block, value):
    """Labels of a switch whose (fall-through) group returns `value`."""
    out, pending = set(), []
    for line in block.splitlines():
        m = re.match(r"\s*case (\w+):", line)
        if m:
            pending.append(m.group(1))
        elif "return" in line:
            if line.strip().startswith("return " + value + ";"):
                out.update(pending)
            pending = []
    return out


# --- events ---------------------------------------------------------------------
enum = re.search(r"typedef enum \{(.*?)EV_COUNT", common, re.S).group(1)
ev_ids = [n for n in re.findall(r"^\s*(EV_\w+)", enum, re.M)]
EV = {name: i for i, name in enumerate(ev_ids)}
names = switch_map(body(events_c, "const char* Events_Name(s32 ev)"))
descs = switch_map(body(v11, "static const char* Events_Description(s32 ev)"))
hidden = set(re.findall(r"\(ev == (EV_\w+)\)", body(events_c, "s32 Events_IsHidden(s32 ev)")))
tier_block = body(v2, "static s32 Ev_Tier(s32 ev)")
tier_of = {}
for t in range(4):
    for n in cases_returning(tier_block, str(t)):
        tier_of[n] = t
prim_block = body(v11, "static s32 Combo_CanBePrimary(s32 ev)")
not_primary = cases_returning(prim_block, "false")
second = cases_returning(body(v11, "static s32 Combo_CanBeSecond(s32 ev)"), "true")
# Events a friend can double (X combo X): Ev_CanDouble in events.c.
doubles = cases_returning(body(events_c, "static s32 Ev_CanDouble(s32 ev)"), "true")
v3defs = re.findall(r'\{ "([^"]+)", "([^"]+)", (\d), \{', body(v3, "static const V3Def sV3Defs[]"))
first_v3 = EV["EV_STARFALL"]

events = []
for name, i in EV.items():
    if name in hidden:
        continue
    if i >= first_v3:
        n, d, t = v3defs[i - first_v3]
        events.append({"id": i, "name": n, "desc": d, "second": True, "primary": False, "tier": int(t),
                       "double": name in doubles})
        continue
    events.append({
        "id": i, "name": names[name], "desc": descs.get(name, ""),
        "second": True, "primary": name not in not_primary,  # friend combos: any event can ride along
        "tier": tier_of.get(name, 1),
        "double": name in doubles,  # a friend can pair it with itself (runs once, twice as fierce)
    })

# Experimental friends get every event, hidden ones included.
allevents = []
removed = {"EV_BULLETHELL", "EV_INFIGHT", "EV_LEVIATHAN"}  # gone for good, even for Experimental friends
for name, i in EV.items():
    if name in removed:
        continue
    if i >= first_v3:
        allevents.append({"id": i, "name": v3defs[i - first_v3][0] + (" (hidden)" if name in hidden else "")})
    else:
        allevents.append({"id": i, "name": names.get(name, name) + (" (hidden)" if name in hidden else "")})

# --- traits, mutators, pouch ---------------------------------------------------------
def c_array(src, decl):
    i = src.index(decl)
    return strings(src[i:src.index("};", i)])

trait_names = c_array(v2, "static const char* sTraitNames[TR_COUNT]")
trait_descs = c_array(v2, "static const char* sTraitDescs[TR_COUNT]")
traits = [{"id": i, "name": n, "desc": d} for i, (n, d) in enumerate(zip(trait_names, trait_descs)) if n != "(retired)"]

mut_enum = re.search(r"typedef enum \{([^}]*?MUT_COUNT)", common + v2 + events_c, re.S)
mut_ids = re.findall(r"(MUT_\w+)", mut_enum.group(1)) if mut_enum else []
retired_names = set(re.findall(r"\(m == (MUT_\w+)\)", body(v2, "static s32 Mut_Retired(s32 m)")))
retired = {mut_ids.index(n) for n in retired_names if n in mut_ids}
mut_names = c_array(v2, "static const char* sMutNames[MUT_COUNT]")
mut_descs = c_array(v2, "static const char* sMutDescs[MUT_COUNT]")
muts = [{"id": i, "name": n, "desc": d} for i, (n, d) in enumerate(zip(mut_names, mut_descs))
        if i not in retired and n != "(retired)"]

pouch_names = c_array(v2, "static const char* sPouchNames[PI_COUNT]")
pouch_descs = c_array(v2, "static const char* sPouchDescs[PI_COUNT]")
pouch = [{"id": i, "name": n, "desc": d} for i, (n, d) in enumerate(zip(pouch_names, pouch_descs))]

# --- curses: drop the ones the game retired -------------------------------------
can_use = body(v11, "static s32 Curse_CanUse(s32 curse) {")
retired_curses = cases_returning(can_use, "false")
curse_enum = re.search(r"typedef enum \{([^}]*?CURSE_COUNT)", common + events_c + v11, re.S).group(1)
curse_ids = re.findall(r"(CURSE_\w+)", curse_enum)
dead = {curse_ids.index(n) for n in retired_curses if n in curse_ids}
describe = body(v11, "static void Curse_DescribeId(s32 curse, char* out, s32 size)")
curse_desc = dict(re.findall(r'case (CURSE_\w+):\s*Ev_Append\(out, 0, "((?:[^"\\]|\\.)*)"', describe))
curses = []
for c in old["curses"]:
    if c["id"] in dead:
        continue
    d = curse_desc.get(curse_ids[c["id"]])
    curses.append(dict(c, desc=d) if d else c)

# --- enemies that can be bounties / Nemeses -------------------------------------------
hunt = [int(x) for x in re.search(r"sHuntPool\[\] = \{([^}]*)\}", v2).group(1).split(",")]
pool_names = c_array(events_c, "const char* Pool_Name(s32 index)")
# Tiers come straight from the game's Pool_Tier (default 1).
pool_tier_block = body(v2, "static s32 Pool_Tier(s32 pool)")
pool_tier = {}
for t in range(4):
    for n in cases_returning(pool_tier_block, str(t)):
        pool_tier[int(n)] = t
pools = [{"id": i, "name": pool_names[i], "tier": pool_tier.get(i, 1), "desc": next((p.get("desc", "") for p in old["pools"] if p["id"] == i), "")} for i in hunt]

# --- zones: the game's 77 areas, drawn as a map of the outdoor ones ---------------------
LAYOUT = {  # area: x, y on a 480 x 430 map, short label
    0: (240, 222, "S. Town"), 1: (276, 200, "E. Town"), 2: (204, 200, "W. Town"), 3: (240, 178, "N. Town"),
    4: (280, 228, "Laundry"), 5: (240, 200, "Termina Field"), 6: (240, 292, "Swamp Road"),
    7: (240, 335, "Southern Swamp"), 8: (165, 362, "Deku Palace"), 9: (240, 382, "Woodfall"),
    10: (315, 362, "Woods of Mystery"), 11: (240, 100, "Mountain Path"), 12: (240, 70, "Mountain Village"),
    13: (160, 48, "Twin Islands"), 14: (80, 28, "Goron Village"), 15: (320, 48, "Snowhead Path"),
    16: (400, 26, "Snowhead"), 17: (340, 272, "Milk Road"), 18: (405, 320, "Romani Ranch"),
    19: (92, 212, "Great Bay Coast"), 20: (52, 285, "Zora Cape"), 21: (52, 140, "Pirates' Fortress"),
    22: (60, 345, "Zora Hall"), 23: (380, 200, "Road to Ikana"), 24: (430, 252, "Graveyard"),
    25: (432, 158, "Ikana Canyon"), 26: (445, 105, "Stone Tower"),
    # The four temples hang off their areas on the map.
    49: (300, 408, "WF Temple"), 55: (455, 58, "SH Temple"), 65: (118, 302, "GB Temple"), 76: (385, 100, "ST Temple"),
}
zone_block = v2[v2.index("static const ZoneDef sZones[] = {"):v2.index("#undef ZL")]
zones = []
for m in re.finditer(r'/\*\s*(\d+) \*/ \{ "([^"]+)", \{ ([^}]*) \}, (-?\d+), ZL\(([^)]*)\) \}', zone_block):
    zid, name, scenes, parent, links = int(m.group(1)), m.group(2), m.group(3), int(m.group(4)), m.group(5)
    z = {"id": zid, "name": name, "parent": parent,
         "links": [int(x) for x in links.split(",") if int(x) >= 0],
         "scenes": [s.strip() for s in scenes.split(",") if s.strip() != "-1"]}
    if zid in LAYOUT:
        z["x"], z["y"], z["short"] = LAYOUT[zid]
    zones.append(z)
assert len(zones) == 77, len(zones)

# Scene ids (the status line sends play->sceneId) from the decomp's scene table order.
try:
    table = open("../../mm-decomp/include/tables/scene_table.h").read()
    sid = {}
    for idx, args in re.findall(r"/\* (0x[0-9A-Fa-f]+) \*/ DEFINE_SCENE\w*\(([^)]*)", table):
        for a in args.split(","):
            if a.strip().startswith("SCENE_"):
                sid[a.strip()] = int(idx, 16)
    for z in zones:
        z["scenes"] = [sid[s] for s in z["scenes"] if s in sid]
except FileNotFoundError:
    pass

# For the DLL: where every scene's file sits in the ROM (its dmadata index), so the
# map builder covers all of them (indoors, grottos, dungeons and boss rooms too).
# The _SYMS entries are real (empty) rows of the ROM's file table, so they count.
try:
    table = open("../../mm-decomp/include/tables/scene_table.h").read()
    dma_src = open("../../mm-decomp/include/tables/dmadata/dmadata_table_us.h").read()
    dma = re.findall(r"^DEFINE_DMA_ENTRY(?:_SYMS)?\((\w+),", dma_src, re.M)
    dma_idx = {n: i for i, n in enumerate(dma)}
    rows = []
    for idx, args in re.findall(r"/\* (0x[0-9A-Fa-f]+) \*/ DEFINE_SCENE\(([^)]*)", table):
        parts = [a.strip() for a in args.split(",")]
        if parts[0] in dma_idx:
            rows.append((int(idx, 16), dma_idx[parts[0]], parts[1]))
    with open("../native/ge_scenes.h", "w") as h:
        h.write("// Generated by remote/relay/sync_tables.py: scene id -> dmadata index of its scene file (US).\n")
        h.write("// Every scene in the game's scene table that has a scene file.\n")
        h.write("static const unsigned short sSceneRom[][2] = {\n")
        for sid, di, name in rows:
            h.write(f"    {{ {sid}, {di} }}, // {name}\n")
        h.write("};\n")
except FileNotFoundError:
    pass

# Archipelago (MM Recomp randomizer): location name -> area, for the spoiler heatmap.
try:
    aploc = json.load(open("ap_locations.json"))
except FileNotFoundError:
    aploc = {}

out = dict(old)
out["aploc"] = aploc
# The Moon Draft: one list of picks. Mutators keep their ids; a curse is MUT_COUNT + its id.
picks = [dict(m, kind="mutator") for m in muts] + [dict(c, id=mut_ids.index("MUT_COUNT") + c["id"], kind="curse") for c in curses]
allpools = [{"id": i, "name": n, "tier": pool_tier.get(i, 1)} for i, n in enumerate(pool_names)]
out.update(allevents=allevents, allpools=allpools, events=events, traits=traits, muts=muts, pouch=pouch, curses=curses, picks=picks, pools=pools, zones=zones)

# (3.1.8) Nemesis stats and the unique trait each unlocks at 5, 10 and 20.
stat_names = c_array(v2, "static const char* sNemStatNames[NST_COUNT]")
uniq_names = c_array(v2, "static const char* sNemUniqNames[NU_COUNT]")
uniq_descs = c_array(v2, "static const char* sNemUniqDescs[NU_COUNT]")
stat_descs = ["More health (+25% of its base per point).", "Hits harder (+15% damage per point).",
              "Moves faster (+3% per point).", "Finds them faster and turns up more often. At 8 it follows them through doors."]
out["stats"] = [{"id": i, "name": n, "desc": stat_descs[i]} for i, n in enumerate(stat_names)]
out["uniques"] = [{"id": i, "name": n, "desc": d, "stat": i // 3, "at": [5, 10, 20][i % 3]}
                  for i, (n, d) in enumerate(zip(uniq_names, uniq_descs))]

# Every sound effect by name, for the panel's Raw sound picker (bank > group > sound).
import glob, os
SFX_BANKS = [("system", "Menus and system"), ("player", "Link"), ("voice", "Voices"), ("item", "Items and weapons"),
             ("enemy", "Enemies"), ("environment", "World"), ("ocarina", "Ocarina")]
sfx = []
for key, title in SFX_BANKS:
    path = "../../mm-decomp/include/tables/sfx/%sbank_table.h" % key
    groups = {}
    for m in re.finditer(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_SFX\(\s*\w+\s*,\s*(NA_SE_\w+)", open(path).read()):
        sid = int(m.group(1), 16)
        parts = [x for x in m.group(2).split("_")[3:] if x]  # NA SE XX <rest>
        if not parts or "DUMMY" in m.group(2) or "UNUSED" in m.group(2):
            continue
        g = parts[0].capitalize()
        rest = " ".join(parts[1:]).lower() or parts[0].lower()
        groups.setdefault(g, []).append([sid, rest])
    misc = []
    tree = []
    for g in sorted(groups):
        if len(groups[g]) < 3:
            misc += [[sid, (g + " " + r).lower() if r != g.lower() else r] for sid, r in groups[g]]
        else:
            tree.append([g, groups[g]])
    if misc:
        tree.append(["Other", sorted(misc, key=lambda x: x[1])])
    sfx.append([title, tree])
json.dump(sfx, open("sfx.json", "w"), separators=(",", ":"))
print("sfx", sum(len(i) for b in sfx for g in b[1] for i in [g[1]]))
json.dump(out, open("tables.json", "w"), indent=1)
print({k: len(v) for k, v in out.items()})
