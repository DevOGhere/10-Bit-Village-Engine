// 10-Bit Village — local replay viewer (v1 stage 2). Standalone, file:// only,
// never touches the engine. Loads a replay.json exported by scripts/export_replay.py.

const TYPE_NAMES = ["EXPERIENCE", "HEARSAY", "DREAM"];
const TILE = 16;

// Sprite assets (sliced from sprites/Gemini_Generated_Image_u06tuyu06tuyu06t.png, generated
// 2026-06-15, never wired in until now). Color assigned by villager_id, not genome -- genome
// isn't exported to replay.json/the /ws feed today; wiring that through is future work.
const SPRITE_COLORS = ["green", "blue", "red", "yellow", "purple"];
const villagerSprites = {};
const eatingSprites = {};
let grassTile = null;
let grassPattern = null;
// Draw itself runs continuously in frame() regardless of load state (checks .complete each
// frame and falls back to a dot until then), so these onload handlers only need to update
// state, not force a redraw.
for (const c of SPRITE_COLORS) {
    const img = new Image();
    img.src = `sprites/villager_${c}_idle.png`;
    villagerSprites[c] = img;
}
// only 4/5 colors have a distinct eating pose in the source sheet (purple's slot is a
// mislabeled duplicate of idle) -- villagerSprites[c] is the honest fallback for purple
for (const c of ["green", "blue", "red", "yellow"]) {
    const img = new Image();
    img.src = `sprites/eating_${c}.png`;
    eatingSprites[c] = img;
}
{
    const img = new Image();
    img.onload = () => { grassTile = img; };
    img.src = "sprites/terrain_grass.png";
}

// Event-type cue icons (small floating overlays -- "small visual cues" section of the
// sprite sheet). Shown briefly above a villager when a new event involving them lands,
// so a viewer can actually see cognition happening instead of just a static crowd.
const CUE_ICONS = {};
for (const [type, file] of [["EXPERIENCE", "cue_thought.png"], ["HEARSAY", "cue_speech.png"], ["DREAM", "cue_dream.png"]]) {
    const img = new Image();
    img.src = `sprites/${file}`;
    CUE_ICONS[type] = img;
}
const CUE_DURATION_MS = 4000;
const activeCues = new Map(); // vid -> { type, expiresAt }

// Continuous per-villager visual state, decoupled from the sim's real (sparse) position
// updates -- real movement is genuinely rare (1 cognition dispatch per villager per ~100
// ticks, ~10s/tick on live infra = ~16min between a given villager's turns), which reads as
// a dead crowd. visualPos eases toward the real target instead of teleporting, and idleBob
// gives every villager a small constant sway so the tank always looks alive even between
// real updates. Purely cosmetic -- never written back, never confused with real state.
const visualPos = new Map(); // vid -> { x, y } in tile units (floating)

let data = null;
let positionsByTick = new Map();
let eventsSorted = [];
let coinTerms = [];
let currentTick = 0;
let playing = false;
let ticksPerSecond = 5;
let lastFrameTime = 0;
let accumulator = 0;
let renderedLogCount = 0; // live mode only: how many of eventsSorted are already in the DOM

const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const fileInput = document.getElementById("fileInput");
const playBtn = document.getElementById("playBtn");
const scrub = document.getElementById("scrub");
const tickLabel = document.getElementById("tickLabel");
const speedRange = document.getElementById("speedRange");
const speedLabel = document.getElementById("speedLabel");
const logEl = document.getElementById("log");
const coinListEl = document.getElementById("coinList");
const liveUrlEl = document.getElementById("liveUrl");
const liveBtn = document.getElementById("liveBtn");
const liveStatusEl = document.getElementById("liveStatus");
const controlsEl = document.getElementById("controls");

// Step 7 fish tank: same renderer as the static file:// viewer, fed from a WebSocket
// instead of a static replay.json. Grid size is the engine's fixed constant (32x24,
// packet 052) -- the live feed doesn't send it, there's only ever one grid shape.
const LIVE_GRID_W = 32;
const LIVE_GRID_H = 24;
const LIVE_HISTORY_TICKS = 500; // cap kept ticks so a long-running spectator tab doesn't leak memory
let liveSocket = null;
let liveMode = false;

