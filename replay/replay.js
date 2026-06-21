// 10-Bit Village — local replay viewer (v1 stage 2). Standalone, file:// only,
// never touches the engine. Loads a replay.json exported by scripts/export_replay.py.

const TYPE_NAMES = ["EXPERIENCE", "HEARSAY", "DREAM"];
const TILE = 16;

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

function villagerColor(vid) {
    const hue = (vid * 37) % 360;
    return `hsl(${hue}, 65%, 55%)`;
}

function drawGrid() {
    ctx.fillStyle = "#1c1f27";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = "#262a34";
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
    for (const v of villagers) {
        const cx = v.x * TILE + TILE / 2;
        const cy = v.y * TILE + TILE / 2;
        ctx.fillStyle = villagerColor(v.vid);
        ctx.beginPath();
        ctx.arc(cx, cy, TILE * 0.32, 0, Math.PI * 2);
        ctx.fill();
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
