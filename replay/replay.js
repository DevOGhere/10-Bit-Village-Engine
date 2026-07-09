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
let dirtTile = null;
let dirtPattern = null;
let waterTile = null;
let waterPattern = null;
let treeImg = null;
let bushImg = null;
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
{
    const img = new Image();
    img.onload = () => { dirtTile = img; };
    img.src = "sprites/terrain_dirt.png";
}
{
    const img = new Image();
    img.onload = () => { waterTile = img; };
    img.src = "sprites/terrain_water.png";
}
{
    const img = new Image();
    img.src = "sprites/obj_tree.png";
    treeImg = img;
}
{
    const img = new Image();
    img.src = "sprites/obj_bush.png";
    bushImg = img;
}

// Static terrain layout (client-only decoration -- the engine's grid has no terrain concept,
// so none of this is ever fed back or read by the sim; a road/pond/treeline just gives the
// tank a sense of place instead of a flat green void). Grid is always 32x24 (packet 052).
const DIRT_ROWS = new Set([11, 12]); // a road cutting across the village
const WATER_CELLS = new Set();
for (const [wy, xs] of [[18, [27, 28, 29]], [19, [26, 27, 28, 29, 30]], [20, [26, 27, 28, 29, 30]],
                         [21, [26, 27, 28, 29, 30]], [22, [27, 28, 29]]]) {
    for (const wx of xs) WATER_CELLS.add(wx + "," + wy);
}
const TREE_CELLS = [[2, 2], [4, 1], [5, 3], [2, 5], [6, 4], [3, 6]];
const BUSH_CELLS = [[8, 15], [10, 18], [13, 16], [22, 3], [24, 6], [20, 15]];