function connectLive(url) {
    if (!url) return;
    liveUrlEl.value = url;
    liveMode = true;
    playing = false;
    playBtn.textContent = "Play";
    // Play/scrub/speed are the static-replay scrubber -- in live mode the WS feed forces
    // currentTick to the newest tick on every message anyway, so they'd just fight the live
    // feed and do nothing coherent. Hide them rather than ship a control that looks
    // interactive but silently does nothing.
    controlsEl.style.display = "none";
    data = { meta: { grid_w: LIVE_GRID_W, grid_h: LIVE_GRID_H, ticks: 1 }, positions: [], events: [], coinages: [] };
    positionsByTick = new Map();
    eventsSorted = [];
    coinTerms = [];
    renderedLogCount = 0;
    logEl.innerHTML = "";
    visualPos.clear();
    activeCues.clear();
    renderCoinList();
    liveStatusEl.textContent = "connecting...";
    liveBtn.textContent = "Disconnect";
    liveSocket = new WebSocket(url);
    liveSocket.onopen = () => { liveStatusEl.textContent = "connected"; };
    liveSocket.onclose = () => { liveStatusEl.textContent = "disconnected"; liveSocket = null; liveBtn.textContent = "Connect"; };
    liveSocket.onerror = () => { liveStatusEl.textContent = "error"; };
    liveSocket.onmessage = (ev) => {
        const msg = JSON.parse(ev.data);
        if (msg.heartbeat) return; // no tick change, nothing to render
        const tick = msg.tick;
        if (msg.positions.length > 0) {
            positionsByTick.set(tick, msg.positions.map(
                ([vid, x, y, holding, hunger, social, safety]) => ({ vid, x, y, holding, hunger, social, safety })
            ));
        }
        // Events keep their OWN tick (fold-back catch-up can deliver several past ticks'
        // worth at once) -- do NOT assume they belong to msg.tick, same shape as replay.json.
        for (const ev of msg.events) eventsSorted.push(ev);
        if (positionsByTick.size > LIVE_HISTORY_TICKS) {
            const oldest = Math.min(...positionsByTick.keys());
            positionsByTick.delete(oldest);
        }
        data.meta.ticks = tick + 1;
        currentTick = tick;
        scrub.max = tick;
        render();
    };
}

liveBtn.addEventListener("click", () => {
    if (liveSocket) {
        liveSocket.close();
        return;
    }
    connectLive(liveUrlEl.value.trim());
});

// Auto-connect when this page is actually served BY the Space (supervisor.py's static
// fallback), not opened as a local file:// -- same-origin, so no URL to type. The static
// viewer (file://) keeps the manual "Connect" flow, since there's no meaningful same-origin
// WS target there.
if (location.protocol !== "file:") {
    const scheme = location.protocol === "https:" ? "wss:" : "ws:";
    connectLive(`${scheme}//${location.host}/ws`);
}

fileInput.addEventListener("change", (e) => {
    const file = e.target.files[0];
    if (!file) return;
    if (liveSocket) liveSocket.close();
    liveMode = false;
    controlsEl.style.display = "";
    const reader = new FileReader();
    reader.onload = (ev) => {
        data = JSON.parse(ev.target.result);
        load();
    };
    reader.readAsText(file);
});

playBtn.addEventListener("click", () => {
    playing = !playing;
    playBtn.textContent = playing ? "Pause" : "Play";
    lastFrameTime = performance.now();
});

scrub.addEventListener("input", () => {
    currentTick = parseInt(scrub.value, 10);
    render();
});

speedRange.addEventListener("input", () => {
    ticksPerSecond = parseFloat(speedRange.value);
    speedLabel.textContent = ticksPerSecond.toFixed(1) + " ticks/s";
});

function load() {
    canvas.width = data.meta.grid_w * TILE;
    canvas.height = data.meta.grid_h * TILE;

    positionsByTick = new Map();
    for (const [tick, vid, x, y, holding, hunger, social, safety] of data.positions) {
        if (!positionsByTick.has(tick)) positionsByTick.set(tick, []);
        positionsByTick.get(tick).push({ vid, x, y, holding, hunger, social, safety });
    }

    eventsSorted = data.events; // already sorted by tick

    // Compute spread per coined term: distinct villager_ids whose event text
    // contains the term as a whole word (case-insensitive).
    coinTerms = data.coinages.map(([term, coiner, birth_tick]) => {
        const re = new RegExp("\\b" + term + "\\b", "i");
        const users = new Set();
        for (const [, vid, , , , , text] of eventsSorted) {
            if (re.test(text)) users.add(vid);
        }
        return { term, coiner, birth_tick, spread: users.size };
    });

    scrub.max = Math.max(0, data.meta.ticks - 1);
    currentTick = 0;
    renderCoinList();
    render();
}

function villagerColorName(vid) {
    return SPRITE_COLORS[vid % SPRITE_COLORS.length];
}

