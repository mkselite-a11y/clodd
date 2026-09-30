// Moonfall Mayhem friend server.
//
//   POST /api/room/:code/game   the game (via moonfall_net.dll) posts its state
//                               once a second and receives queued commands.
//   GET  /api/room/:code/ws     friends connect here with a WebSocket.
//   GET  /api/room/:code/peek   plain JSON snapshot (handy for debugging).
//
// Everything else is the static friend website in ./public.

import { DurableObject } from "cloudflare:workers";

const ROOM_RE = /^[A-Z0-9_-]{2,24}$/;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const m = url.pathname.match(/^\/api\/room\/([^/]+)\/(game|ws|peek)$/);
    if (!m) {
      if (url.pathname.startsWith("/api/")) {
        return new Response("Not found", { status: 404 });
      }
      return env.ASSETS.fetch(request);
    }
    const code = decodeURIComponent(m[1]).toUpperCase();
    if (!ROOM_RE.test(code)) {
      return new Response("Bad room code", { status: 400 });
    }
    const stub = env.ROOMS.get(env.ROOMS.idFromName(code));
    return stub.fetch(request);
  },
};

// ---------------------------------------------------------------------------
// Friend actions. Costs are in Favor, which every friend earns while watching.
// ---------------------------------------------------------------------------

const FAVOR_MAX = 20;
const FAVOR_EVERY_MS = 15000;
const GAME_TIMEOUT_MS = 8000;
const VOTE_EVERY_MS = 4 * 60 * 1000;
const VOTE_LENGTH_MS = 45 * 1000;

const ACTIONS = {
  heal:      { cost: 3,  hurtful: false, label: "healed the hero" },
  shield:    { cost: 4,  hurtful: false, label: "cast a Moonshield" },
  gift:      { cost: 5,  hurtful: false, label: "sent a pouch item" },
  marks:     { cost: 4,  hurtful: false, label: "sent Moon Marks" },
  message:   { cost: 1,  hurtful: false, label: "sent a message" },
  nemweak:   { cost: 6,  hurtful: false, label: "weakened the nemesis" },
  bounty:    { cost: 6,  hurtful: false, label: "posted a bounty" },
  rename:    { cost: 4,  hurtful: false, label: "renamed the nemesis" },
  event:     { cost: 6,  hurtful: true,  label: "summoned an event" }, // calm events are allowed in helpful-only mode
  randevent: { cost: 4,  hurtful: true,  label: "rolled a random event" },
  chill:     { cost: 3,  hurtful: true,  label: "froze the hero's feet" },
  nembuff:   { cost: 8,  hurtful: true,  label: "empowered the nemesis" },
  nemsummon: { cost: 7,  hurtful: true,  label: "sent the nemesis hunting" },
  nemcrown:  { cost: 10, hurtful: true,  label: "crowned a nemesis" },
  curse:     { cost: 12, hurtful: true,  label: "rerolled the curse" },
};

// Friendly events (harmless/boons) cost less to summon.
const CALM_EVENT_COST = 3;

function clean(text, max) {
  return String(text ?? "")
    .replace(/[|\r\n\t]/g, " ")
    .replace(/[^\x20-\x7E]/g, "")
    .trim()
    .slice(0, max);
}

function cleanName(text) {
  return clean(text, 12).replace(/[^A-Za-z0-9 _-]/g, "") || "Friend";
}

