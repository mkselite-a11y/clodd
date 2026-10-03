// Global Events friend link: relay + control panel (Cloudflare Worker).
//
// Setup (Cloudflare dashboard, no installs):
//   1. Storage & Databases > D1 > Create database, name it "global-events".
//   2. Workers & Pages > Create > Worker ("Hello World"), then Edit code,
//      replace everything with this file, and Deploy.
//   3. The worker's Settings > Bindings > Add > D1 database:
//      variable name DB, database "global-events". Deploy again if asked.
//   4. Change ROOM_KEY below to your own password (or add a secret named
//      ROOM_KEY under Settings > Variables and Secrets, which wins over this).

const ROOM_KEY = "change-me";

// Every friend has their own name (5 letters, unique in the room), their own chaos
// points and their own cooldowns. The player picks what each friend may do in
// game (Friends tab): Full, Experimental (Full plus the sandbox), Helpful (only nice things) or Blocked.

// Unlimited power: true = no chaos point costs and no cooldowns for everyone.
const UNLIMITED = false;

// Secret power mode on the panel (Konami code, or tap the title 7 times).
// It lifts costs and cooldowns for that browser only.
// "" = the secret alone unlocks it. Set a password here so that only someone
// who knows it can turn it on (your friend can read the page's code).
const POWER_KEY = "";

// Chaos points: each friend spends their own; they refill over time.
const POINTS_MAX = 100;
const POINTS_PER_MIN = 10;
const POINTS_OVERFLOW = 150; // wagers and death bonuses can push past the max, up to this
const DEATH_BONUS = 15;      // every friend gets this each time they fall
const ONLINE_MS = 75000;     // a friend counts as "here" this long after their panel last checked in
                             // (long enough that a background tab, which browsers slow to one check a minute, doesn't flicker)

// type: [cost, cooldown seconds, label]
const COMMANDS = {
  ev_now:    [40,  90, "Start an event now"],
  ev_next:   [20,  30, "Pick the next event"],
  ev_soon:   [15,  60, "Hurry the next event"],
  ev_extend: [20,  45, "Event +30 seconds"],
  ev_end:    [45, 300, "End the event"],
  fakeout:   [5,   30, "Fake out"],
  empower:   [25,  60, "Empower the next event"],
  nem_make:  [25,  60, "Create a Nemesis"],
  nem_trait: [35,  60, "Give the Nemesis a trait"],
  nem_level: [15,  30, "Give the Nemesis a skill point"],
  nem_hunt:  [25,  90, "Send the Nemesis to them"],
  nem_name:  [2,   10, "Rename the Nemesis"],
  nem_stat:  [0,    1, "Spend Nemesis points"],
  nem_pick:  [0,    1, "Pick the Nemesis trait"],
  nem_purge: [45, 300, "Purge the Nemesis"],
  bounty:    [20, 120, "Post a bounty on them"],
  wanted:    [40, 600, "Post a WANTED poster"],
  ambush:    [25,  45, "Ambush"],
  airstrike: [20,  45, "Air strike"],
  heal:      [35, 180, "Heal 2 hearts"],
  size:      [15,  60, "Change their size"],
  shake:     [5,   15, "Shake the ground"],
  blackout:  [15,  60, "Blackout"],
  sfx:       [3,    5, "Sound"],
  pouch:     [30, 180, "Gift a pouch item"],
  gift:      [30, 300, "Gift 10 Moon Marks"],
  tax:       [15,  60, "Tax 15 Moon Marks"],
  msg:       [2,    5, "Message"],
  quiz:      [40,  90, "Custom Pop Quiz"],
  x:         [0,    0, "Experimental"],
  place:     [5,    2, "Place"],
  steer:     [0,    1, "Steer"],
  surprise:  [20,  30, "Plant a surprise"],
  cuccos:    [10,  45, "Cucco party"],
  disco:     [8,   45, "Disco lights"],
  confetti:  [3,   15, "Confetti"],
  beam:      [0,    1, "Look here"],
  forced_ambush: [60, 300, "FORCED AMBUSH"],
  bet:       [0,    0, "Wager"],
};

// What a Helpful friend can't do (matches the game's own list).
const HARMFUL = new Set(["ev_now", "ev_next", "ev_soon", "nem_make", "nem_trait", "nem_level", "nem_hunt",
  "bounty", "ambush", "quiz", "tax", "airstrike", "wanted", "ev_extend", "empower", "blackout",
  "surprise", "place", "steer", "forced_ambush", "bet"]);

// Events a friend can place things for, and the surprise kinds.
const PLACEABLE = { 9: "the treasure", 23: "the procession's path", 38: "the buried cache", 42: "the next sweep", 55: "a seal" };
const SURPRISE_KINDS = ["Event", "Ambush", "Bomb drop", "Rupoor rain", "Nemesis"];
const STEER_KINDS = ["Moon Tether", "Moonfall", "Searchlights"];


const TABLES = __TABLES__;
const PANEL_HTML = __PANEL__;

const EVENTS = new Map(TABLES.events.map((e) => [e.id, e]));
const MUTS = new Map(TABLES.muts.map((m) => [m.id, m]));
const PICKS = new Map(TABLES.picks.map((m) => [m.id, m])); // the Moon Draft: mutators and curses
const POOL_IDS = new Set(TABLES.pools.map((p) => p.id));
const STAKES = [10, 25, 50];

// Game feature switches (the status line "opts").
const OPT = { events: 1, muts: 2, bounties: 4, nemesis: 8, pouch: 16, curse: 32, combos: 64 };

let schemaReady = false;

