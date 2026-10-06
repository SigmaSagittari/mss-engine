const token = document.querySelector('meta[name="mss-token"]').content;
const boardElement = document.getElementById("board");
const messageElement = document.getElementById("message");
const detailEmpty = document.getElementById("detail-empty");
const detailContent = document.getElementById("detail-content");
const detailPosition = document.getElementById("detail-position");
const detailRisk = document.getElementById("detail-risk");
const detailCandidates = document.getElementById("detail-candidates");
const observeResult = document.getElementById("observe-result");
const difficulty = document.getElementById("difficulty");
const customSettings = document.getElementById("custom-settings");
const settingsDialog = document.getElementById("settings-dialog");
const suggestStrategy = document.getElementById("suggest-strategy");
const shapeAlgo = document.getElementById("shape-algo");
const probabilityDisplay = document.getElementById("probability-display");
const cellSizeSetting = document.getElementById("cell-size");
const rngSeedInput = document.getElementById("rng-seed");
const regenerateSeedButton = document.getElementById("regenerate-seed");
const boardStringInput = document.getElementById("board-string");
const boardExportStatus = document.getElementById("board-export-status");
const importBoardStringButton = document.getElementById("import-board-string");
const analysisEditor = document.getElementById("analysis-editor");
const cellState = document.getElementById("cell-state");
const endgameCandidateLimit = document.getElementById("endgame-candidate-limit");
const endgameCheckAllMoves = document.getElementById("endgame-check-all-moves");
const endgameMultithread = document.getElementById("endgame-multithread");
const endgameMaxThreads = document.getElementById("endgame-max-threads");
const endgameResult = document.getElementById("endgame-result");
const endgameSummary = document.getElementById("endgame-summary");
const endgameMoves = document.getElementById("endgame-moves");
const analysisBlocker = document.getElementById("analysis-blocker");
const mctsButton = document.getElementById("mcts-analysis");
const mctsSummary = document.getElementById("mcts-summary");
const mctsDetail = document.getElementById("mcts-detail");
const mctsVisits = document.getElementById("mcts-visits");
const mctsWins = document.getElementById("mcts-wins");
const mctsWinrate = document.getElementById("mcts-winrate");
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
let analysisElapsedText = "";
let leftMouseDown = false;
let pressedCells = new Set();
let analysisRequestQueue = Promise.resolve();
let cachedAnalysis = null;
let probabilityUpdateQueue = Promise.resolve();
let probabilityRevision = 0;
let probabilityPending = false;
let operationBusy = false;
let endgameResultAvailable = false;
let endgameCandidateCells = new Set();
let endgameBestCells = new Set();
let mctsController = null;
let mctsStopQueue = Promise.resolve();
let mctsResult = null;
let mctsMoves = new Map();
let mctsHistory = [];
let mctsTimer = null;
const rngMask = 0xffffffffffffffffn;

function makeRandomSeed() {
    const words = new Uint32Array(4);
    crypto.getRandomValues(words);
    return { lo: (BigInt(words[0]) << 32n) | BigInt(words[1]), hi: (BigInt(words[2]) << 32n) | BigInt(words[3]) };
}

async function post(path, fields = {}) {
    const response = await fetch(path, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded", "X-Mss-Token": token },
        body: new URLSearchParams(fields)
    });
    const result = await response.json();
    if (!response.ok || !result.ok)
        throw new Error(result.error || `请求失败（${response.status}）`);
    return result;
}

function activeState() {
    return mode === "play" ? playState : analysisState;
}

let rng = makeRandomSeed();
