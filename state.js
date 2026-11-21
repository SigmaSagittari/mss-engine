const token = document.querySelector('meta[name="mss-token"]').content;
const boardElement = document.getElementById("board");
const boardMessage = document.getElementById("board-message");
const analysisMessage = document.getElementById("analysis-message");
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
const templatePrefix = document.getElementById("template-prefix");
const ltrEventLimit = document.getElementById("ltr-event-limit");
const policyNodeBudget = document.getElementById("policy-node-budget");
const shapeAlgo = document.getElementById("shape-algo");
const probabilityDisplay = document.getElementById("probability-display");
const highlightDisplay = document.getElementById("highlight-display");
const cellSizeSetting = document.getElementById("cell-size");
const rngSeedInput = document.getElementById("rng-seed");
const boardStringInput = document.getElementById("board-string");
const boardExportStatus = document.getElementById("board-export-status");
const importBoardStringButton = document.getElementById("import-board-string");
const bruteEnabled = document.getElementById("endgame-enabled");
const endgameCandidateLimit = document.getElementById("endgame-candidate-limit");
const endgameCheckAllMoves = document.getElementById("endgame-check-all-moves");
const endgameMultithread = document.getElementById("endgame-multithread");
const endgameMaxThreads = document.getElementById("endgame-max-threads");
const analysisSummary = document.getElementById("analysis-summary");
const searchStatus = document.getElementById("search-status");
const searchCount = document.getElementById("search-count");
const searchSpeed = document.getElementById("search-speed");
const searchDepth = document.getElementById("search-depth");
const boardCountRow = document.getElementById("board-count-row");
const boardCount = document.getElementById("board-count");
const analysisRecommendation = document.getElementById("analysis-recommendation");
const ioeResultPanel = document.getElementById("ioe-result");
let mode = "play";
let playState = null;
let analysisState = null;
let probabilities = null;
let candidateCount = "";
let hoveredCell = -1;
let selectedCell = -1;
let flags = new Set();
let detailRequest = 0;
const boardPresses = new Map();
let pressedCells = new Set();
let analysisRequestQueue = Promise.resolve();
let cachedAnalysis = null;
let probabilityUpdateQueue = Promise.resolve();
let probabilityRevision = 0;
let probabilityPending = false;
let operationBusy = false;
let ioeController = null;
let ioeMove = null;
let detailData = null;
const searchButton = document.getElementById("anytime-search");
const searchHistory = document.getElementById("anytime-search-history");
const searchWidths = ["search-root-width", "search-middle-width", "search-deep-width"].map((id) => document.getElementById(id));
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

async function post(path, fields = {}, signal) {
    const response = token ? await postForm(path, fields, signal) : null;
    const result = response ? await readApiJson(response) : await wasmRequest(path, fields);
    return requireApiSuccess(result, response);
}

function readApiJson(response) {
    if (!response.ok && !response.headers.get("Content-Type")?.startsWith("application/json"))
        throw new UiError("httpFailed", response.status);
    return response.json();
}

function requireApiSuccess(result, response) {
    if ((!response || response.ok) && result.ok) return result;
    const code = result.errorCode;
    if (Number.isInteger(code))
        throw Object.hasOwn(messages, code) ? new UiError(code) : new UiError("unknownErrorCode", code);
    if (response && !response.ok) throw new UiError("httpFailed", response.status);
    throw new UiError("requestFailed");
}

async function startUpdates(path, fields, signal) {
    if (!token)
        return post(`${path}/start`, fields);
    const response = await postForm(`${path}/start`, fields, signal);
    if (!response.ok || !response.headers.get("Content-Type")?.startsWith("text/event-stream")) {
        requireApiSuccess(await readApiJson(response), response);
        throw new UiError("requestFailed");
    }
    return response;
}

async function readUpdates(path, response, controller, update) {
    if (!token) {
        while (!controller.signal.aborted) {
            const result = await post(`${path}/update`);
            if (controller.signal.aborted) break;
            update(result);
            if (!controller.signal.aborted)
                await new Promise((resolve) => setTimeout(resolve, result.paused ? 100 : 0));
        }
        return;
    }
    await readEventStream(response, update, controller.signal);
}

async function readEventStream(response, update, signal) {
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let pending = "";
    while (!signal?.aborted) {
        const { value, done } = await reader.read();
        if (done || signal?.aborted) break;
        pending += decoder.decode(value, { stream: true });
        let end;
        while ((end = pending.indexOf("\n\n")) >= 0) {
            const event = pending.slice(0, end);
            pending = pending.slice(end + 2);
            update(JSON.parse(event.slice(6)));
            if (signal?.aborted) break;
        }
    }
}

async function analyzeIoeWithProgress(fields, update, signal) {
    const path = "/api/analyze-ioe/stream";
    let result;
    if (!token) {
        result = await wasmRequest(path, fields, update, signal);
    } else {
        const response = await postForm(path, fields, signal);
        if (response.ok && response.headers.get("Content-Type")?.startsWith("text/event-stream")) {
            await readEventStream(response, event => {
                if (event.progress) update(event.progress);
                else result = event;
            }, signal);
        } else {
            requireApiSuccess(await readApiJson(response), response);
            throw new UiError("requestFailed");
        }
    }
    signal.throwIfAborted();
    if (!result) throw new UiError("ioeDisconnected");
    return requireApiSuccess(result);
}

function activeState() {
    return mode === "play" ? playState : analysisState;
}

function boardSnapshot({ rows, cols, mines, cells }, hidden = /\*/g) {
    return { rows, cols, mines, cells: cells.replace(hidden, "?") };
}

let rng = makeRandomSeed();