async function ensureSchema(db) {
  if (schemaReady) return;
  await db.batch([
    db.prepare("CREATE TABLE IF NOT EXISTS cmds (id INTEGER PRIMARY KEY AUTOINCREMENT, t INTEGER, line TEXT, label TEXT)"),
    db.prepare("CREATE TABLE IF NOT EXISTS kv (k TEXT PRIMARY KEY, v TEXT)"),
    db.prepare("CREATE TABLE IF NOT EXISTS maps (scene INTEGER, bx INTEGER, bz INTEGER, base INTEGER, cells TEXT, t INTEGER, PRIMARY KEY (scene, bx, bz))"),
  ]);
  // Columns added later (older databases get them here; errors mean they exist).
  for (const sql of ["ALTER TABLE cmds ADD COLUMN cost INTEGER DEFAULT 0", "ALTER TABLE cmds ADD COLUMN refunded INTEGER DEFAULT 0",
    "ALTER TABLE cmds ADD COLUMN fid TEXT DEFAULT ''"]) {
    try { await db.prepare(sql).run(); } catch (e) { /* already there */ }
  }
  schemaReady = true;
}

// All small values in one read.
async function kvAll(db) {
  const { results } = await db.prepare("SELECT k, v FROM kv").all();
  return Object.fromEntries(results.map((r) => [r.k, r.v]));
}

function kvSet(db, k, v) {
  return db.prepare("INSERT INTO kv (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v").bind(k, String(v));
}

function kvDel(db, k) {
  return db.prepare("DELETE FROM kv WHERE k = ?").bind(k);
}

// --- Friends ------------------------------------------------------------------------

// Everyone who has used this room: fid -> { name, t (last seen) }.
function friendsOf(kv) {
  const out = {};
  for (const k of Object.keys(kv)) {
    if (!k.startsWith("friend:")) continue;
    try { out[k.slice(7)] = JSON.parse(kv[k]); } catch (e) { /* skip */ }
  }
  return out;
}

// What the player allows each friend (from the game's status): 0 blocked, 1 helpful, 2 full, 3 experimental.
function modeOf(st, name) {
  for (const part of (st.fmodes || "").split(",")) {
    const [n, m] = part.split(":");
    if (n === name) return parseInt(m, 10);
  }
  return 2;
}

// A friend's points: their own pool, refilling, plus the room's fall bonuses they haven't banked.
function pointsNow(kv, fid, now) {
  const p = kv["pts:" + fid] !== undefined ? parseFloat(kv["pts:" + fid]) : POINTS_MAX;
  const t = kv["ptt:" + fid] !== undefined ? parseInt(kv["ptt:" + fid], 10) : now;
  const refill = p >= POINTS_MAX ? p : Math.min(POINTS_MAX, p + ((now - t) / 60000) * POINTS_PER_MIN);
  const bonus = Math.max(0, parseInt(kv.bonus_total || "0", 10) - parseInt(kv["bonus:" + fid] !== undefined ? kv["bonus:" + fid] : kv.bonus_total || "0", 10));
  return Math.min(Math.max(POINTS_OVERFLOW, refill), refill + bonus);
}

// Writes for a friend's new total (also banks the fall bonuses).
function pointsSet(db, kv, fid, now, value) {
  return [kvSet(db, "pts:" + fid, value), kvSet(db, "ptt:" + fid, now), kvSet(db, "bonus:" + fid, kv.bonus_total || "0")];
}

function pointsAdd(kv, fid, now, amount) {
  const p = pointsNow(kv, fid, now);
  return Math.min(Math.max(POINTS_OVERFLOW, p), p + amount);
}

function parseStatus(text) {
  const o = {};
  for (const l of (text || "").split("\n")) {
    const i = l.indexOf("=");
    if (i > 0) o[l.slice(0, i)] = l.slice(i + 1);
  }
  return o;
}