function drawGrid() {
    if (grassTile && grassTile.complete && grassTile.naturalWidth > 0) {
        if (!grassPattern) grassPattern = ctx.createPattern(grassTile, "repeat");
        ctx.fillStyle = grassPattern;
    } else {
        ctx.fillStyle = "#1c1f27"; // fallback until the sprite loads
    }
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = "rgba(0,0,0,0.15)";
    ctx.lineWidth = 1;
    for (let x = 0; x <= data.meta.grid_w; x++) {
        ctx.beginPath();
        ctx.moveTo(x * TILE + 0.5, 0);
        ctx.lineTo(x * TILE + 0.5, canvas.height);
        ctx.stroke();
    }
    for (let y = 0; y <= data.meta.grid_h; y++) {
        ctx.beginPath();
        ctx.moveTo(0, y * TILE + 0.5);
        ctx.lineTo(canvas.width, y * TILE + 0.5);
        ctx.stroke();
    }
}

const TWEEN_SPEED = 4; // tile-units/sec the visual position eases toward the real one
const BOB_AMPLITUDE_PX = 1.4;
const BOB_SPEED = 1 / 700; // radians/ms, per-vid phase offset keeps the crowd from bobbing in lockstep

function drawVillagers(now, dt) {
    const villagers = positionsByTick.get(currentTick) || [];
    // sprites are taller than one grid tile (character art, not a flat token) -- draw
    // oversized and anchor on the tile's bottom-center so they read as standing in the cell
    const spriteH = TILE * 1.6;
    const spriteW = spriteH * (140 / 105);
    const seenVids = new Set();
    for (const v of villagers) {
        seenVids.add(v.vid);
        let vp = visualPos.get(v.vid);
        if (!vp) {
            vp = { x: v.x, y: v.y };
            visualPos.set(v.vid, vp);
        }
        // ease toward the real (sparse) position instead of snapping -- makes the rare
        // real moves readable as motion instead of a teleport, purely cosmetic
        const step = TWEEN_SPEED * dt;
        vp.x += Math.max(-step, Math.min(step, v.x - vp.x));
        vp.y += Math.max(-step, Math.min(step, v.y - vp.y));

        const bob = Math.sin(now * BOB_SPEED + v.vid) * BOB_AMPLITUDE_PX;
        const cx = vp.x * TILE + TILE / 2;
        const cy = vp.y * TILE + TILE / 2 + bob;
        const colorName = villagerColorName(v.vid);
        const sprite = (v.holding && eatingSprites[colorName]) ? eatingSprites[colorName] : villagerSprites[colorName];
        if (sprite && sprite.complete && sprite.naturalWidth > 0) {
            ctx.drawImage(sprite, cx - spriteW / 2, cy - spriteH * 0.75, spriteW, spriteH);
        } else {
            // fallback dot until sprites load
            ctx.fillStyle = "#8a8a8a";
            ctx.beginPath();
            ctx.arc(cx, cy, TILE * 0.32, 0, Math.PI * 2);
            ctx.fill();
        }
        if (v.holding && !eatingSprites[colorName]) {
            // purple has no distinct eating pose in the source sheet -- keep the old dot cue
            ctx.fillStyle = "#ffd166";
            ctx.beginPath();
            ctx.arc(cx + TILE * 0.28, cy - TILE * 0.28, TILE * 0.12, 0, Math.PI * 2);
            ctx.fill();
        }

        const cue = activeCues.get(v.vid);
        if (cue) {
            if (now > cue.expiresAt) {
                activeCues.delete(v.vid);
            } else {
                const icon = CUE_ICONS[cue.type];
                const remaining = (cue.expiresAt - now) / CUE_DURATION_MS;
                ctx.globalAlpha = Math.min(1, remaining * 2.5); // hold, then fade in the last ~40%
                if (icon && icon.complete && icon.naturalWidth > 0) {
                    const iw = TILE * 1.3, ih = iw * (icon.naturalHeight / icon.naturalWidth);
                    ctx.drawImage(icon, cx - iw / 2, cy - spriteH * 0.75 - ih + 2, iw, ih);
                }
                ctx.globalAlpha = 1;
            }
        }
    }
    // drop tween state for villagers no longer in frame (avoids an unbounded map on long runs)
    for (const vid of visualPos.keys()) if (!seenVids.has(vid)) visualPos.delete(vid);
}

// Villager colors as real hex (not CSS color names) so the chat-avatar dot matches the
// sprite exactly. Approximate swatches pulled from the sprite art itself.
const VILLAGER_COLOR_HEX = { green: "#5fb878", blue: "#5b9bd5", red: "#d9635c", yellow: "#f0ad4e", purple: "#b07cd6" };
const TYPE_LABEL = { EXPERIENCE: "MOMENT", HEARSAY: "GOSSIP", DREAM: "DREAM" };

