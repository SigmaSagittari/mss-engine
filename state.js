const token = document.querySelector('meta[name="mss-token"]').content;
const boardElement = document.getElementById("board");
const messageElement = document.getElementById("message");
const detailPanel = document.getElementById("cell-detail");
const detailStatus = document.getElementById("detail-status");
const detailScoreRow = document.getElementById("detail-score-row");
const detailScoreLabel = document.getElementById("detail-score-label");
const detailScore = document.getElementById("detail-score");
const detailContent = document.getElementById("detail-content");
const detailPosition = document.getElementById("detail-position");
const detailEngine = document.getElementById("detail-engine");
const detailColumns = document.getElementById("detail-columns");
const detailSide = document.getElementById("detail-side");
const detailRisk = document.getElementById("detail-risk");
const observeResult = document.getElementById("observe-result");
const difficulty = document.getElementById("difficulty");
const customSettings = document.getElementById("custom-settings");
const settingsDialog = document.getElementById("settings-dialog");
const suggestStrategy = document.getElementById("suggest-strategy");
const fiftyFifty = document.getElementById("fifty-fifty");
const templatePrefix = document.getElementById("template-prefix");
const ltrEventLimit = document.getElementById("ltr-event-limit");
const policyNodeBudget = document.getElementById("policy-node-budget");
const shapeAlgo = document.getElementById("shape-algo");
const probabilityDisplay = document.getElementById("probability-display");
const cellSizeSetting = document.getElementById("cell-size");
const rngSeedInput = document.getElementById("rng-seed");
const regenerateSeedButton = document.getElementById("regenerate-seed");
const boardStringInput = document.getElementById("board-string");
const boardExportStatus = document.getElementById("board-export-status");
const importBoardStringButton = document.getElementById("import-board-string");
const bruteEnabled = document.getElementById("endgame-enabled");
const endgameCandidateLimit = document.getElementById("endgame-candidate-limit");
const endgameCheckAllMoves = document.getElementById("endgame-check-all-moves");
const endgameMultithread = document.getElementById("endgame-multithread");
const endgameMaxThreads = document.getElementById("endgame-max-threads");
const analysisSummary = document.getElementById("analysis-summary");
const analysisCountRow = document.getElementById("analysis-count-row");
const analysisCount = document.getElementById("analysis-count");
const analysisRecommendation = document.getElementById("analysis-recommendation");
const analysisBlocker = document.getElementById("analysis-blocker");
const mctsButton = document.getElementById("mcts-analysis");
const mctsDetail = document.getElementById("mcts-detail");
const mctsVisits = document.getElementById("mcts-visits");
const mctsWins = document.getElementById("mcts-wins");
const mctsCountFormat = new Intl.NumberFormat("en", { notation: "compact", maximumFractionDigits: 1 });
let mode = "play";
let playState = null;
let analysisState = null;
let probabilities = null;
let candidateCount = "";
let hoveredCell = -1;
let selectedCell = -1;
let flags = new Set();
let detailRequest = 0;
let leftMouseDown = false;
let pressedCells = new Set();
let analysisRequestQueue = Promise.resolve();
let cachedAnalysis = null;
let probabilityUpdateQueue = Promise.resolve();
let probabilityRevision = 0;
let probabilityPending = false;
let operationBusy = false;
let suggestionResult = null;
let detailData = null;
let suggestionMoves = new Map();
let endgameBestCells = new Set();
const searchButton = document.getElementById("anytime-search");
const searchHistory = document.getElementById("anytime-search-history");
const searchNodeBudget = document.getElementById("anytime-search-nodes");
const searchWidths = ["search-root-width", "search-middle-width", "search-deep-width"].map((id) => document.getElementById(id));
const rngMask = 0xffffffffffffffffn;

function makeRandomSeed() {
    const words = new Uint32Array(4);
    crypto.getRandomValues(words);
    return { lo: (BigInt(words[0]) << 32n) | BigInt(words[1]), hi: (BigInt(words[2]) << 32n) | BigInt(words[3]) };
}

function postForm(path, fields, signal) {
    return fetch(path, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded", "X-Mss-Token": token },
        body: new URLSearchParams(fields),
        signal
    });
}

async function post(path, fields = {}) {
    const response = token ? await postForm(path, fields) : null;
    const result = response ? await response.json() : await wasmRequest(path, fields);
    if ((response && !response.ok) || !result.ok)
        throw new Error(result.error || (response ? `请求失败（${response.status}）` : "请求失败。"));
    return result;
}

async function readUpdates(path, fields, controller, update, nodes) {
    if (!token) {
        await post(`${path}/start`, fields);
        if (nodes && !controller.signal.aborted) await post(`${path}/control`, { action: "step", nodes });
        while (!controller.signal.aborted) {
            const result = await post(`${path}/update`);
            if (controller.signal.aborted) break;
            update(result);
            if (!controller.signal.aborted)
                await new Promise((resolve) => setTimeout(resolve, result.paused ? 100 : path === "/api/anytime-search" ? 0 : 16));
        }
        return;
    }
    const response = await postForm(`${path}/start`, fields, controller.signal);
    if (!response.ok || !response.headers.get("Content-Type").startsWith("text/event-stream")) {
        const result = await response.json();
        throw new Error(result.error || `请求失败（${response.status}）`);
    }
    if (nodes) await post(`${path}/control`, { action: "step", nodes });
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let pending = "";
    while (!controller.signal.aborted) {
        const { value, done } = await reader.read();
        if (done || controller.signal.aborted) break;
        pending += decoder.decode(value, { stream: true });
        let end;
        while ((end = pending.indexOf("\n\n")) >= 0) {
            const event = pending.slice(0, end);
            pending = pending.slice(end + 2);
            update(JSON.parse(event.slice(6)));
            if (controller.signal.aborted) break;
        }
    }
}
function activeState() {
    return mode === "play" ? playState : analysisState;
}

function boardSnapshot({ rows, cols, mines, cells }, hidden = /\*/g) {
    return { rows, cols, mines, cells: cells.replace(hidden, "?") };
}

let rng = makeRandomSeed();