function clean(text, max) {
  return String(text || "").replace(/[^A-Za-z0-9 .,!?'():;+\-]/g, "").slice(0, max || 30).trim();
}

function int(v) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? n : NaN;
}

function evName(id) {
  return EVENTS.has(id) ? EVENTS.get(id).name : "?";
}

function note(db, now, label, fid) {
  return db.prepare("INSERT INTO cmds (t, line, label, cost, refunded, fid) VALUES (?, 'note|0|0|0|', ?, 0, 1, ?)").bind(now, label, fid || "");
}

// "x,z;x,z" -> [[x, z], ...] (up to max pairs, each within the world).
function parseSpots(text, max) {
  const out = [];
  for (const part of String(text || "").split(";")) {
    const [x, z] = part.split(",").map((v) => parseInt(v, 10));
    if (Number.isFinite(x) && Number.isFinite(z) && Math.abs(x) < 40000 && Math.abs(z) < 40000) out.push([x, z]);
    if (out.length >= max) break;
  }
  return out;
}

// What the game has switched on / what's possible right now. Returns an error string or "".
const QUIZ_EV = (TABLES.events.find((e) => e.name === "Pop Quiz") || { id: -1 }).id;

// A friend's own Pop Quiz: up to 4 questions, each with the right answer and 3 wrong ones.
// Becomes "question;right;wrong;wrong;wrong" lines the game reads in order.
function quizLines(q) {
  if (!Array.isArray(q)) return "Write at least one question.";
  const out = [];
  for (const item of q.slice(0, 4)) {
    const f = (t, n) => clean(String(t || "").replace(/;/g, ","), n);
    const question = f(item && item.q, 36);
    const ans = (item && Array.isArray(item.a) ? item.a : []).slice(0, 4).map((x) => f(x, 14));
    if (!question && ans.every((x) => !x)) continue;
    if (!question) return "Every question needs its question.";
    if (ans.length < 4 || ans.some((x) => !x)) return "Every question needs the right answer and 3 wrong ones.";
    out.push([question, ...ans].join(";"));
  }
  return out.length ? out : "Write at least one question.";
}

function precheck(type, a, b, c, st) {
  const opts = parseInt(st.opts || "127", 10);
  const on = (bit) => (opts & bit) !== 0;
  const enabled = st.enabled ? BigInt("0x" + st.enabled) : null;
  const evOn = (id) => enabled === null || ((enabled >> BigInt(id)) & 1n) === 1n;
  const hasNem = st.nem && st.nem !== "-";
  // (3.1.8) Every Nemesis; "mine" is the one this friend's moves go to: their own,
  // or the default one if they're the first friend there.
  const nemList = (st.nems || "").split(";").filter(Boolean).map((e) => {
    const f = e.split("|");
    return { slot: parseInt(f[0], 10), owner: f[1] || "", level: parseInt(f[4], 10) || 0, points: parseInt(f[5], 10) || 0,
      stats: (f[6] || "0,0,0,0").split(",").map((x) => parseInt(x, 10) || 0), picks: parseInt(f[7], 10) || 0,
      traits: parseInt(f[10], 10) || 0 };
  });
  const me = String(st.me || "").slice(0, 5);
  const leads = !st.nemfirst || st.nemfirst === me;
  const mine = nemList.find((n) => n.owner && n.owner === me) || (leads ? nemList.find((n) => n.slot === 0) : null);
  const freeSlot = [1, 2, 3].some((i) => !nemList.find((n) => n.slot === i));
  const muts = (st.muts || "").split(",").filter(Boolean);
  if (type === "quiz") {
    type = "ev_now";
    a = QUIZ_EV;
  }
  switch (type) {
    case "ev_now":
    case "ev_next":
      if (!on(OPT.events)) return "Their events are turned off.";
      if (!evOn(a)) return "They turned that event off.";
      if (type === "ev_now" && st.friendnow) return "An event a friend started is still waiting to start.";
      // (3.1.8) A wager locks the next event until it starts.
      if (st.betlock && (!st.ev || st.ev === "-")) return "There's a wager on their next event (" + st.betlock + "). Wait until it starts.";
      if (type === "ev_next" && st.foresight === "1") return "They used Foresight: the next event is locked in.";
      if (type === "ev_next" && st.friendnext) return "A friend already picked their next event.";
      if (type === "ev_next" && b >= 0) {
        if (b === a && !EVENTS.get(a).double) return "That event can't be doubled. Pick a different combo event.";
        if (!on(OPT.combos)) return "They have combos turned off.";
        if (!evOn(b)) return "They turned the combo event off.";
        if (b !== a && !EVENTS.get(a).primary) return "That event can't lead a combo.";
      }
      return "";
    case "empower":
      if (!on(OPT.events)) return "Their events are turned off.";
      return st.empower === "1" ? "Their next event is already empowered." : "";
    case "ev_soon":
      if (!on(OPT.events)) return "Their events are turned off.";
      if (st.ev && st.ev !== "-") return "An event is already running.";
      return "";
    case "ev_end":
      return !st.ev || st.ev === "-" ? "No event is running." : "";
    case "ev_extend":
      return st.evleft === undefined ? "No timed event is running." : "";
    case "nem_make":
      if (!on(OPT.nemesis)) return "Their Nemesis is turned off.";
      if (mine) return leads && !mine.owner ? "They already have a Nemesis." : "Your Nemesis on them is still alive.";
      return leads || freeSlot ? "" : "Every Nemesis slot is taken (4 at most).";
    case "nem_stat":
    case "nem_pick":
    case "nem_purge":
    case "nem_trait":
    case "nem_level":
    case "nem_hunt":
    case "nem_name":
      if (!on(OPT.nemesis)) return "Their Nemesis is turned off.";
      if (!mine) return nemList.length ? "None of their Nemeses is yours. You get one when a slot opens." : "They don't have a Nemesis.";
      if (type === "nem_stat" && mine.points <= 0) return "No skill points to spend.";
      if (type === "nem_stat" && mine.stats[a] >= 50) return "That stat is maxed (50).";
      if (type === "nem_pick" && mine.picks <= 0) return "No trait pick waiting. It earns one every 5 levels.";
      if ((type === "nem_pick" || type === "nem_trait") && ((mine.traits >> a) & 1)) return "It already has that trait.";
      if (type === "nem_level" && mine.level >= 255) return "It's at its max level.";
      return "";
    case "bounty":
    case "wanted":
      if (!on(OPT.bounties)) return "Their bounties are turned off.";
      if (type === "wanted" && st.wanted === "1") return "A WANTED poster is already up.";
      return "";
    case "pouch":
      return on(OPT.pouch) ? "" : "Their pouch is turned off.";
    case "heal":
      return st.hpfull === "1" ? "They're at full health." : "";
    case "tax":
      return st.marks === "0" ? "They're broke." : "";
    case "place": {
      const [ev, left] = (st.evplace || "").split(":");
      if (!st.evplace) return "Nothing to place right now. Some events let you place things in their first 15 seconds.";
      if (parseInt(ev, 10) !== a) return "That's not the event that's running.";
      return parseInt(left, 10) > 0 ? "" : "Too late to place it.";
    }
    case "steer":
      return String(a) === st.steer ? "" : "Nothing to steer right now.";
    case "surprise": {
      const waiting = (st.surprises || "").split(";").filter(Boolean).length;
      if (waiting >= 8) return "8 surprises are already waiting for them.";
      if (b === 0 && !on(OPT.events)) return "Their events are turned off.";
      if (b === 0 && !evOn(c)) return "They turned that event off.";
      if (b === 4 && !hasNem) return "They don't have a Nemesis.";
      return "";
    }
    default:
      return "";
  }
}

// Checks the arguments; returns [a, b, c, text, extra label] or a string error.
function validate(type, a, b, c, text) {
  const poolName = (id) => (TABLES.pools.find((p) => p.id === id) || {}).name;
  switch (type) {
    case "ev_now":
      return EVENTS.has(a) ? [a, 0, 0, "", evName(a)] : "Pick an event.";
    case "ev_next":
      if (!EVENTS.has(a)) return "Pick an event.";
      // b === a doubles the event (one run, twice as fierce), only for events marked "double".
      if (b !== -1 && (!EVENTS.has(b) || (b === a ? !EVENTS.get(a).double : !EVENTS.get(b).second))) return "That can't be a combo partner.";
      return [a, b, 0, "", evName(a) + (b === a ? " x2" : b >= 0 ? " + " + evName(b) : "")];
    case "nem_make":
      return POOL_IDS.has(a) ? [a, 0, 0, "", poolName(a)] : "Pick an enemy.";
    case "ambush": {
      if (!POOL_IDS.has(a)) return "Pick an enemy.";
      if (!(b >= 1 && b <= 3)) return "1 to 3 enemies.";
      const spots = parseSpots(text, b);
      return [a, b, 0, spots.map((s) => s.join(",")).join(";"), poolName(a) + (spots.length ? " (aimed)" : "")];
    }
    case "airstrike":
      if (c === 1 && (!(Math.abs(a) < 40000) || !(Math.abs(b) < 40000))) return "Pick a spot.";
      return c === 1 ? [a, b, 1, "", "aimed"] : [0, 0, 0, "", ""];
    case "nem_trait":
    case "nem_pick":
      return TABLES.traits.find((t) => t.id === a) ? [a, 0, 0, "", TABLES.traits.find((t) => t.id === a).name] : "Pick a trait.";
    case "nem_name": {
      const t = clean(text, 12);
      return t ? [0, 0, 0, t, '"' + t + '"'] : "Type a name.";
    }
    case "nem_stat":
      if (!(a >= 0 && a < TABLES.stats.length)) return "Pick a stat.";
      if (!(b >= 1 && b <= 20)) return "1 to 20 points.";
      return [a, b, 0, "", TABLES.stats[a].name + " +" + b];
    case "bounty": {
      if (!POOL_IDS.has(a)) return "Pick an enemy.";
      if (!(b >= 1 && b <= 5)) return "1 to 5 stars.";
      if (!(c >= 0 && c < 65536)) return "Bad traits.";
      let bits = 0;
      for (let i = 0; i < 16; i++) if (c & (1 << i)) bits++;
      if (bits > b) return "At most one trait per star.";
      return [a, b, c, "", poolName(a) + " " + "*".repeat(b)];
    }
    case "sfx":
      return a >= 0 && a < TABLES.sounds.length ? [a, 0, 0, "", TABLES.sounds[a].name] : "Pick a sound.";
    case "pouch":
      return TABLES.pouch.find((p) => p.id === a) ? [a, 0, 0, "", TABLES.pouch.find((p) => p.id === a).name] : "Pick an item.";
    case "size":
      return a === 1 || a === 2 ? [a, 0, 0, "", a === 1 ? "giant" : "tiny"] : "Giant or tiny.";
    case "quiz":
      return a >= 1 && a <= 4 ? [a, 0, 0, "", a + (a === 1 ? " question" : " questions")] : "Write at least one question.";
    case "msg": {
      const t = clean(text, 30);
      return t ? [0, 0, 0, t, '"' + t + '"'] : "Type a message.";
    }
    case "place": {
      if (!PLACEABLE[a]) return "That event has nothing to place.";
      if (!(Math.abs(b) < 40000) || !(Math.abs(c) < 40000)) return "Pick a spot.";
      const extra = Math.max(0, Math.min(9, parseInt(text, 10) || 0));
      return [a, b, c, String(extra), PLACEABLE[a]];
    }
    case "steer":
      if (!(a >= 0 && a <= 2) || !(Math.abs(b) < 40000) || !(Math.abs(c) < 40000)) return "Pick a spot.";
      return [a, b, c, "", STEER_KINDS[a]];
    case "beam":
      if (!(Math.abs(a) < 40000) || !(Math.abs(b) < 40000)) return "Pick a spot.";
      return [a, b, 0, "", ""];
    case "forced_ambush":
      if (!POOL_IDS.has(a)) return "Pick an enemy.";
      if (!(b >= 3 && b <= 6)) return "3 to 6 enemies.";
      return [a, b, 0, "", b + " " + poolName(a)];
    case "surprise": {
      const zone = TABLES.zones[a];
      if (!zone) return "Pick an area.";
      if (!(b >= 0 && b < SURPRISE_KINDS.length)) return "Pick a surprise.";
      if (b === 0 && !EVENTS.has(c)) return "Pick an event.";
      if (b === 1 && !POOL_IDS.has(c)) return "Pick an enemy.";
      // A spot, or (3.1.8) an area: two opposite corners, any size.
      const spot = parseSpots(text, 2);
      const what = b === 0 ? evName(c) : b === 1 ? poolName(c) : SURPRISE_KINDS[b];
      return [a, b, b <= 1 ? c : 0, spot.map((p) => p.join(",")).join(";"), what + " in " + zone.name];
    }
    default:
      return [0, 0, 0, "", ""];
  }
}

// Harder events and enemies cost more (tiers set with the player).
function costFor(type, a, b, base, c) {
  const evT = (id) => (EVENTS.get(id) || { tier: 1 }).tier;
  const poolT = (id) => ((TABLES.pools.find((p) => p.id === id)) || { tier: 1 }).tier;
  switch (type) {
    case "ev_now": return [25, 35, 45, 60][evT(a)];
    case "quiz": return [25, 35, 45, 60][evT(QUIZ_EV)] + 5;
    case "ev_next": return Math.min([10, 15, 25, 35][evT(a)] + (b >= 0 ? [5, 8, 12, 18][evT(b)] : 0), 50);
    case "ambush": return [8, 12, 18, 30][poolT(a)] * Math.max(1, b);
    case "nem_make": return [15, 25, 35, 45][poolT(a)];
    case "bounty": return [10, 15, 20, 30][poolT(a)] + 5 * Math.max(1, b);
    // Gifts cost about what they're worth (Second Wind is an extra life).
    case "pouch": return [30, 30, 25, 40, 40, 90, 35, 20, 15, 25, 45, 20][a] || base;
    case "airstrike": return c === 1 ? base + 5 : base;
    // Never cheaper than sending the same thing straight at them.
    case "forced_ambush": return Math.min(POINTS_MAX, Math.max(base, [10, 15, 22, 30][poolT(a)] * Math.max(3, b)));
    case "surprise":
      if (b === 0) return [25, 35, 45, 60][evT(c)];         // = start it now
      if (b === 1) return [8, 12, 18, 30][poolT(c)] * 3;    // = an ambush of 3
      return [base, base, 30, 15, 25][b] || base;           // bombs: more than an aimed air strike
    default: return base;
  }
}

function json(obj, status = 200) {
  return new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json" } });
}

const MAP_CELLS = /^[.~0-o]{256}([0-9a-f]{64})?$/;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const key = env.ROOM_KEY || ROOM_KEY;

    if (url.pathname === "/" || url.pathname === "/index.html") {
      return new Response(PANEL_HTML, { headers: { "content-type": "text/html; charset=utf-8" } });
    }
    if (!url.pathname.startsWith("/api/")) {
      return new Response("Not found", { status: 404 });
    }
    if (!env.DB) {
      return new Response("The D1 database isn't bound as DB yet.", { status: 500 });
    }
    if (key === "change-me") {
      return new Response("Set ROOM_KEY in the worker first.", { status: 401 });
    }
    if (request.headers.get("X-Room-Key") !== key) {
      return new Response("Wrong room key.", { status: 401 });
    }
    const db = env.DB;
    await ensureSchema(db);
    const now = Date.now();
    const powerKey = env.POWER_KEY !== undefined ? env.POWER_KEY : POWER_KEY;
    const powerOk = (k) => powerKey === "" || k === powerKey;
    const power = UNLIMITED || (request.headers.get("X-Power") === "1" && powerOk(request.headers.get("X-Power-Key") || ""));
    // Who's asking (panel calls): a hidden id kept by their browser, plus their name.
    const fid = String(request.headers.get("X-Friend-Id") || "").replace(/[^A-Za-z0-9]/g, "").slice(0, 32);
    const myName = String(request.headers.get("X-Friend-Name") || "").replace(/[^A-Za-z0-9]/g, "").slice(0, 5);

    // The panel: check the power password.
    if (url.pathname === "/api/power" && request.method === "POST") {
      const ok = powerOk(request.headers.get("X-Power-Key") || "");
      return json({ ok, needKey: powerKey !== "" });
    }

    // The game: new commands since `after` (-1 on startup: just the cursor), and who's here.
    if (url.pathname === "/api/pull") {
      const after = parseInt(url.searchParams.get("after") || "-1", 10);
      // (only the rows this needs: the whole kv table every second adds up on D1's free plan)
      const { results: kvRows } = await db.prepare("SELECT k, v FROM kv WHERE k = 'panel_t' OR (k >= 'friend:' AND k < 'friend;')").all();
      const kv = Object.fromEntries(kvRows.map((r) => [r.k, r.v]));
      // Nobody watching the panel for a minute: the game can check in less often.
      const idle = !kv.panel_t || now - parseInt(kv.panel_t, 10) > 60000 ? "idle|1\n" : "idle|0\n";
      const here = Object.values(friendsOf(kv)).filter((f) => now - f.t < ONLINE_MS).map((f) => f.name);
      const friends = "friends|" + here.join(",") + "\n";
      if (!(after >= 0)) {
        const row = await db.prepare("SELECT MAX(id) AS m FROM cmds").first();
        return new Response("cursor|" + (row && row.m ? row.m : 0) + "\n" + idle + friends);
      }
      const { results } = await db.prepare("SELECT id, line FROM cmds WHERE id > ? AND line NOT LIKE 'note|%' ORDER BY id LIMIT 10").bind(after).all();
      let out = "";
      let cursor = after;
      for (const r of results) {
        out += r.id + "|" + r.line + "\n";
        cursor = Math.max(cursor, r.id);
      }
      // (The game only moves past commands it actually took, if its queue is full.)
      return new Response("cursor|" + cursor + "\n" + idle + friends + out);
    }

    // The game: a block of the area's floor, for the panel's maps.
    if (url.pathname === "/api/map" && request.method === "POST") {
      const m = parseStatus((await request.text()).slice(0, 1000));
      const scene = int(m.scene), bx = int(m.bx), bz = int(m.bz), base = int(m.base);
      if (![scene, bx, bz, base].every(Number.isFinite) || !MAP_CELLS.test(m.cells || "")) return new Response("bad map", { status: 400 });
      // Once the ROM maps (walkable ground only) are in, the game's own scans aren't needed.
      const rom = await db.prepare("SELECT v FROM kv WHERE k = 'maps_rom_v3'").first();
      if (rom) return new Response("ok");
      await db.prepare("INSERT INTO maps (scene, bx, bz, base, cells, t) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(scene, bx, bz) DO UPDATE SET base = excluded.base, cells = excluded.cells, t = excluded.t")
        .bind(scene, bx, bz, base, m.cells, now).run();
      return new Response("ok");
    }

    // The game's DLL: every area's map, made once from the player's ROM.
    if (url.pathname === "/api/mapbulk") {
      if (request.method !== "POST") {
        const row = await db.prepare("SELECT v FROM kv WHERE k = 'maps_rom_v3'").first();
        return new Response(row ? "done" : "no");
      }
      if (url.searchParams.get("reset") === "1") {
        await db.batch([db.prepare("DELETE FROM maps"), kvDel(db, "maps_rom_v1"), kvDel(db, "maps_rom_v2"), kvDel(db, "maps_rom_v3")]);
        return new Response("ok");
      }
      if (url.searchParams.get("done") === "1") {
        await kvSet(db, "maps_rom_v3", now).run();
        return new Response("ok");
      }
      const writes = [];
      for (const line of (await request.text()).slice(0, 40000).split("\n")) {
        const [scene, bx, bz, base, cells] = line.split("|");
        const n = [scene, bx, bz, base].map((x) => parseInt(x, 10));
        if (!n.every(Number.isFinite) || !MAP_CELLS.test(cells || "")) continue;
        writes.push(db.prepare("INSERT OR REPLACE INTO maps (scene, bx, bz, base, cells, t) VALUES (?, ?, ?, ?, ?, ?)").bind(n[0], n[1], n[2], n[3], cells, 0));
        if (writes.length >= 60) break;
      }
      if (writes.length) await db.batch(writes);
      return new Response("ok");
    }

    // The panel: the mapped blocks of one area, or which areas have any.
    if (url.pathname === "/api/map") {
      const scene = int(url.searchParams.get("scene"));
      if (Number.isFinite(scene)) {
        const { results } = await db.prepare("SELECT bx, bz, base, cells FROM maps WHERE scene = ? LIMIT 1500").bind(scene).all();
        return json({ scene, blocks: results });
      }
      const { results } = await db.prepare("SELECT scene, COUNT(*) AS n FROM maps GROUP BY scene").all();
      return json({ scenes: results });
    }

    // The game: status snapshot. Also where refunds, fall bonuses and wagers settle.
    if (url.pathname === "/api/status" && request.method === "POST") {
      const text = (await request.text()).slice(0, 8000);
      const st = parseStatus(text);
      const kv = await kvAll(db);
      const writes = [kvSet(db, "status", text), kvSet(db, "status_t", now)];
      const gains = {}; // fid -> points back
      const give = (who, n) => { if (who) gains[who] = (gains[who] || 0) + n; };

      // Commands that couldn't run in the game: points back to whoever sent them.
      const ids = (st.refund || "").split(",").map((x) => parseInt(x, 10)).filter((x) => x > 0).slice(0, 8);
      if (ids.length) {
        const marks = ids.map(() => "?").join(",");
        const { results } = await db.prepare(`SELECT id, cost, label, fid FROM cmds WHERE id IN (${marks}) AND refunded = 0`).bind(...ids).all();
        for (const r of results) {
          give(r.fid, r.cost || 0);
          writes.push(db.prepare("UPDATE cmds SET refunded = 1 WHERE id = ?").bind(r.id));
          if (r.cost) writes.push(note(db, now, "Refund +" + r.cost + ": " + r.label + " (couldn't happen)", r.fid));
        }
      }
      // They fell: every friend gets a little something.
      const deaths = parseInt(st.deaths || "0", 10);
      const lastDeaths = kv.last_deaths !== undefined ? parseInt(kv.last_deaths, 10) : deaths;
      if (deaths > lastDeaths) {
        const total = parseInt(kv.bonus_total || "0", 10) + DEATH_BONUS * (deaths - lastDeaths);
        writes.push(kvSet(db, "bonus_total", total), note(db, now, "They fell! +" + DEATH_BONUS * (deaths - lastDeaths) + " chaos for everyone"));
        kv.bonus_total = String(total);
      }
      if (deaths !== lastDeaths || kv.last_deaths === undefined) writes.push(kvSet(db, "last_deaths", deaths));
      // Wager on the event that was running.
      const betFid = kv.bet_fid || "";
      const betWho = kv.bet_from ? kv.bet_from + ": " : "";
      const endBet = () => writes.push(kvDel(db, "bet_stake"), kvDel(db, "bet_seq"), kvDel(db, "bet_pick"), kvDel(db, "bet_ev"), kvDel(db, "bet_from"), kvDel(db, "bet_fid"));
      // The event that just ended, and whether it's the one the wager was on (a restarted
      // game counts events from 0 again, so the count alone can't tell).
      const endedEv = (st.evres || "").split("|")[0];
      const betOnOther = kv.bet_stake !== undefined && parseInt(st.evseq || "0", 10) > parseInt(kv.bet_seq, 10) &&
        endedEv && kv.bet_ev && !kv.bet_ev.startsWith(endedEv);
      // (3.1.8) The wagered event stopped being the next one before it started (can't happen here).
      const betGone = kv.bet_stake !== undefined && parseInt(st.evseq || "0", 10) === parseInt(kv.bet_seq, 10) &&
        (!st.ev || st.ev === "-") && st.nextev && st.nextev !== kv.bet_ev;
      if (kv.bet_stake !== undefined && (parseInt(st.evseq || "0", 10) < parseInt(kv.bet_seq, 10) || betOnOther || betGone)) {
        give(betFid, parseInt(kv.bet_stake, 10));
        writes.push(note(db, now, betWho + (betGone || betOnOther ? "Wager refunded (that event couldn't happen)" : "Wager refunded (their game restarted)"), betFid));
        endBet();
      } else if (kv.bet_stake !== undefined && parseInt(st.evseq || "0", 10) > parseInt(kv.bet_seq, 10)) {
        const stake = parseInt(kv.bet_stake, 10);
        const result = (st.evres || "").split("|")[1] || "stopped";
        const saidSurvive = kv.bet_pick === "0";
        if (result === "stopped") {
          give(betFid, stake);
          writes.push(note(db, now, betWho + "Wager refunded (the event was stopped)", betFid));
        } else if ((result === "survived") === saidSurvive) {
          give(betFid, stake * 2);
          writes.push(note(db, now, betWho + "Won the wager! +" + stake * 2 + " chaos", betFid));
        } else {
          writes.push(note(db, now, betWho + "Lost the wager (" + stake + " chaos)", betFid));
        }
        endBet();
      }
      for (const [who, n] of Object.entries(gains)) {
        if (n > 0) writes.push(...pointsSet(db, kv, who, now, pointsAdd(kv, who, now, n)));
      }
      await db.batch(writes);
      return new Response("ok");
    }

    // Everything below is the panel, and needs to know which friend it is.
    if (!fid || !myName) return json({ ok: false, needName: true, msg: "Pick a name first." }, 400);
    // The panel closing or logging out: gone right away instead of after ONLINE_MS.
    if (url.pathname === "/api/bye") {
      const row = await db.prepare("SELECT v FROM kv WHERE k = ?").bind("friend:" + fid).first();
      if (row) {
        let f = null;
        try { f = JSON.parse(row.v); } catch (e) { /* skip */ }
        if (f) { f.t = 0; await kvSet(db, "friend:" + fid, JSON.stringify(f)).run(); }
      }
      return new Response("ok");
    }
    const kv = await kvAll(db);
    const friends = friendsOf(kv);
    const st = parseStatus(kv.status || "");
    // Names are unique in a room (the player sets modes by name).
    // (A name nobody has used for two weeks is free again.)
    const taken = Object.entries(friends).find(([id, f]) => id !== fid && f.name.toLowerCase() === myName.toLowerCase() &&
      now - f.t < 14 * 86400000);
    if (taken) return json({ ok: false, nameTaken: true, msg: "Someone in this room already goes by " + myName + ". Pick another name." }, 409);
    const me = friends[fid];
    const mode = modeOf(st, me ? me.name : myName);
    if (me && me.name !== myName && modeOf(st, me.name) === 0) {
      return json({ ok: false, blocked: true, name: me.name }); // a blocked friend can't rename their way out
    }
    if (!me) {
      // New here: fall bonuses count from now on.
      await kvSet(db, "bonus:" + fid, kv.bonus_total || "0").run();
      kv["bonus:" + fid] = kv.bonus_total || "0";
    }
    if (!me || me.name !== myName || now - me.t > 30000) {
      await kvSet(db, "friend:" + fid, JSON.stringify({ name: myName, t: now })).run();
      friends[fid] = { name: myName, t: now };
    }

    // The panel (Spoilers on): the Archipelago room a friend typed in, shared with the room.
    if (url.pathname === "/api/ap") {
      if (mode === 0) return json({ blocked: true });
      if (request.method === "POST") {
        let body;
        try { body = await request.json(); } catch { return json({ ok: false }, 400); }
        const address = String(body.address || "").replace(/[^A-Za-z0-9.:\-]/g, "").slice(0, 80);
        const slot = String(body.slot || "").slice(0, 32);
        await db.batch([kvSet(db, "ap_address", address), kvSet(db, "ap_slot", slot)]);
        return json({ ok: true });
      }
      // What the player typed in their game wins; what a friend typed is the fallback.
      if (st.ap && st.apslot) return json({ address: st.ap, slot: st.apslot, fromGame: true });
      return json({ address: kv.ap_address || "", slot: kv.ap_slot || "", fromGame: false });
    }

    // The panel: everything it shows.
    if (url.pathname === "/api/state") {
      if (mode === 0) return json({ blocked: true, name: myName });
      const status = kv.status || "";
      const statusT = parseInt(kv.status_t || "0", 10);
      const points = power ? POINTS_MAX : pointsNow(kv, fid, now);
      const { results } = await db.prepare("SELECT id, t, label, fid FROM cmds WHERE label != '' ORDER BY id DESC LIMIT 20").all();
      const cooldowns = {};
      for (const type of Object.keys(COMMANDS)) {
        const until = power ? 0 : parseInt(kv["cd:" + fid + ":" + type] || "0", 10);
        if (until > now) cooldowns[type] = Math.ceil((until - now) / 1000);
      }
      if (!kv.panel_t || now - parseInt(kv.panel_t, 10) > 30000) {
        await kvSet(db, "panel_t", now).run(); // "someone's watching" (the game polls faster then)
      }
      return json({
        status, statusAge: statusT ? Math.round((now - statusT) / 1000) : -1,
        me: myName, mode,
        friends: Object.values(friends).filter((f) => now - f.t < ONLINE_MS).map((f) => ({ name: f.name, mode: modeOf(st, f.name) })),
        points: Math.floor(points), max: POINTS_MAX, perMin: POINTS_PER_MIN, power,
        bet: kv.bet_stake !== undefined ? { stake: parseInt(kv.bet_stake, 10), pick: kv.bet_pick, ev: kv.bet_ev, from: kv.bet_from || "", mine: kv.bet_fid === fid } : null,
        deathBonus: DEATH_BONUS,
        log: results.map((r) => ({ t: r.t, label: r.label, mine: r.fid === fid })), cooldowns,
        costs: Object.fromEntries(Object.entries(COMMANDS).map(([k, v]) => [k, power ? 0 : v[0]])),
        cdlen: Object.fromEntries(Object.entries(COMMANDS).map(([k, v]) => [k, v[1]])),
      });
    }

    // The panel: send a command.
    if (url.pathname === "/api/cmd" && request.method === "POST") {
      let body;
      try { body = await request.json(); } catch { return json({ ok: false, msg: "Bad request." }, 400); }
      let type = String(body.type || "");
      const who = myName + ": ";
      const def = COMMANDS[type];
      if (!def) return json({ ok: false, msg: "Unknown action." });
      if (mode === 0) return json({ ok: false, blocked: true, msg: "..." });
      if (mode === 1 && HARMFUL.has(type)) return json({ ok: false, msg: "They only let you help them." });
      const statusT = parseInt(kv.status_t || "0", 10);
      if (!statusT || now - statusT > 20000) return json({ ok: false, msg: "The game isn't connected right now." });
      if (st.link === "0") return json({ ok: false, msg: "Friend Link is turned off in their menu." });
      const points = power ? POINTS_MAX : pointsNow(kv, fid, now);

      // Experimental friends: the sandbox. Free, a second apart, any settings ("k:v,k:v").
      if (type === "x") {
        if (mode !== 3) return json({ ok: false, msg: "They haven't set you to Experimental (Friends tab in their game)." });
        const op = int(body.a);
        if (!(op >= 1 && op <= 99)) return json({ ok: false, msg: "Unknown Experimental move." });
        let text = "";
        if (op === 50) {
          text = clean(body.text, 40);
          if (!text) return json({ ok: false, msg: "Type the banner text." });
        } else {
          const parts = [];
          for (const [k, v] of Object.entries(body.p || {})) {
            if (!/^[a-z0-9]{1,5}$/.test(k)) continue;
            const n = Number(v);
            if (!Number.isFinite(n)) continue;
            parts.push(k + ":" + (Math.round(Math.max(-100000, Math.min(100000, n)) * 10) / 10)); // (tenths at most)
          }
          text = parts.join(",");
          if (text.length > 96) return json({ ok: false, msg: "Too many settings at once (" + text.length + "/96 letters). Leave some at their defaults." });
        }
        const claim = await db.prepare(
          "INSERT INTO kv (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v WHERE CAST(kv.v AS INTEGER) <= ?"
        ).bind("cd:" + fid + ":x", String(now + 700), now).run();
        if (!claim.meta || claim.meta.changes === 0) return json({ ok: false, msg: "Easy, one a second." });
        const label = who + "EXP " + (String(body.label || "").replace(/[^A-Za-z0-9 .,!?+\-]/g, "").slice(0, 30) || "#" + op);
        await db.batch([
          db.prepare("INSERT INTO cmds (t, line, label, cost, refunded, fid) VALUES (?, ?, ?, 0, 1, ?)")
            .bind(now, ["x", op, 0, 0, text, myName].join("|"), label, fid),
          db.prepare("DELETE FROM cmds WHERE id < (SELECT MAX(id) - 300 FROM cmds)"),
        ]);
        return json({ ok: true, msg: "Sent: " + label, points: Math.floor(points) });
      }

      // Wager: chaos points on whether they get through the running event.
      // (3.1.8) Only on the next event, before it starts; that event is then locked in.
      if (type === "bet") {
        const stake = int(body.b);
        if (st.ev && st.ev !== "-") return json({ ok: false, msg: "Wagers are only on the next event, before it starts." });
        if (!st.nextev || st.nextid === undefined) return json({ ok: false, msg: "No next event to bet on yet." });
        if (kv.bet_stake !== undefined) return json({ ok: false, msg: kv.bet_from ? kv.bet_from + " already has a wager on the next event." : "There's already a wager on the next event." });
        if (st.next !== undefined && parseInt(st.next, 10) < 5) return json({ ok: false, msg: "Too late to bet on this one." });
        if (!STAKES.includes(stake)) return json({ ok: false, msg: "Pick a stake." });
        if (power) return json({ ok: false, msg: "Wagers are off during Moon Power." });
        if (points < stake) return json({ ok: false, msg: "Not enough chaos points." });
        const pick = int(body.a) === 1 ? "1" : "0";
        const label = who + "Wager " + stake + ": they " + (pick === "0" ? "make it through " : "don't make it through ") + st.nextev;
        // Claim the wager slot in one step, so two friends betting at once can't both pay.
        const claim = await db.prepare("INSERT INTO kv (k, v) VALUES ('bet_stake', ?) ON CONFLICT(k) DO NOTHING").bind(String(stake)).run();
        if (!claim.meta || claim.meta.changes === 0) return json({ ok: false, msg: "Someone just placed a wager on this event." });
        await db.batch([kvSet(db, "bet_seq", st.evseq || "0"), kvSet(db, "bet_pick", pick), kvSet(db, "bet_ev", st.nextev), kvSet(db, "bet_from", myName),
          kvSet(db, "bet_fid", fid), ...pointsSet(db, kv, fid, now, points - stake), note(db, now, label, fid),
          // The game holds that event as the next one (no refund: it costs nothing).
          db.prepare("INSERT INTO cmds (t, line, label, cost, refunded, fid) VALUES (?, ?, '', 0, 1, ?)")
            .bind(now, ["betlock", parseInt(st.nextid, 10) || 0, 0, 0, "", myName].join("|"), fid),
        ]);
        return json({ ok: true, msg: "Wager placed. Good luck!", points: Math.floor(points - stake) });
      }

      let a = int(body.a);
      const b = int(body.b), c = int(body.c), text = body.text;
      let quiz = null;
      if (type === "quiz") {
        quiz = quizLines(body.quiz);
        if (typeof quiz === "string") return json({ ok: false, msg: quiz });
        a = quiz.length;
      }
      const prefix = "";
      const v = validate(type, a, b, c, text);
      if (typeof v === "string") return json({ ok: false, msg: v });
      st.betlock = kv.bet_stake !== undefined ? kv.bet_ev || "?" : "";
      st.me = myName;
      const why = precheck(type, v[0], v[1], v[2], st);
      if (why) return json({ ok: false, msg: why });
      const cost = power ? 0 : costFor(type, v[0], v[1], def[0], v[2]);
      if (points < cost) return json({ ok: false, msg: "Not enough chaos points." });
      // Claim the cooldown in one step, so a double-click can't slip through.
      const cdType = type;
      await db.prepare("DELETE FROM kv WHERE k LIKE ? AND CAST(v AS INTEGER) < ?").bind("cd:" + fid + ":%", now).run(); // expired cooldowns
      const cd = COMMANDS[cdType][1];
      if (!power && cd > 0) {
        const claim = await db.prepare(
          "INSERT INTO kv (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v WHERE CAST(kv.v AS INTEGER) <= ?"
        ).bind("cd:" + fid + ":" + cdType, String(now + cd * 1000), now).run();
        if (!claim.meta || claim.meta.changes === 0) {
          const until = parseInt(kv["cd:" + fid + ":" + cdType] || String(now), 10);
          return json({ ok: false, msg: "Cooling down: " + Math.max(1, Math.ceil((until - now) / 1000)) + "s." });
        }
      }
      const [va, vb, vc, vtext, extra] = v;
      const line = [type, va, vb, vc, vtext, myName].join("|");
      // Steering is many small moves: it reaches the game but stays out of the history.
      const quiet = type === "steer" || type === "beam";
      const label = quiet ? "" : who + prefix + COMMANDS[type][2] + (extra ? ": " + extra : "");
      const writes = [
        // The quiz's questions go first, so the game has them when the quiz starts.
        ...(quiz || []).map((q, i) => db.prepare("INSERT INTO cmds (t, line, label, cost, refunded, fid) VALUES (?, ?, '', 0, 1, ?)")
          .bind(now, ["quizq", i, 0, 0, q, myName].join("|"), fid)),
        db.prepare("INSERT INTO cmds (t, line, label, cost, refunded, fid) VALUES (?, ?, ?, ?, 0, ?)").bind(now, line, label, cost, fid),
        db.prepare("DELETE FROM cmds WHERE id < (SELECT MAX(id) - 300 FROM cmds)"),
      ];
      if (!power && cost > 0) writes.push(...pointsSet(db, kv, fid, now, points - cost));
      await db.batch(writes);
      const wait = st.hold ? " (waits: " + st.hold.toLowerCase() + ")" : "";
      return json({ ok: true, msg: quiet ? "" : "Sent: " + label + wait, points: Math.floor(points - cost) });
    }

    return new Response("Not found", { status: 404 });
  },
};