async function sha256(text) {
  const data = new TextEncoder().encode(text);
  const hash = await crypto.subtle.digest("SHA-256", data);
  return [...new Uint8Array(hash)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export class Room extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.game = null;        // latest parsed state from the game
    this.gameSeen = 0;       // ms timestamp of the last game post
    this.queue = [];         // command lines waiting for the game
    this.feed = [];          // recent friend activity
    this.favor = {};         // name -> { favor, last }
    this.cooldowns = {};     // name -> ms of next allowed action
    this.vote = null;        // { options: [ids], votes: {name: i}, ends }
    this.nextVoteAt = Date.now() + 60 * 1000;
    this.lastBroadcast = 0;
    this.loaded = this.ctx.blockConcurrencyWhile(async () => {
      this.hostKeyHash = (await this.ctx.storage.get("hostKeyHash")) || null;
      this.favor = (await this.ctx.storage.get("favor")) || {};
      this.feed = (await this.ctx.storage.get("feed")) || [];
    });
  }

  async fetch(request) {
    await this.loaded;
    const url = new URL(request.url);
    if (url.pathname.endsWith("/game") && request.method === "POST") {
      return this.handleGame(request);
    }
    if (url.pathname.endsWith("/ws")) {
      if (request.headers.get("Upgrade") !== "websocket") {
        return new Response("Expected WebSocket", { status: 426 });
      }
      const pair = new WebSocketPair();
      this.ctx.acceptWebSocket(pair[1]);
      pair[1].serializeAttachment({ name: null });
      return new Response(null, { status: 101, webSocket: pair[0] });
    }
    if (url.pathname.endsWith("/peek")) {
      return Response.json({ online: this.gameOnline(), game: this.game, feed: this.feed, vote: this.vote });
    }
    return new Response("Not found", { status: 404 });
  }

  gameOnline() {
    return Date.now() - this.gameSeen < GAME_TIMEOUT_MS;
  }

  // -------------------------------------------------------------------------
  // Game side
  // -------------------------------------------------------------------------

  async handleGame(request) {
    const key = request.headers.get("X-Host-Key") || "";
    if (key.length < 4) {
      return new Response("ERR host key too short", { status: 403 });
    }
    const hash = await sha256(key);
    if (!this.hostKeyHash) {
      // First game to connect claims the room.
      this.hostKeyHash = hash;
      await this.ctx.storage.put("hostKeyHash", hash);
    } else if (this.hostKeyHash !== hash) {
      return new Response("ERR wrong host key", { status: 403 });
    }
    try {
      const text = await request.text();
      if (text.length > 64 * 1024) {
        return new Response("ERR too large", { status: 413 });
      }
      const state = JSON.parse(text);
      if (state && typeof state === "object" && state.v) {
        this.game = state;
      }
    } catch {
      // Keep the previous state if this one was malformed.
    }
    this.gameSeen = Date.now();
    this.tick();
    const lines = this.queue.splice(0, 8);
    this.broadcastState();
    return new Response(["OK", ...lines].join("\n"), {
      headers: { "Content-Type": "text/plain" },
    });
  }

  // Favor trickles in, votes open and close. Runs on every game post.
  tick() {
    const now = Date.now();
    const names = this.onlineNames();
    let changed = false;
    for (const name of names) {
      const f = (this.favor[name] ||= { favor: 3, last: now });
      if (now - f.last >= FAVOR_EVERY_MS) {
        const gained = Math.floor((now - f.last) / FAVOR_EVERY_MS);
        f.favor = Math.min(FAVOR_MAX, f.favor + gained);
        f.last += gained * FAVOR_EVERY_MS;
        changed = true;
      }
    }
    if (changed) {
      this.ctx.storage.put("favor", this.favor);
    }
    if (!this.vote && now >= this.nextVoteAt && names.length > 0 && this.game?.events && this.game.allowed) {
      this.openVote();
    }
    if (this.vote && now >= this.vote.ends) {
      this.closeVote();
    }
  }

  openVote() {
    const events = this.game.events
      .map((e, i) => ({ i, name: e[0], kind: e[1], enabled: e[3] }))
      .filter((e) => e.enabled && (this.game.hurtful || e.kind !== 2));
    if (events.length < 3) {
      this.nextVoteAt = Date.now() + VOTE_EVERY_MS;
      return;
    }
    const picks = [];
    while (picks.length < 3) {
      const e = events[Math.floor(Math.random() * events.length)];
      if (!picks.includes(e.i)) picks.push(e.i);
    }
    this.vote = { options: picks, votes: {}, ends: Date.now() + VOTE_LENGTH_MS };
    this.addFeed("The Moon Court is voting on the next event!");
  }

  closeVote() {
    const counts = [0, 0, 0];
    for (const v of Object.values(this.vote.votes)) counts[v]++;
    const best = Math.max(...counts);
    if (best > 0) {
      const winners = counts.map((c, i) => (c === best ? i : -1)).filter((i) => i >= 0);
      const pick = this.vote.options[winners[Math.floor(Math.random() * winners.length)]];
      this.queue.push(`EVENT|${pick}|The Court`);
      this.addFeed(`The Court chose: ${this.game?.events?.[pick]?.[0] ?? "an event"}`);
    } else {
      this.addFeed("Nobody voted. The moon grows bored.");
    }
    this.vote = null;
    this.nextVoteAt = Date.now() + VOTE_EVERY_MS;
  }

  addFeed(text) {
    this.feed.unshift({ at: Date.now(), text: clean(text, 90) });
    this.feed = this.feed.slice(0, 30);
    this.ctx.storage.put("feed", this.feed);
  }

  // -------------------------------------------------------------------------
  // Friend side
  // -------------------------------------------------------------------------

  onlineNames() {
    const names = new Set();
    for (const ws of this.ctx.getWebSockets()) {
      const a = ws.deserializeAttachment();
      if (a?.name) names.add(a.name);
    }
    return [...names];
  }

  costOf(action, params) {
    const def = ACTIONS[action];
    if (!def) return null;
    let cost = def.cost;
    if (action === "event") {
      const kind = this.game?.events?.[params.id]?.[1];
      if (kind !== 2) cost = CALM_EVENT_COST;
    }
    if (this.game?.crowd) cost = Math.max(1, Math.ceil(cost / 2)); // Curse of the Crowd
    return cost;
  }

  costTable() {
    const out = {};
    for (const k of Object.keys(ACTIONS)) out[k] = this.costOf(k, {});
    out.calmEvent = this.game?.crowd ? Math.ceil(CALM_EVENT_COST / 2) : CALM_EVENT_COST;
    return out;
  }

  snapshotFor(name) {
    return {
      t: "state",
      you: name,
      online: this.gameOnline(),
      game: this.game,
      favor: name ? Math.floor(this.favor[name]?.favor ?? 0) : 0,
      favorMax: FAVOR_MAX,
      cooldown: name ? Math.max(0, (this.cooldowns[name] || 0) - Date.now()) : 0,
      friends: this.onlineNames(),
      feed: this.feed.slice(0, 15),
      vote: this.vote && {
        options: this.vote.options.map((i) => ({ id: i, name: this.game?.events?.[i]?.[0] ?? "?" })),
        counts: this.vote.options.map((_, idx) => Object.values(this.vote.votes).filter((v) => v === idx).length),
        mine: name ? this.vote.votes[name] ?? null : null,
        endsIn: Math.max(0, this.vote.ends - Date.now()),
      },
      costs: this.costTable(),
    };
  }

  broadcastState(force = false) {
    const now = Date.now();
    if (!force && now - this.lastBroadcast < 900) return;
    this.lastBroadcast = now;
    for (const ws of this.ctx.getWebSockets()) {
      const a = ws.deserializeAttachment();
      try {
        ws.send(JSON.stringify(this.snapshotFor(a?.name)));
      } catch {
        // socket is closing
      }
    }
  }

  reply(ws, ok, text) {
    try {
      ws.send(JSON.stringify({ t: "toast", ok, text }));
    } catch {}
  }

  async webSocketMessage(ws, raw) {
    await this.loaded;
    let msg;
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }
    const att = ws.deserializeAttachment() || {};

    if (msg.t === "hello") {
      const name = cleanName(msg.name);
      ws.serializeAttachment({ name, actions: [] });
      this.favor[name] ||= { favor: 3, last: Date.now() };
      this.addFeed(`${name} joined the Moon Court.`);
      ws.send(JSON.stringify(this.snapshotFor(name)));
      this.broadcastState(true);
      return;
    }
    if (!att.name) return;
    const name = att.name;

    if (msg.t === "vote") {
      if (this.vote && [0, 1, 2].includes(msg.i)) {
        this.vote.votes[name] = msg.i;
        this.broadcastState(true);
      }
      return;
    }
    if (msg.t !== "act") return;

    // Basic flood protection: 5 actions per 10 seconds per socket.
    const now = Date.now();
    att.actions = (att.actions || []).filter((t) => now - t < 10000);
    if (att.actions.length >= 5) {
      return this.reply(ws, false, "Slow down a little!");
    }
    att.actions.push(now);
    ws.serializeAttachment(att);

    const result = this.performAction(name, msg.a, msg.p || {});
    this.reply(ws, result.ok, result.text);
    this.broadcastState(true);
  }

  performAction(name, action, p) {
    const def = ACTIONS[action];
    const g = this.game;
    if (!def) return { ok: false, text: "Unknown action." };
    if (!this.gameOnline() || !g) return { ok: false, text: "The hero isn't playing right now." };
    if (!g.allowed) return { ok: false, text: "The hero has blocked meddling." };
    const hurtful = action === "event" ? g.events?.[parseInt(p.id, 10)]?.[1] === 2 : def.hurtful;
    if (hurtful && !g.hurtful) return { ok: false, text: "The hero only allows helpful meddling." };
    if (g.dead) return { ok: false, text: "The hero is dead. Show some respect." };

    const now = Date.now();
    const cdMs = (g.cooldown ?? 20) * 1000 * (g.crowd ? 0.5 : 1);
    if (action !== "message" && (this.cooldowns[name] || 0) > now) {
      return { ok: false, text: `Wait ${Math.ceil((this.cooldowns[name] - now) / 1000)}s.` };
    }
    const cost = this.costOf(action, p);
    const f = (this.favor[name] ||= { favor: 0, last: now });
    if (f.favor < cost) return { ok: false, text: `Needs ${cost} favor.` };

    let line = null;
    let detail = "";
    const int = (v, lo, hi) => Math.max(lo, Math.min(hi, parseInt(v, 10) || 0));

    switch (action) {
      case "heal":
        line = `HEAL|4|${name}`;
        break;
      case "shield":
        line = `SHIELD|6|${name}`;
        break;
      case "gift": {
        const item = int(p.item, 0, (g.items?.length ?? 24) - 1);
        line = `GIFT|${item}|${name}`;
        detail = `: ${g.items?.[item] ?? "item"}`;
        break;
      }
      case "marks":
        line = `MARKS|5|${name}`;
        break;
      case "message": {
        const text = clean(p.text, 60);
        if (!text) return { ok: false, text: "Say something!" };
        line = `MSG|${name}|${text}`;
        detail = `: "${text}"`;
        break;
      }
      case "nemweak":
        if (!g.nemesis) return { ok: false, text: "There is no nemesis." };
        line = `NEMWEAK|${int(p.stat, 0, 3)}|${name}`;
        break;
      case "nembuff":
        if (!g.nemesis) return { ok: false, text: "There is no nemesis." };
        line = `NEMBUFF|${int(p.stat, 0, 3)}|${name}`;
        break;
      case "nemsummon":
        if (!g.nemesis) return { ok: false, text: "There is no nemesis." };
        line = `NEMSUMMON|${name}`;
        break;
      case "nemcrown": {
        if (g.nemesis) return { ok: false, text: "A nemesis already hunts the hero." };
        const s = int(p.species, 0, (g.species?.length ?? 1) - 1);
        line = `NEMCROWN|${s}|${name}`;
        detail = `: ${g.species?.[s]?.[0] ?? ""}`;
        break;
      }
      case "rename":
        if (!g.nemesis) return { ok: false, text: "There is no nemesis." };
        line = `RENAME|${int(p.a, 0, 23)}|${int(p.b, 0, 23)}|${name}`;
        break;
      case "bounty": {
        const s = int(p.species, 0, (g.species?.length ?? 1) - 1);
        const z = int(p.zone, 0, (g.zones?.length ?? 1) - 1);
        const diff = g.species?.[s]?.[1] ?? 3;
        const reward = 5 + diff * 6;
        line = `BOUNTY|${s}|${z}|${reward}|${name}`;
        detail = `: ${g.species?.[s]?.[0] ?? ""} in ${g.zones?.[z] ?? "Termina"}`;
        break;
      }
      case "event": {
        if (g.event) return { ok: false, text: "An event is already underway." };
        const id = int(p.id, 0, (g.events?.length ?? 32) - 1);
        const ev = g.events?.[id];
        if (!ev || !ev[3]) return { ok: false, text: "That event is disabled." };
        if (ev[1] === 2 && !g.hurtful) return { ok: false, text: "The hero only allows helpful meddling." };
        line = `EVENT|${id}|${name}`;
        detail = `: ${ev[0]}`;
        break;
      }
      case "randevent":
        if (g.event) return { ok: false, text: "An event is already underway." };
        line = `EVENT|-1|${name}`;
        break;
      case "chill":
        line = `CHILL|5|${name}`;
        break;
      case "curse":
        line = `CURSE|${name}`;
        break;
    }
    if (!line) return { ok: false, text: "Nothing happened." };
    if (this.queue.length > 20) return { ok: false, text: "The hero is overwhelmed. Try again soon." };

    f.favor -= cost;
    this.ctx.storage.put("favor", this.favor);
    if (action !== "message") this.cooldowns[name] = now + cdMs;
    this.queue.push(line);
    this.addFeed(`${name} ${def.label}${detail}`);
    return { ok: true, text: `Done! (-${cost} favor)` };
  }

  async webSocketClose(ws) {
    const att = ws.deserializeAttachment();
    if (att?.name) {
      this.addFeed(`${att.name} left.`);
    }
    this.broadcastState(true);
  }

  async webSocketError(ws) {
    this.broadcastState(true);
  }
}
