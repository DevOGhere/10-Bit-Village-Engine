// 10-Bit Village — local replay viewer (v1 stage 2). Standalone, file:// only,
// never touches the engine. Loads a replay.json exported by scripts/export_replay.py.

const TYPE_NAMES = ["EXPERIENCE", "HEARSAY", "DREAM"];
const TILE = 16;

// Sprite assets (sliced from sprites/Gemini_Generated_Image_u06tuyu06tuyu06t.png, generated
// 2026-06-15, never wired in until now). Color assigned by villager_id, not genome -- genome
// isn't exported to replay.json/the /ws feed today; wiring that through is future work.
const SPRITE_COLORS = ["green", "blue", "red", "yellow", "purple"];
const villagerSprites = {};
let grassTile = null;
let grassPattern = null;
for (const c of SPRITE_COLORS) {
    const img = new Image();
    img.onload = () => render(); // upgrade from the fallback dot even if view is paused
    img.src = `sprites/villager_${c}_idle.png`;
    villagerSprites[c] = img;
}
{
    const img = new Image();
    img.onload = () => { grassTile = img; render(); };
    img.src = "sprites/terrain_grass.png";
}

let data = null;
let positionsByTick = new Map();
let eventsSorted = [];
let coinTerms = [];
let currentTick = 0;
let playing = false;
let ticksPerSecond = 5;
let lastFrameTime = 0;
let accumulator = 0;

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
    data = { meta: { grid_w: LIVE_GRID_W, grid_h: LIVE_GRID_H, ticks: 1 }, positions: [], events: [], coinages: [] };
    positionsByTick = new Map();
    eventsSorted = [];
    coinTerms = [];
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

function drawVillagers() {
    const villagers = positionsByTick.get(currentTick) || [];
    // sprites are taller than one grid tile (character art, not a flat token) -- draw
    // oversized and anchor on the tile's bottom-center so they read as standing in the cell
    const spriteH = TILE * 1.6;
    const spriteW = spriteH * (140 / 105);
    for (const v of villagers) {
        const cx = v.x * TILE + TILE / 2;
        const cy = v.y * TILE + TILE / 2;
        const sprite = villagerSprites[villagerColorName(v.vid)];
        if (sprite && sprite.complete && sprite.naturalWidth > 0) {
            ctx.drawImage(sprite, cx - spriteW / 2, cy - spriteH * 0.75, spriteW, spriteH);
        } else {
            // fallback dot until sprites load
            ctx.fillStyle = "#8a8a8a";
            ctx.beginPath();
            ctx.arc(cx, cy, TILE * 0.32, 0, Math.PI * 2);
            ctx.fill();
        }
        if (v.holding) {
            ctx.fillStyle = "#ffd166";
            ctx.beginPath();
            ctx.arc(cx + TILE * 0.28, cy - TILE * 0.28, TILE * 0.12, 0, Math.PI * 2);
            ctx.fill();
        }
    }
}

function renderEvents() {
    // Show events up to currentTick, most recent ~60, newest first, current-tick highlighted.
    const visible = eventsSorted.filter((e) => e[0] <= currentTick).slice(-60).reverse();
    logEl.innerHTML = "";
    for (const [tick, vid, actor, type, depth, importance, text] of visible) {
        const div = document.createElement("div");
        const typeName = TYPE_NAMES[type] || "EXPERIENCE";
        div.className = "event " + typeName + (tick === currentTick ? " current" : "");
        const meta = document.createElement("div");
        meta.className = "meta";
        meta.textContent = `t${tick}  v${vid}  ${typeName}` +
            (typeName === "HEARSAY" ? ` (depth ${depth}, from v${actor})` : "") +
            `  imp ${importance}`;
        const body = document.createElement("div");
        body.textContent = text;
        div.appendChild(meta);
        div.appendChild(body);
        logEl.appendChild(div);
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
    if (!data) return;
    drawGrid();
    drawVillagers();
    renderEvents();
    scrub.value = currentTick;
    tickLabel.textContent = `tick ${currentTick} / ${data.meta.ticks - 1}`;
}

function frame(now) {
    if (playing && data) {
        const dt = (now - lastFrameTime) / 1000;
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
    lastFrameTime = now;
    requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