function buildEventNode(tick, vid, actor, type, depth, importance, text, currentTickHighlight) {
    // Chat-message shape instead of a debug-log line ("t136 v36 HEARSAY (depth 2, from v85)
    // imp 509" reads like a log dump, not something a casual viewer parses at a glance).
    // Internal engine metrics (depth/importance/tick) move to a hover tooltip instead of
    // being printed inline -- a group-chat viewer cares about who said what, not the
    // salience score behind it.
    const typeName = TYPE_NAMES[type] || "EXPERIENCE";
    const colorName = villagerColorName(vid);
    const div = document.createElement("div");
    div.className = "event " + typeName + (currentTickHighlight ? " current" : "");
    div.title = `tick ${tick} · importance ${importance}` + (typeName === "HEARSAY" ? ` · hop depth ${depth}` : "");

    const head = document.createElement("div");
    head.className = "eventHead";
    const avatar = document.createElement("span");
    avatar.className = "avatar";
    avatar.style.background = VILLAGER_COLOR_HEX[colorName] || "#888";
    const name = document.createElement("span");
    name.className = "name";
    name.textContent = `Villager ${vid}`;
    const badge = document.createElement("span");
    badge.className = "badge " + typeName;
    badge.textContent = TYPE_LABEL[typeName] || typeName;
    head.appendChild(avatar);
    head.appendChild(name);
    head.appendChild(badge);
    if (typeName === "HEARSAY") {
        const via = document.createElement("span");
        via.className = "via";
        via.textContent = `↺ retelling Villager ${actor}`;
        head.appendChild(via);
    }

    const body = document.createElement("div");
    body.className = "bubble";
    body.textContent = text;

    div.appendChild(head);
    div.appendChild(body);
    return div;
}

function renderEvents() {
    if (liveMode) {
        // Append-only: a full rebuild (innerHTML = "") on every WS message was the actual
        // complaint -- it wiped the panel and reset scroll position multiple times a
        // minute, so nothing was readable. Add only what's new, insert at the top (newest-
        // first), and never touch existing nodes -- scroll position naturally survives
        // because content below the insertion point doesn't move.
        const newOnes = eventsSorted.slice(renderedLogCount);
        for (const [tick, vid, actor, type, depth, importance, text] of newOnes) {
            const div = buildEventNode(tick, vid, actor, type, depth, importance, text, false);
            div.classList.add("new");
            logEl.insertBefore(div, logEl.firstChild);
            const typeName = TYPE_NAMES[type] || "EXPERIENCE";
            activeCues.set(vid, { type: typeName, expiresAt: performance.now() + CUE_DURATION_MS });
        }
        renderedLogCount = eventsSorted.length;
        while (logEl.children.length > 200) logEl.removeChild(logEl.lastChild);
        return;
    }
    // Static replay: currentTick can scrub in either direction, so append-only doesn't
    // apply -- rebuild the filtered view each time (unchanged from before).
    const visible = eventsSorted.filter((e) => e[0] <= currentTick).slice(-60).reverse();
    logEl.innerHTML = "";
    for (const [tick, vid, actor, type, depth, importance, text] of visible) {
        logEl.appendChild(buildEventNode(tick, vid, actor, type, depth, importance, text, tick === currentTick));
    }
}

function renderCoinList() {
    coinListEl.innerHTML = "";
    for (const c of coinTerms) {
        const div = document.createElement("div");
        div.className = "coin" + (c.spread >= 2 ? " spread" : "");
        div.innerHTML = `<span class="term">${c.term}</span> — coined by v${c.coiner} @t${c.birth_tick}, spread to ${c.spread} villager(s)`;
        coinListEl.appendChild(div);
    }
}

function render() {
    // Data-driven side (log/scrub/label) -- called when currentTick or the underlying data
    // actually changes. Canvas drawing is NOT here anymore: it needs to run every animation
    // frame (bob/tween are continuous), not just on data change, so it lives in frame() now.
    if (!data) return;
    renderEvents();
    scrub.value = currentTick;
    tickLabel.textContent = `tick ${currentTick} / ${data.meta.ticks - 1}`;
}

function frame(now) {
    const dt = Math.min(0.1, (now - lastFrameTime) / 1000); // clamp so a stalled tab doesn't jump-tween on return
    if (playing && data) {
        accumulator += dt * ticksPerSecond;
        while (accumulator >= 1) {
            accumulator -= 1;
            if (currentTick < data.meta.ticks - 1) {
                currentTick++;
            } else {
                playing = false;
                playBtn.textContent = "Play";
            }
        }
        render();
    }
    if (data) {
        drawGrid();
        drawVillagers(now, dt);
    }
    lastFrameTime = now;
    requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
