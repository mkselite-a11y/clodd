// Moon Court client: one WebSocket to the room's Durable Object.
(() => {
  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  // Same name lists as the mod (nemesis.c), used by the rename action.
  const NAMES_A = ["Grak", "Vorn", "Skrell", "Ulgoth", "Maz", "Kethra", "Brum", "Ziv", "Horga", "Drel", "Vasko", "Tuk",
    "Oona", "Ragnor", "Pell", "Syx", "Gorm", "Irla", "Kaz", "Mordu", "Fenn", "Zorba", "Quill", "Hask"];
  const NAMES_B = ["the Unbroken", "Moonbitten", "Skullcleaver", "the Patient", "Ashmaw", "the Grinning", "Nightstalker",
    "the Twice-Dead", "Gloomfang", "the Collector", "Bonegnaw", "the Laughing", "Stormhide", "the Hungry",
    "Duskwalker", "the Scarred", "Ironjaw", "the Whisper", "Blightclaw", "the Relentless", "the Mirthless",
    "Cinderheart", "the Masked", "Frostmourn"];
  const STAT_NAMES = ["Health", "Power", "Tracking", "Speed"];
  const KIND = ["calm", "good", "danger"];
  const KIND_LABEL = ["Harmless", "Boon", "Danger"];

  let ws = null;
  let room = "";
  let name = "";
  let snap = null;
  let catalogKey = "";
  let retry = 0;

  // ---------------------------------------------------------------- join
  try {
    $("roomInput").value = localStorage.getItem("mc_room") || new URLSearchParams(location.search).get("room") || "";
    $("nameInput").value = localStorage.getItem("mc_name") || "";
  } catch {}

  $("joinForm").addEventListener("submit", (e) => {
    e.preventDefault();
    room = $("roomInput").value.trim().toUpperCase().replace(/[^A-Z0-9_-]/g, "");
    name = $("nameInput").value.trim().replace(/[^A-Za-z0-9 _-]/g, "").slice(0, 12);
    if (!room || !name) return;
    try {
      localStorage.setItem("mc_room", room);
      localStorage.setItem("mc_name", name);
    } catch {}
    $("join").classList.add("hidden");
    $("app").classList.remove("hidden");
    $("roomLabel").textContent = `Room ${room}`;
    $("roomLabel").classList.remove("hidden");
    history.replaceState(null, "", `?room=${encodeURIComponent(room)}`);
    connect();
  });

  function connect() {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    ws = new WebSocket(`${proto}://${location.host}/api/room/${encodeURIComponent(room)}/ws`);
    setPill("warn", "Connecting...");
    ws.onopen = () => {
      retry = 0;
      ws.send(JSON.stringify({ t: "hello", name }));
    };
    ws.onmessage = (ev) => {
      let msg;
      try { msg = JSON.parse(ev.data); } catch { return; }
      if (msg.t === "state") render(msg);
      if (msg.t === "toast") toast(msg.text, msg.ok);
    };
    ws.onclose = () => {
      setPill("off", "Reconnecting...");
      setTimeout(connect, Math.min(10000, 1000 * 2 ** retry++));
    };
  }

  function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
  }

  function setPill(kind, text) {
    const p = $("connPill");
    p.className = `pill ${kind}`;
    p.textContent = text;
  }

  function toast(text, ok = true) {
    const el = document.createElement("div");
    el.className = `toast ${ok ? "" : "bad"}`;
    el.textContent = text;
    $("toasts").appendChild(el);
    setTimeout(() => el.remove(), 3500);
  }

  // ---------------------------------------------------------------- render
  function heartSvg(fill) {
    // fill: 0..4 quarters
    const pct = (fill / 4) * 100;
    const id = `h${Math.random().toString(36).slice(2, 8)}`;
    return `<svg viewBox="0 0 24 24" aria-hidden="true"><defs><linearGradient id="${id}"><stop offset="${pct}%" stop-color="#ff4d6d"/><stop offset="${pct}%" stop-color="#3a2c5c"/></linearGradient></defs><path fill="url(#${id})" stroke="#1b0f2e" stroke-width="1.2" d="M12 21s-7.5-4.6-9.6-9.2C.9 8.4 3 4.5 6.8 4.5c2.2 0 3.6 1.3 5.2 3.2 1.6-1.9 3-3.2 5.2-3.2 3.8 0 5.9 3.9 4.4 7.3C19.5 16.4 12 21 12 21z"/></svg>`;
  }

  function renderHearts(hp, max) {
    const hearts = Math.max(1, Math.round(max / 16));
    let html = "";
    for (let i = 0; i < hearts; i++) {
      const q = Math.max(0, Math.min(4, Math.floor((hp - i * 16) / 4)));
      html += heartSvg(q);
    }
    $("hearts").innerHTML = html;
  }

  function fillCatalog(g) {
    const key = `${g.events?.length}-${g.items?.length}-${g.species?.length}-${g.zones?.length}-${(g.events || []).map((e) => e[3]).join("")}`;
    if (key === catalogKey) return;
    catalogKey = key;
    $("giftSelect").innerHTML = (g.items || []).map((n, i) => `<option value="${i}">${esc(n)}</option>`).join("");
    $("eventSelect").innerHTML = (g.events || [])
      .map((e, i) => (e[3] ? `<option value="${i}" data-kind="${e[1]}">${esc(e[0])} (${KIND_LABEL[e[1]]})</option>` : ""))
      .join("");
    $("crownSelect").innerHTML = (g.species || [])
      .map((s, i) => `<option value="${i}">${esc(s[0])} (difficulty ${s[1]})</option>`).join("");
    $("bountySpecies").innerHTML = $("crownSelect").innerHTML;
    $("bountyZone").innerHTML = (g.zones || []).map((z, i) => `<option value="${i}">${esc(z)}</option>`).join("");
    $("nameA").innerHTML = NAMES_A.map((n, i) => `<option value="${i}">${esc(n)}</option>`).join("");
    $("nameB").innerHTML = NAMES_B.map((n, i) => `<option value="${i}">${esc(n)}</option>`).join("");
    updateBountyReward();
  }

  function updateBountyReward() {
    const s = snap?.game?.species?.[+$("bountySpecies").value];
    $("bountyReward").textContent = s ? `Pays about ${5 + s[1] * 6} Moon Marks` : "";
  }
  $("bountySpecies").addEventListener("change", updateBountyReward);

  function render(s) {
    snap = s;
    const g = s.game;
    if (!s.online || !g) {
      setPill("warn", g ? "Hero offline" : "Waiting for the hero");
    } else {
      setPill("on", "Live");
    }
    if (!g) return;
    fillCatalog(g);

    renderHearts(g.hp, g.hpMax);
    $("marks").textContent = g.marks;
    $("zone").textContent = g.zone === "Indoors" ? `Indoors (near ${g.lastZone})` : g.zone;
    $("clock").textContent = `Day ${g.day}, ${g.time}`;
    $("form").textContent = g.form;
    $("heroStatus").textContent = g.dead ? "The hero has fallen..." : g.menu ? "The hero is browsing the Moon Menu." : "";

    // Event
    const ec = $("eventCard");
    if (g.event) {
      ec.classList.add("active");
      $("eventName").textContent = g.event.name + (g.event.by ? ` (from ${g.event.by})` : "");
      $("eventCounter").textContent = g.event.counter;
      $("eventBadge").textContent = `${KIND_LABEL[g.event.kind]} - ${g.event.left}s`;
      $("eventBadge").className = `badge ${KIND[g.event.kind]}`;
      const total = Math.max(g.event.left, 30);
      $("eventBar").style.width = `${(g.event.left / total) * 100}%`;
    } else {
      ec.classList.remove("active");
      $("eventName").textContent = "Quiet, for now...";
      $("eventCounter").textContent = g.nextEvent > 0 ? `The moon stirs again in about ${g.nextEvent}s.` : "";
      $("eventBadge").textContent = "";
      $("eventBadge").className = "badge";
      $("eventBar").style.width = "0";
    }

    // Nemesis
    const n = g.nemesis;
    if (n) {
      $("nemBadge").textContent = n.present ? "Fighting now!" : n.state;
      $("nemBadge").className = `badge ${n.present ? "danger" : ""}`;
      $("nemesis").innerHTML = `
        <p class="nem-name">${esc(n.name)}</p>
        <p class="muted">Lv ${n.level} ${esc(n.species)} - last seen in ${esc(n.zone)} - killed the hero ${n.kills}x</p>
        <div class="stats">${n.stats.map((v, i) => `
          <div class="stat"><span>${STAT_NAMES[i]}</span><div class="track"><div class="fill" style="width:${Math.min(100, v * 10)}%"></div></div><b>${v}</b></div>`).join("")}
        </div>
        <div class="chips">${n.traits.map((t) => `<span class="chip">${esc(t)}</span>`).join("") || '<span class="muted small">No traits yet</span>'}</div>`;
    } else {
      $("nemBadge").textContent = "";
      $("nemesis").innerHTML = '<p class="muted">No enemy has earned the title yet. Crown one from the Nemesis tab.</p>';
    }

    // Curse
    $("curseName").textContent = g.curse ? g.curse.name + (g.curse.warded ? " (warded)" : "") : "-";
    $("curseDesc").textContent = g.curse ? g.curse.desc : "";

    // Bounties
    $("bounties").innerHTML = (g.bounties || []).map((b) => `
      <li class="${b.claimed ? "claimed" : ""}">
        <span><span class="rank">${esc(b.rank)}</span>${esc(b.name)}<br><small class="muted">${esc(b.zone)}${b.claimed ? " - claimed" : ""}${b.sponsor ? ` - by ${esc(b.sponsor)}` : ""}</small></span>
        <span class="r">${b.reward} MM</span>
      </li>`).join("");

    $("chronicle").innerHTML = (g.log || []).map((l) => `<li>${esc(l)}</li>`).join("");

    // Favor
    $("favorNum").textContent = s.favor;
    $("favorPips").innerHTML = Array.from({ length: s.favorMax }, (_, i) => `<span class="${i < s.favor ? "on" : ""}"></span>`).join("");
    $("cooldown").textContent = s.cooldown > 0 ? `Your next action is ready in ${Math.ceil(s.cooldown / 1000)}s.` : "You can act now.";

    // Costs & availability
    document.querySelectorAll("[data-cost]").forEach((el) => {
      el.textContent = `${s.costs[el.dataset.cost] ?? "?"} favor`;
    });
    const evSel = $("eventSelect").selectedOptions[0];
    const evKind = evSel ? +evSel.dataset.kind : 2;
    $("eventCost").textContent = `${evKind === 2 ? s.costs.event : s.costs.calmEvent} favor`;
    $("hurtNote").classList.toggle("hidden", !!g.hurtful);
    document.querySelectorAll(".act[data-a]").forEach((b) => {
      const a = b.dataset.a;
      const cost = a === "event" ? (evKind === 2 ? s.costs.event : s.costs.calmEvent) : s.costs[a];
      const hurt = b.classList.contains("hurt") || (a === "event" && evKind === 2);
      b.disabled = !s.online || !g.allowed || (hurt && !g.hurtful) || s.favor < cost || (s.cooldown > 0 && a !== "message");
    });

    // Vote
    const v = s.vote;
    $("voteCard").classList.toggle("hidden", !v);
    if (v) {
      const total = v.counts.reduce((a, b) => a + b, 0) || 1;
      $("voteTimer").textContent = `${Math.ceil(v.endsIn / 1000)}s`;
      $("voteOptions").innerHTML = v.options.map((o, i) => `
        <button class="act ${v.mine === i ? "mine" : ""}" data-vote="${i}">
          <span class="fillbar" style="width:${(v.counts[i] / total) * 100}%"></span>
          <b>${esc(o.name)}</b><span>${v.counts[i]} vote${v.counts[i] === 1 ? "" : "s"}</span><i>${v.mine === i ? "Your pick" : "Vote"}</i>
        </button>`).join("");
    }

    // Friends & feed
    $("friends").innerHTML = (s.friends || []).map((f) => `<span class="chip">${esc(f)}${f === s.you ? " (you)" : ""}</span>`).join("");
    $("feed").innerHTML = (s.feed || []).map((f) => `<li>${esc(f.text)}</li>`).join("");
  }

  // ---------------------------------------------------------------- actions
  document.querySelectorAll(".tab").forEach((t) => {
    t.addEventListener("click", () => {
      document.querySelectorAll(".tab").forEach((x) => x.classList.toggle("active", x === t));
      document.querySelectorAll(".panel").forEach((p) => p.classList.toggle("hidden", p.dataset.panel !== t.dataset.tab));
    });
  });

  $("eventSelect").addEventListener("change", () => snap && render(snap));

  document.addEventListener("click", (e) => {
    const vote = e.target.closest("[data-vote]");
    if (vote) {
      send({ t: "vote", i: +vote.dataset.vote });
      return;
    }
    const btn = e.target.closest(".act[data-a]");
    if (!btn || btn.disabled || btn.type === "submit") return;
    const a = btn.dataset.a;
    const p = {};
    if (a === "gift") p.item = +$("giftSelect").value;
    if (a === "nemweak") p.stat = +$("weakSelect").value;
    if (a === "nembuff") p.stat = +$("buffSelect").value;
    if (a === "nemcrown") p.species = +$("crownSelect").value;
    if (a === "event") p.id = +$("eventSelect").value;
    if (a === "rename") { p.a = +$("nameA").value; p.b = +$("nameB").value; }
    if (a === "bounty") { p.species = +$("bountySpecies").value; p.zone = +$("bountyZone").value; }
    send({ t: "act", a, p });
  });

  $("msgForm").addEventListener("submit", (e) => {
    e.preventDefault();
    const text = $("msgInput").value.trim();
    if (!text) return;
    send({ t: "act", a: "message", p: { text } });
    $("msgInput").value = "";
  });

  // Tick the visible countdowns between server updates.
  setInterval(() => {
    if (!snap) return;
    if (snap.vote) snap.vote.endsIn = Math.max(0, snap.vote.endsIn - 1000);
    if (snap.cooldown > 0) snap.cooldown = Math.max(0, snap.cooldown - 1000);
  }, 1000);
})();