function terrainAt(x, y) {
    if (WATER_CELLS.has(x + "," + y)) return "water";
    if (DIRT_ROWS.has(y)) return "dirt";
    return "grass";
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
const statusBarEl = document.getElementById("statusBar");
const statusWorldEl = document.getElementById("statusWorld");
const titleEl = document.getElementById("title");
const taglineEl = document.getElementById("tagline");
const connDotEl = document.getElementById("connDot");
const devControlsEl = document.getElementById("devControls");
const pulseEl = document.getElementById("pulse");
const pulseCountsEl = document.getElementById("pulseCounts");
const pulseTopEl = document.getElementById("pulseTop");
const helpBtn = document.getElementById("helpBtn");
const introOverlayEl = document.getElementById("introOverlay");
const introDismissBtn = document.getElementById("introDismiss");
const focusCardEl = document.getElementById("focusCard");
const focusAvatarEl = document.getElementById("focusAvatar");
const focusNameEl = document.getElementById("focusName");
const focusCloseBtn = document.getElementById("focusClose");
const focusLastEl = document.getElementById("focusLast");
const barEls = {
    hunger: document.getElementById("barHunger"),
    social: document.getElementById("barSocial"),
    safety: document.getElementById("barSafety"),
};

// Click-to-follow state. latestPositions is whatever position snapshot currently drives the
// canvas (newest WS rows in live mode, currentTick's rows in static replay) so the focus
// card's needs bars always match what's drawn.
let focusVid = null;
let latestPositions = [];

// Gossip arcs: when a HEARSAY event lands live, draw a brief arc from the source villager to
// the reteller -- rumor propagation is the experiment's whole thesis, make it visible on the
// tank itself, not only as a "retelling Villager N" subline in the chat.
const ARC_DURATION_MS = 5000;
const activeArcs = []; // { from, to, expiresAt }

// Village pulse: run-so-far counts, sourced from /status's run_totals (A2, Run 2 Plan §A2).
// Used to compute these client-side from the WS catch-up frame, which relied on that frame
// replaying the FULL MemoryGraph history on connect -- A2 caps that backlog to the newest
// WS_BACKLOG_TICKS window (server-side), so these tiles would silently undercount on any
// run past that window. The server keeps the real incremental total instead (supervisor.py
// run_totals) and /status is polled every 30s to refresh them.
const pulseStats = { MOMENT: 0, GOSSIP: 0, DREAM: 0, COINED: 0 };
const speakCounts = new Map(); // vid -> events attributed to them (from what this tab has seen -- "most heard from" stays a since-connection approximation, unaffected by the totals fix)

// One pending reconnect timer at a time; manual connects cancel it. A socket that was
// genuinely open retries fast (blip recovery); one that never opened (Space rebuilding,
// bad URL) backs off long so we don't hammer a down host.
let reconnectTimer = null;
let socketWasOpen = false;

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
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
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
    // pulseStats is NOT reset here anymore -- it's server-truth run totals (A2), not derived
    // from this connection's catch-up frame, so a reconnect shouldn't zero it back to 0.
    // pollStatus() (already running on its own interval) refreshes it independently.
    speakCounts.clear();
    renderPulse();
    liveStatusEl.textContent = "connecting...";
    liveBtn.textContent = "Disconnect";
    socketWasOpen = false;
    liveSocket = new WebSocket(url);
    liveSocket.onopen = () => {
        socketWasOpen = true;
        liveStatusEl.textContent = "connected";
        connDotEl.classList.add("connected");
        connDotEl.title = "connected";
    };
    liveSocket.onclose = () => {
        liveSocket = null;
        liveBtn.textContent = "Connect";
        connDotEl.classList.remove("connected");
        // Served page = unattended public exhibit; never leave it permanently dead on a WS
        // drop. Reconnect resets state on purpose -- the server replays event history from
        // its own cursors, so appending across connections would duplicate everything.
        if (location.protocol !== "file:") {
            const delay = socketWasOpen ? 5000 : 30000;
            liveStatusEl.textContent = "reconnecting...";
            connDotEl.title = "reconnecting";
            reconnectTimer = setTimeout(() => connectLive(url), delay);
        } else {
            liveStatusEl.textContent = "disconnected";
            connDotEl.title = "disconnected";
        }
    };
    liveSocket.onerror = () => { liveStatusEl.textContent = "error"; };
    liveSocket.onmessage = (ev) => {
        const msg = JSON.parse(ev.data);
        if (msg.heartbeat) return; // no tick change, nothing to render
        const tick = msg.tick;
        if (msg.positions.length > 0) {
            const rows = msg.positions.map(
                ([vid, x, y, holding, hunger, social, safety]) => ({ vid, x, y, holding, hunger, social, safety })
            );
            positionsByTick.set(tick, rows);
            latestPositions = rows;
        }
        // Events keep their OWN tick (fold-back catch-up can deliver several past ticks'
        // worth at once) -- do NOT assume they belong to msg.tick, same shape as replay.json.
        for (const ev of msg.events) eventsSorted.push(ev);
        if (msg.coinages && msg.coinages.length > 0) {
            // newest-first in the live panel; spread stays null -- the live feed carries the
            // coinage rows, not the full event-text corpus a spread count is computed over
            for (const [term, coiner, birth_tick] of msg.coinages) {
                coinTerms.unshift({ term, coiner, birth_tick, spread: null });
            }
            if (coinTerms.length > 300) coinTerms.length = 300; // keep panel + memory bounded
            // pulseStats.COINED comes from /status now (A2) -- the panel list above is still
            // WS-fed (newest-200 snapshot + incremental), just no longer double-books the tile.
            renderCoinList();
        }
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

function tilePattern(tile, cache) {
    if (tile && tile.complete && tile.naturalWidth > 0) {
        if (!cache.pattern) cache.pattern = ctx.createPattern(tile, "repeat");
        return cache.pattern;
    }
    return null;
}
const grassCache = {}, dirtCache = {}, waterCache = {};

function drawGrid() {
    // Ground: per-cell terrain instead of one flat grass fill. Canvas patterns tile in
    // canvas-space (not reset per fillRect), so adjacent same-type cells still line up
    // seamlessly even though each cell is painted individually here.
    for (let y = 0; y < data.meta.grid_h; y++) {
        for (let x = 0; x < data.meta.grid_w; x++) {
            const t = terrainAt(x, y);
            const tile = t === "water" ? waterTile : t === "dirt" ? dirtTile : grassTile;
            const cache = t === "water" ? waterCache : t === "dirt" ? dirtCache : grassCache;
            const pattern = tilePattern(tile, cache);
            ctx.fillStyle = pattern || "#1c1f27"; // fallback until the sprite loads
            ctx.fillRect(x * TILE, y * TILE, TILE, TILE);
        }
    }
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

// Trees/bushes drawn as oversized objects anchored bottom-center of their cell (same
// convention as villager sprites) so they read as standing on the grid, not a background
// texture. Drawn after the ground fill but before villagers, so villagers always render
// in front of decoration -- avoids needing real depth-sorting for a purely cosmetic layer.
function drawDecorations() {
    for (const [cells, img, scale] of [[TREE_CELLS, treeImg, 2.2], [BUSH_CELLS, bushImg, 1.6]]) {
        if (!img || !img.complete || img.naturalWidth === 0) continue;
        const h = TILE * scale, w = h * (img.naturalWidth / img.naturalHeight);
        for (const [cx, cy] of cells) {
            const px = cx * TILE + TILE / 2, py = cy * TILE + TILE;
            ctx.drawImage(img, px - w / 2, py - h, w, h);
        }
    }
}

const TWEEN_SPEED = 4; // tile-units/sec the visual position eases toward the real one
const BOB_AMPLITUDE_PX = 1.4;
const BOB_SPEED = 1 / 700; // radians/ms, per-vid phase offset keeps the crowd from bobbing in lockstep

function drawVillagers(now, dt) {
    // Live mode draws the newest snapshot we actually have -- an event-only WS message
    // advances currentTick without carrying positions, and looking that tick up in
    // positionsByTick would come back empty and blank the whole tank for a frame.
    const villagers = liveMode ? latestPositions : (positionsByTick.get(currentTick) || []);
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
        const dx = v.x - vp.x, dy = v.y - vp.y;
        // walking = still easing toward a real target this frame. The sheet's paired
        // "walk" frames turned out to be near-duplicate idle poses for 4 of 5 colors on
        // close inspection (only red actually differs) -- a real frame-swap would've
        // animated one color and left the rest static, so this is a procedural squash/
        // stretch bounce instead, consistent across every color with no new art needed.
        const walking = Math.hypot(dx, dy) > 0.04;
        vp.x += Math.max(-step, Math.min(step, dx));
        vp.y += Math.max(-step, Math.min(step, dy));

        const bobSpeed = walking ? BOB_SPEED * 3.5 : BOB_SPEED;
        const bobAmp = walking ? BOB_AMPLITUDE_PX * 2.2 : BOB_AMPLITUDE_PX;
        const bob = Math.sin(now * bobSpeed + v.vid) * bobAmp;
        const cx = vp.x * TILE + TILE / 2;
        const cy = vp.y * TILE + TILE / 2 + bob;
        const walkPhase = walking ? Math.sin(now * bobSpeed + v.vid) : 0;
        const scaleX = 1 + walkPhase * 0.09, scaleY = 1 - walkPhase * 0.09;
        const colorName = villagerColorName(v.vid);
        if (v.vid === focusVid) {
            // follow ring under the focused villager's feet, drawn before the sprite
            ctx.strokeStyle = "#ffd166";
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.ellipse(cx, vp.y * TILE + TILE - 1, TILE * 0.55, TILE * 0.28, 0, 0, Math.PI * 2);
            ctx.stroke();
        }
        const sprite = (v.holding && eatingSprites[colorName]) ? eatingSprites[colorName] : villagerSprites[colorName];
        if (sprite && sprite.complete && sprite.naturalWidth > 0) {
            if (walking) {
                // pivot the squash/stretch from the feet (cy), not the sprite center, so it
                // reads as a bounce off the ground rather than the whole body stretching
                ctx.save();
                ctx.translate(cx, cy);
                ctx.scale(scaleX, scaleY);
                ctx.translate(-cx, -cy);
                ctx.drawImage(sprite, cx - spriteW / 2, cy - spriteH * 0.75, spriteW, spriteH);
                ctx.restore();
            } else {
                ctx.drawImage(sprite, cx - spriteW / 2, cy - spriteH * 0.75, spriteW, spriteH);
            }
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
    div.dataset.vid = vid; // click-to-follow chat filter matches on this
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
            if (focusVid !== null && vid !== focusVid) div.classList.add("filtered");
            logEl.insertBefore(div, logEl.firstChild);
            const typeName = TYPE_NAMES[type] || "EXPERIENCE";
            activeCues.set(vid, { type: typeName, expiresAt: performance.now() + CUE_DURATION_MS });
            if (typeName === "HEARSAY" && actor != null && actor !== vid) {
                activeArcs.push({ from: actor, to: vid, expiresAt: performance.now() + ARC_DURATION_MS });
            }
            // pulseStats no longer incremented here -- see A2 note at its declaration; the
            // MOMENT/GOSSIP/DREAM/COINED tiles come from /status's run_totals instead.
            speakCounts.set(vid, (speakCounts.get(vid) || 0) + 1);
        }
        if (newOnes.length > 0) renderPulse();
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
    applyChatFilter();
}

function applyChatFilter() {
    for (const el of logEl.children) {
        el.classList.toggle("filtered", focusVid !== null && parseInt(el.dataset.vid, 10) !== focusVid);
    }
}

function renderCoinList() {
    coinListEl.innerHTML = "";
    for (const c of coinTerms) {
        const div = document.createElement("div");
        div.className = "coin" + (c.spread >= 2 ? " spread" : "");
        const spreadNote = c.spread === null ? "" : `, spread to ${c.spread} villager(s)`;
        div.innerHTML = `<span class="term">${c.term}</span> — coined by v${c.coiner} @t${c.birth_tick}${spreadNote}`;
        coinListEl.appendChild(div);
    }
}

// Quadratic arc from gossip source to reteller, with a travelling dot for direction --
// drawn every frame like the villagers, expired entries dropped in place.
function drawArcs(now) {
    for (let i = activeArcs.length - 1; i >= 0; i--) {
        const a = activeArcs[i];
        if (now > a.expiresAt) { activeArcs.splice(i, 1); continue; }
        const f = visualPos.get(a.from), t = visualPos.get(a.to);
        if (!f || !t) continue; // endpoint not on screen (yet) -- keep it, positions may land
        const x1 = f.x * TILE + TILE / 2, y1 = f.y * TILE + TILE / 2;
        const x2 = t.x * TILE + TILE / 2, y2 = t.y * TILE + TILE / 2;
        const lift = Math.min(60, Math.hypot(x2 - x1, y2 - y1) * 0.35) + 14;
        const mx = (x1 + x2) / 2, my = (y1 + y2) / 2 - lift;
        const rem = (a.expiresAt - now) / ARC_DURATION_MS;
        ctx.globalAlpha = Math.min(0.85, rem * 2);
        ctx.strokeStyle = "#5b9bd5";
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.quadraticCurveTo(mx, my, x2, y2);
        ctx.stroke();
        const p = 1 - rem; // progress 0->1 over the arc's lifetime
        const bx = (1 - p) * (1 - p) * x1 + 2 * (1 - p) * p * mx + p * p * x2;
        const by = (1 - p) * (1 - p) * y1 + 2 * (1 - p) * p * my + p * p * y2;
        ctx.fillStyle = "#bcd9f2";
        ctx.beginPath();
        ctx.arc(bx, by, 3, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
    }
}

// ---- click-to-follow ----

canvas.addEventListener("click", (e) => {
    if (!data) return;
    const rect = canvas.getBoundingClientRect();
    const tx = (e.clientX - rect.left) / TILE;
    const ty = (e.clientY - rect.top) / TILE;
    // hit-test against visualPos (where sprites are actually drawn), nearest within ~1 tile
    let best = null, bestD = 1.1;
    for (const [vid, vp] of visualPos) {
        const d = Math.hypot(vp.x + 0.5 - tx, vp.y + 0.5 - ty);
        if (d < bestD) { bestD = d; best = vid; }
    }
    setFocus(best); // empty-grass click -> null -> clears focus
});

focusCloseBtn.addEventListener("click", () => setFocus(null));

function setFocus(vid) {
    focusVid = vid;
    if (vid === null) {
        focusCardEl.classList.add("hidden");
    } else {
        focusCardEl.classList.remove("hidden");
        const colorName = villagerColorName(vid);
        focusAvatarEl.style.background = VILLAGER_COLOR_HEX[colorName] || "#888";
        focusNameEl.textContent = `Villager ${vid}`;
        updateFocusCard();
    }
    applyChatFilter();
}

function updateFocusCard() {
    if (focusVid === null) return;
    const row = latestPositions.find((v) => v.vid === focusVid);
    if (row) {
        // needs are engine fixed-point satiety values: 100000 = fully met (types.h
        // FIXED_POINT_ONE), so /1000 = percent-full for the bar
        for (const [key, el] of Object.entries(barEls)) {
            const pct = Math.max(0, Math.min(100, row[key] / 1000));
            el.style.width = pct + "%";
            el.style.background = pct < 25 ? "#d9635c" : pct < 55 ? "#f0ad4e" : "#5fb878";
        }
    }
    for (let i = eventsSorted.length - 1; i >= 0; i--) {
        const [, vid, , type, , , text] = eventsSorted[i];
        if (vid === focusVid) {
            const label = TYPE_LABEL[TYPE_NAMES[type]] || "MOMENT";
            focusLastEl.textContent = `${label}: “${text.length > 160 ? text.slice(0, 160) + "…" : text}”`;
            return;
        }
    }
    focusLastEl.textContent = "hasn't said anything yet";
}

// ---- village pulse (run-so-far stats, served mode) ----

const PULSE_TILES = [["MOMENT", "💭 moments"], ["GOSSIP", "🗣️ gossips"], ["DREAM", "🌙 dreams"], ["COINED", "✨ words coined"]];

function renderPulse() {
    if (pulseEl.classList.contains("hidden")) return;
    pulseCountsEl.innerHTML = "";
    for (const [key, label] of PULSE_TILES) {
        const div = document.createElement("div");
        div.className = "pulseStat " + key;
        // no "+"-floor suffix anymore -- run_totals (A2) is an exact server-side count,
        // not a WS-snapshot-derived lower bound like the old client-side tally was.
        div.innerHTML = `<span class="num">${pulseStats[key].toLocaleString()}</span><span class="lbl">${label}</span>`;
        pulseCountsEl.appendChild(div);
    }
    let topVid = null, topN = 0;
    for (const [vid, n] of speakCounts) if (n > topN) { topN = n; topVid = vid; }
    if (topVid !== null) {
        const hex = VILLAGER_COLOR_HEX[villagerColorName(topVid)] || "#888";
        pulseTopEl.innerHTML = `<span class="avatar" style="background:${hex}"></span>most heard from: <b>Villager ${topVid}</b> (${topN})`;
    } else {
        pulseTopEl.textContent = "";
    }
}

// ---- experiment status strip (live/served mode only) ----

function fmtDuration(s) {
    const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
    return d > 0 ? `${d}d ${h}h` : h > 0 ? `${h}h ${m}m` : `${m}m`;
}

async function pollStatus() {
    try {
        const r = await fetch("/status");
        const s = await r.json();
        const parts = [`🌍 tick ${s.tick != null ? s.tick.toLocaleString() : "—"}`,
                       `container up ${fmtDuration(s.uptime_s)}`];
        parts.push(s.last_backup_ts != null
            ? `last backup ${fmtDuration(Date.now() / 1000 - s.last_backup_ts)} ago`
            : "no backup yet");
        statusWorldEl.textContent = parts.join(" · ");
        // A2: Village Pulse tiles are server-truth run totals now (see pulseStats decl) --
        // /status is the only place they're set, on this existing 30s poll.
        if (s.run_totals) {
            pulseStats.MOMENT = s.run_totals.moments;
            pulseStats.GOSSIP = s.run_totals.gossips;
            pulseStats.DREAM = s.run_totals.dreams;
            pulseStats.COINED = s.run_totals.coined;
            renderPulse();
        }
    } catch {
        statusWorldEl.textContent = "status unavailable";
    }
}

// ---- first-visit intro overlay ----

const INTRO_SEEN_KEY = "tbv_intro_seen";
helpBtn.addEventListener("click", () => introOverlayEl.classList.remove("hidden"));
introDismissBtn.addEventListener("click", () => {
    introOverlayEl.classList.add("hidden");
    try { localStorage.setItem(INTRO_SEEN_KEY, "1"); } catch { /* private mode */ }
});
let introSeen = false;
try { introSeen = localStorage.getItem(INTRO_SEEN_KEY) === "1"; } catch { /* private mode */ }
if (!introSeen) introOverlayEl.classList.remove("hidden");

if (location.protocol !== "file:") {
    pollStatus();
    setInterval(pollStatus, 30000);
    // Served by the Space = public exhibit: the file picker, raw WS URL and connect
    // button are local-dev plumbing (auto-connect already handles the live feed) --
    // swap them for identity + the village pulse. file:// keeps the dev tooling.
    titleEl.textContent = "🐠 10-Bit Village";
    devControlsEl.classList.add("hidden");
    taglineEl.classList.remove("hidden");
    pulseEl.classList.remove("hidden");
    renderPulse();
} else {
    // file:// static replay has no server to ask -- the strip would just say "unavailable"
    statusBarEl.classList.add("hidden");
}

function render() {
    // Data-driven side (log/scrub/label) -- called when currentTick or the underlying data
    // actually changes. Canvas drawing is NOT here anymore: it needs to run every animation
    // frame (bob/tween are continuous), not just on data change, so it lives in frame() now.
    if (!data) return;
    if (!liveMode) latestPositions = positionsByTick.get(currentTick) || [];
    renderEvents();
    updateFocusCard();
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
        drawDecorations();
        drawVillagers(now, dt);
        drawArcs(now);
    }
    lastFrameTime = now;
    requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
