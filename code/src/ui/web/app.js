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
const endgameResult = document.getElementById("endgame-result");
const endgameSummary = document.getElementById("endgame-summary");
const endgameMoves = document.getElementById("endgame-moves");
const analysisBlocker = document.getElementById("analysis-blocker");
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
let probabilityUpdateQueue = Promise.resolve();
let probabilityRevision = 0;
let probabilityPending = false;
let operationBusy = false;
let endgameResultAvailable = false;
let endgameCandidateCells = new Set();
let endgameBestCells = new Set();
const rngMask = 0xffffffffffffffffn;

function makeRandomSeed() {
    const words = new Uint32Array(4);
    crypto.getRandomValues(words);
    return { lo: (BigInt(words[0]) << 32n) | BigInt(words[1]), hi: (BigInt(words[2]) << 32n) | BigInt(words[3]) };
}

let rng = makeRandomSeed();

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

function parseRngSeed(value) {
    if (!/^[0-9a-f]{32}$/i.test(value))
        return null;
    // 十六进制格式按高 64 位、低 64 位排列；C++ Random 初始化顺序仍为 lo、hi。
    return { hi: BigInt(`0x${value.slice(0, 16)}`), lo: BigInt(`0x${value.slice(16)}`) };
}

function formatRngSeed(seed) {
    return seed.hi.toString(16).padStart(16, "0") + seed.lo.toString(16).padStart(16, "0");
}

// 与 core/utility/rng.h 的 splitmix64 位运算保持一致。
function splitmix64(value) {
    value = (value + 0x9e3779b97f4a7c15n) & rngMask;
    value = ((value ^ (value >> 30n)) * 0xbf58476d1ce4e5b9n) & rngMask;
    value = ((value ^ (value >> 27n)) * 0x94d049bb133111ebn) & rngMask;
    return value ^ (value >> 31n);
}

// 与 GameControl::MineBoard 使用相同 RNG；新盘面生成后输入框显示推进后的状态。
function nextRandom() {
    rng.lo = splitmix64(rng.lo + 0x9e3779b97f4a7c15n);
    rng.hi = splitmix64(rng.hi + 0xd1b54a32d192ed03n);
    return rng.lo ^ rng.hi;
}

function setMessage(text, kind = "") {
    if (mode === "play") {
        detailEmpty.hidden = !text;
        detailEmpty.className = kind ? `muted message ${kind}` : "muted";
        detailEmpty.textContent = text;
        detailContent.hidden = true;
        messageElement.hidden = true;
        messageElement.textContent = "";
        messageElement.className = "message";
        return;
    }
    messageElement.hidden = false;
    messageElement.textContent = text;
    messageElement.className = `message ${kind}`;
}

function activeState() {
    return mode === "play" ? playState : analysisState;
}

function currentBoardString() {
    const state = activeState();
    const cells = mode === "play" ? state.cells.replace(/\*/g, "?") : state.cells;
    return JSON.stringify({ rows: state.rows, cols: state.cols, mines: state.mines, cells });
}

function cellFromTarget(target) {
    const button = target.closest(".cell");
    return button ? Number(button.dataset.cell) : -1;
}

function selectAnalysisCell(cell) {
    if (mode !== "analysis" || hoveredCell === cell)
        return;
    if (hoveredCell >= 0)
        boardElement.children[hoveredCell].classList.remove("hovered");
    hoveredCell = cell;
    if (hoveredCell >= 0)
        boardElement.children[hoveredCell].classList.add("hovered");
}

function setDifficultyValues(value) {
    if (value === "beginner") {
        document.getElementById("rows").value = 9;
        document.getElementById("cols").value = 9;
        document.getElementById("mines").value = 10;
    }
    if (value === "intermediate") {
        document.getElementById("rows").value = 16;
        document.getElementById("cols").value = 16;
        document.getElementById("mines").value = 40;
    }
    if (value === "advanced") {
        document.getElementById("rows").value = 16;
        document.getElementById("cols").value = 30;
        document.getElementById("mines").value = 99;
    }
    customSettings.hidden = value !== "custom";
}

function showDetail(data, cell) {
    detailEmpty.hidden = !analysisElapsedText;
    detailEmpty.textContent = analysisElapsedText;
    detailContent.hidden = false;
    const state = activeState();
    const row = Math.floor(cell / state.cols) + 1;
    const col = cell % state.cols + 1;
    detailPosition.textContent = `${row} 行，${col} 列`;
    detailRisk.textContent = data.probability;
    detailCandidates.textContent = data.candidateCount || candidateCount || "—";
    observeResult.replaceChildren();
    if (!data.observe) {
        const item = document.createElement("div");
        item.className = "muted";
        item.textContent = "该格已揭开，没有新的观察分布。";
        observeResult.append(item);
        return;
    }
    for (let number = 0; number <= 9; ++number) {
        const item = document.createElement("div");
        item.className = "observe-item";
        const label = document.createElement("span");
        label.textContent = number === 9 ? "爆炸" : `数字 ${number}`;
        const value = document.createElement("span");
        value.textContent = data.observe[number];
        item.append(label, value);
        observeResult.append(item);
    }
}

function clearDetail() {
    ++detailRequest;
    analysisElapsedText = "";
    detailEmpty.hidden = true;
    detailEmpty.className = "muted";
    detailContent.hidden = true;
}

function clearEndgameResult() {
    endgameResultAvailable = false;
    endgameCandidateCells.clear();
    endgameBestCells.clear();
    for (let cell = 0; cell < boardElement.children.length; ++cell)
        boardElement.children[cell].classList.remove("endgame-candidate", "endgame-best");
    endgameResult.hidden = true;
    endgameSummary.textContent = "";
    endgameMoves.replaceChildren();
}

async function requestDetail(cell) {
    if (mode !== "analysis" || !probabilityDisplay.checked || probabilityPending || !probabilities)
        return;
    const request = ++detailRequest;
    const requestedMode = mode;
    if (analysisState.cells[cell] >= "0" && analysisState.cells[cell] <= "8") {
        showDetail({ probability: "0.000000", candidateCount, observe: null }, cell);
        return;
    }
    detailEmpty.hidden = !analysisElapsedText;
    detailContent.hidden = true;
    try {
        const data = await post("/api/detail", { mode, cell, algo: shapeAlgo.value });
        if (request === detailRequest && mode === requestedMode)
            showDetail(data, cell);
    } catch (error) {
        if (request === detailRequest) {
            detailEmpty.hidden = false;
            detailContent.hidden = true;
            detailEmpty.textContent = error.message;
        }
    }
}

function neighbors(cell, state) {
    const row = Math.floor(cell / state.cols);
    const col = cell % state.cols;
    const result = [];
    for (let rowOffset = -1; rowOffset <= 1; ++rowOffset)
        for (let colOffset = -1; colOffset <= 1; ++colOffset) {
            if (rowOffset === 0 && colOffset === 0)
                continue;
            const nextRow = row + rowOffset;
            const nextCol = col + colOffset;
            if (nextRow >= 0 && nextRow < state.rows && nextCol >= 0 && nextCol < state.cols)
                result.push(nextRow * state.cols + nextCol);
        }
    return result;
}

function numberCells(minefield, state) {
    let result = "";
    for (let cell = 0; cell < minefield.length; ++cell) {
        if (minefield[cell] === "1") {
            result += "*";
            continue;
        }
        let count = 0;
        for (const neighbor of neighbors(cell, state))
            count += minefield[neighbor] === "1" ? 1 : 0;
        result += count;
    }
    return result;
}

function createPlayState(rows, cols, mines) {
    const cells = new Array(rows * cols);
    for (let cell = 0; cell < cells.length; ++cell)
        cells[cell] = cell;
    // 与 GameControl::MineBoard::generate 使用相同的 Fisher-Yates 顺序。
    for (let cell = cells.length - 1; cell > 0; --cell) {
        const other = Number(nextRandom() % BigInt(cell + 1));
        const value = cells[cell];
        cells[cell] = cells[other];
        cells[other] = value;
    }
    const minefield = new Array(cells.length).fill("0");
    for (let mine = 0; mine < mines; ++mine)
        minefield[cells[mine]] = "1";
    const state = { rows, cols, mines, minefield: minefield.join(""), cells: "?".repeat(rows * cols), firstMove: true, lost: false, won: false, lostCell: -1 };
    state.numbers = numberCells(state.minefield, state);
    return state;
}

function moveFirstMine(cell) {
    const safeCells = [];
    for (let target = 0; target < playState.minefield.length; ++target)
        if (playState.minefield[target] === "0")
            safeCells.push(target);
    const safeCell = safeCells[Number(nextRandom() % BigInt(safeCells.length))];
    rngSeedInput.value = formatRngSeed(rng);
    const minefield = playState.minefield.split("");
    minefield[cell] = "0";
    minefield[safeCell] = "1";
    playState.minefield = minefield.join("");
    playState.numbers = numberCells(playState.minefield, playState);
}

function revealLocally(cell) {
    revealLocallyBatch([cell]);
}

function revealLocallyBatch(seedCells) {
    const cells = playState.cells.split("");
    playState.firstMove = false;
    const pending = [...seedCells];
    const visited = new Set();
    while (pending.length > 0) {
        const current = pending.pop();
        if (visited.has(current) || cells[current] !== "?")
            continue;
        visited.add(current);
        if (playState.numbers[current] === "*") {
            playState.lost = true;
            playState.won = false;
            playState.lostCell = current;
            for (let index = 0; index < playState.minefield.length; ++index)
                if (playState.minefield[index] === "1")
                    cells[index] = "*";
            playState.cells = cells.join("");
            return;
        }
        cells[current] = playState.numbers[current];
        if (cells[current] === "0")
            for (const neighbor of neighbors(current, playState))
                if (cells[neighbor] === "?")
                    pending.push(neighbor);
    }
    playState.cells = cells.join("");
    playState.won = true;
    for (let index = 0; index < cells.length; ++index)
        if (playState.minefield[index] === "0" && cells[index] === "?") {
            playState.won = false;
            break;
        }
}

function clearPressedCells() {
    for (const cell of pressedCells)
        boardElement.children[cell].classList.remove("open");
    pressedCells.clear();
}

function updatePressedCells(cell) {
    clearPressedCells();
    if (!leftMouseDown || mode !== "play" || playState.lost || playState.won || cell < 0)
        return;
    const value = playState.cells[cell];
    let targets = [];
    if (value >= "1" && value <= "8")
        targets = neighbors(cell, playState);
    else if (value === "?" && !flags.has(cell))
        targets.push(cell);
    for (const target of targets) {
        if (playState.cells[target] !== "?" || flags.has(target))
            continue;
        boardElement.children[target].classList.add("open");
        pressedCells.add(target);
    }
}

function revealCell(cell) {
    clearEndgameResult();
    playState = { ...playState };
    if (playState.firstMove && playState.minefield[cell] === "1")
        moveFirstMine(cell);
    flags.delete(cell);
    revealLocally(cell);
    scheduleProbabilityUpdate();
    render();
    if (playState.lost)
        setMessage("踩到地雷，本局结束。", "error");
    else if (playState.won)
        setMessage("恭喜，所有安全格都已揭开。", "success");
    else
        setMessage("");
}

function chord(cell) {
    const number = Number(playState.cells[cell]);
    const around = neighbors(cell, playState);
    const flagCount = around.filter((next) => flags.has(next)).length;
    if (flagCount !== number)
        return;
    const targets = around.filter((next) => playState.cells[next] === "?" && !flags.has(next));
    if (targets.length === 0)
        return;
    clearEndgameResult();
    playState = { ...playState };
    for (const target of targets) {
        if (playState.cells[target] !== "?" || flags.has(target))
            continue;
        if (playState.firstMove && playState.minefield[target] === "1")
            moveFirstMine(target);
        flags.delete(target);
        revealLocally(target);
        if (playState.lost || playState.won)
            break;
    }
    scheduleProbabilityUpdate();
    render();
    if (playState.lost)
        setMessage("踩到地雷，本局结束。", "error");
    else if (playState.won)
        setMessage("恭喜，所有安全格都已揭开。", "success");
    else
        setMessage("");
}

function activatePlayCell(cell) {
    if (cell < 0 || playState.lost || playState.won)
        return;
    const value = playState.cells[cell];
    if (value === "?") {
        if (!flags.has(cell))
            revealCell(cell);
    } else if (value >= "1" && value <= "8")
        chord(cell);
}

function toggleFlag(cell) {
    if (flags.has(cell))
        flags.delete(cell);
    else
        flags.add(cell);
    render();
}

function updateCell(button, cell, state) {
    button.className = "cell";
    button.textContent = "";
    button.setAttribute("aria-label", `第 ${Math.floor(cell / state.cols) + 1} 行，第 ${cell % state.cols + 1} 列`);
    const value = state.cells[cell];
    const isHidden = value === "?";
    if (value >= "0" && value <= "8")
        button.classList.add("open");
    if (value >= "1" && value <= "8")
        button.classList.add(`number-${value}`);
    if (value === "S")
        button.classList.add("forced-safe");
    else if (value === "M")
        button.classList.add("forced-mine");
    if (mode === "play" && isHidden && flags.has(cell)) {
        button.classList.add("flagged");
        button.textContent = "⚑";
    } else if (value === "*") {
        button.classList.add("mine");
        if (cell === playState.lostCell)
            button.classList.add("hit-mine");
        button.textContent = "✹";
    } else if (probabilityDisplay.checked && probabilities) {
        const probability = probabilities[cell];
        const percentage = (probability * 100).toFixed(1);
        if (isHidden && probability === 0)
            button.classList.add("prob-safe");
        else if (isHidden && probability === 1)
            button.classList.add("prob-mine");
        if (isHidden) {
            button.classList.add("probability-cell");
            button.textContent = percentage;
        } else {
            button.textContent = value === "0" ? "" : value;
        }
    } else if (!isHidden) {
        button.textContent = value === "0" ? "" : value;
    }
    if (mode === "analysis" && selectedCell === cell)
        button.classList.add("selected");
    if (mode === "analysis" && hoveredCell === cell)
        button.classList.add("hovered");
    if (endgameResultAvailable && endgameCandidateCells.has(cell))
        button.classList.add("endgame-candidate");
    if (endgameResultAvailable && endgameBestCells.has(cell))
        button.classList.add("endgame-best");
}

function makeCell(cell, state) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "cell";
    button.dataset.cell = cell;
    button.setAttribute("role", "gridcell");
    button.addEventListener("mouseenter", () => {
        selectAnalysisCell(cell);
    });
    button.addEventListener("mouseleave", () => {
        if (hoveredCell === cell)
            selectAnalysisCell(-1);
    });
    button.addEventListener("click", (event) => {
        if (mode === "analysis") {
            selectedCell = cell;
            cellState.value = analysisState.cells[cell];
            analysisEditor.hidden = false;
            render();
            requestDetail(cell);
        } else if (event.detail === 0) {
            activatePlayCell(cell);
        }
    });
    button.addEventListener("contextmenu", (event) => event.preventDefault());
    updateCell(button, cell, state);
    return button;
}

function render() {
    const state = activeState();
    if (!state)
        return;
    clearPressedCells();
    boardElement.style.gridTemplateColumns = `repeat(${state.cols}, var(--cell-size, 34px))`;
    const cellSize = cellSizeSetting.value === "auto" ? Math.max(25, Math.min(38, Math.floor((window.innerWidth - 370) / state.cols))) : Number(cellSizeSetting.value);
    boardElement.style.setProperty("--cell-size", `${cellSize}px`);
    if (boardElement.children.length !== state.cells.length) {
        boardElement.replaceChildren();
        for (let cell = 0; cell < state.cells.length; ++cell)
            boardElement.append(makeCell(cell, state));
    } else
        for (let cell = 0; cell < state.cells.length; ++cell)
            updateCell(boardElement.children[cell], cell, state);
    document.getElementById("play-mode").classList.toggle("active", mode === "play");
    document.getElementById("analysis-mode").classList.toggle("active", mode === "analysis");
    document.getElementById("play-actions").hidden = mode !== "play";
    document.getElementById("play-until-no-safe").disabled = operationBusy || playState.lost || playState.won;
    document.getElementById("endgame-analysis").disabled = operationBusy || (mode === "play" && (playState.lost || playState.won));
    endgameResult.hidden = !endgameResultAvailable;
    rngSeedInput.disabled = mode !== "play";
    regenerateSeedButton.disabled = mode !== "play";
    importBoardStringButton.disabled = mode !== "analysis";
    analysisEditor.hidden = mode !== "analysis" || selectedCell < 0;
    document.getElementById("board-status").textContent = mode === "play" ? playState.lost ? "本局结束" : playState.won ? "已获胜" : "游玩模式" : "分析模式";
    const marked = mode === "play" ? flags.size : 0;
    document.getElementById("mine-counter").textContent = `${state.mines - marked} 雷`;
}

function openAnalysis() {
    if (mode === "analysis")
        return;
    const snapshot = playState;
    analysisState = { rows: snapshot.rows, cols: snapshot.cols, mines: snapshot.mines, cells: snapshot.cells.replace(/\*/g, "?") };
    mode = "analysis";
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate(true);
    render();
    setMessage("已复制可见棋盘。", "success");
}

function startNewGame(seed = null) {
    let rows = Number(document.getElementById("rows").value);
    let cols = Number(document.getElementById("cols").value);
    let mines = Number(document.getElementById("mines").value);
    if (difficulty.value !== "custom") {
        const presets = {
            beginner: [9, 9, 10],
            intermediate: [16, 16, 40],
            advanced: [16, 30, 99]
        };
        [rows, cols, mines] = presets[difficulty.value];
    }
    if (!Number.isInteger(rows) || !Number.isInteger(cols) || !Number.isInteger(mines) || rows < 1 || cols < 1 || rows > 50 || cols > 50 || rows * cols > 2500 || mines < 0 || mines >= rows * cols) {
        setMessage("自定义盘面范围无效。", "error");
        return;
    }
    if (seed)
        rng = { lo: seed.lo, hi: seed.hi };
    playState = createPlayState(rows, cols, mines);
    rngSeedInput.value = formatRngSeed(rng);
    mode = "play";
    clearDetail();
    clearEndgameResult();
    flags = new Set();
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate(true);
    render();
    setMessage("新局已开始，首开安全。", "success");
}

function scheduleProbabilityUpdate(clearPrevious = false) {
    const revision = ++probabilityRevision;
    clearDetail();
    if (!probabilityDisplay.checked) {
        probabilities = null;
        candidateCount = "";
        probabilityPending = false;
        return;
    }
    if (clearPrevious) {
        probabilities = null;
        candidateCount = "";
    }
    probabilityPending = true;
    const requestedMode = mode;
    probabilityUpdateQueue = probabilityUpdateQueue.then(async () => {
        if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
            return;
        const state = activeState();
        const cells = requestedMode === "play" ? state.cells.replace(/\*/g, "?") : state.cells;
        const startedAt = performance.now();
        try {
            await post("/api/analysis/import", { rows: state.rows, cols: state.cols, mines: state.mines, cells });
            if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
                return;
            const result = await post("/api/analysis/run", { algo: shapeAlgo.value });
            if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
                return;
            probabilities = result.probabilities;
            candidateCount = result.candidateCount;
            analysisElapsedText = `概率分析耗时（前端）：${(performance.now() - startedAt).toFixed(1)} ms`;
            probabilityPending = false;
            render();
            setMessage("概率已更新。", "success");
            if (mode === "analysis" && selectedCell >= 0)
                requestDetail(selectedCell);
        } catch (error) {
            if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
                return;
            probabilities = null;
            candidateCount = "";
            probabilityPending = false;
            render();
            setMessage(error.message, "error");
        }
    });
}

function editAnalysisCell(cell, state) {
    if (cell < 0 || analysisState.cells[cell] === state)
        return;
    clearEndgameResult();
    const cells = analysisState.cells.split("");
    cells[cell] = state;
    analysisState = { ...analysisState, cells: cells.join("") };
    if (selectedCell >= 0)
        cellState.value = analysisState.cells[selectedCell];
    scheduleProbabilityUpdate();
    render();
    setMessage("盘面已修改。", "success");
}

async function playUntilNoSafe() {
    if (mode !== "play" || operationBusy || playState.lost || playState.won)
        return;
    operationBusy = true;
    document.getElementById("play-until-no-safe").disabled = true;
    document.getElementById("endgame-analysis").disabled = true;
    ++probabilityRevision;
    probabilityPending = false;
    clearEndgameResult();
    setMessage("");
    clearDetail();
    probabilities = null;
    candidateCount = "";
    let initiallyHidden = 0;
    for (let cell = 0; cell < playState.cells.length; ++cell)
        if (playState.cells[cell] === "?")
            ++initiallyHidden;
    let finalResult = null;
    try {
        await probabilityUpdateQueue;
        for (;;) {
            const state = playState;
            const cells = state.cells.replace(/\*/g, "?");
            await post("/api/analysis/import", { rows: state.rows, cols: state.cols, mines: state.mines, cells });
            const result = await post("/api/analysis/run", { algo: shapeAlgo.value });
            finalResult = result;
            const safeCells = [];
            for (let cell = 0; cell < state.cells.length; ++cell)
                if (state.cells[cell] === "?" && !flags.has(cell) && result.probabilities[cell] === 0)
                    safeCells.push(cell);
            if (safeCells.length === 0)
                break;
            playState = { ...playState };
            for (const cell of safeCells)
                flags.delete(cell);
            revealLocallyBatch(safeCells);
            if (playState.lost || playState.won)
                break;
        }
        if (!playState.lost && !playState.won && probabilityDisplay.checked) {
            probabilities = finalResult.probabilities;
            candidateCount = finalResult.candidateCount;
        }
        render();
        if (playState.lost)
            setMessage("踩到地雷，本局结束。", "error");
        else if (playState.won)
            setMessage("恭喜，所有安全格都已揭开。", "success");
        else {
            let hidden = 0;
            for (let cell = 0; cell < playState.cells.length; ++cell)
                if (playState.cells[cell] === "?")
                    ++hidden;
            setMessage(`自动揭开 ${initiallyHidden - hidden} 格；没有未标旗的确定安全格。`, "success");
        }
    } catch (error) {
        render();
        setMessage(error.message, "error");
    } finally {
        probabilityPending = false;
        operationBusy = false;
        render();
    }
}

function showEndgameResult(result) {
    endgameSummary.textContent = result.engine === "JavaEval" ? `候选方案 ${result.candidateCount}；JavaEval 推荐` : `候选方案 ${result.candidateCount}；搜索节点 ${result.nodes}`;
    endgameMoves.replaceChildren();
    endgameCandidateCells.clear();
    endgameBestCells.clear();
    if (result.engine === "JavaEval") {
        const state = activeState();
        endgameCandidateCells = new Set(result.candidates);
        if (result.bestCell < 0) {
            const item = document.createElement("li");
            item.textContent = "没有可推荐的隐藏格。";
            endgameMoves.append(item);
        } else {
            const cell = result.bestCell;
            const row = Math.floor(cell / state.cols) + 1;
            const col = cell % state.cols + 1;
            endgameCandidateCells.add(cell);
            endgameBestCells.add(cell);
            const item = document.createElement("li");
            item.textContent = `JavaEval 推荐：第 ${row} 行，第 ${col} 列`;
            endgameMoves.append(item);
        }
        endgameResultAvailable = true;
        endgameResult.hidden = false;
        return;
    }
    const moves = result.moves.slice().sort((left, right) => right.wins - left.wins || left.cell - right.cell);
    if (moves.length === 0) {
        const item = document.createElement("li");
        item.textContent = "没有可供分析的隐藏格。";
        endgameMoves.append(item);
    } else {
        endgameBestCells.add(moves[0].cell);
    }
    const state = activeState();
    for (let i = 0; i < moves.length; ++i) {
        const move = moves[i];
        endgameCandidateCells.add(move.cell);
        const item = document.createElement("li");
        const row = Math.floor(move.cell / state.cols) + 1;
        const col = move.cell % state.cols + 1;
        const rate = (move.wins / result.configurationCount * 100).toFixed(3);
        item.textContent = `第 ${row} 行，第 ${col} 列：最终获胜 ${move.wins} / ${result.configurationCount}（${rate}%）`;
        endgameMoves.append(item);
    }
    endgameResultAvailable = true;
    endgameResult.hidden = false;
}

async function analyzeEndgame() {
    if (operationBusy || (mode === "play" && (playState.lost || playState.won)))
        return;
    const limit = Number(endgameCandidateLimit.value);
    if (!Number.isInteger(limit) || limit < 1 || limit > 2147483647) {
        setMessage("JavaEval 切换阈值必须是正整数。", "error");
        return;
    }
    operationBusy = true;
    document.getElementById("play-until-no-safe").disabled = true;
    document.getElementById("endgame-analysis").disabled = true;
    ++probabilityRevision;
    probabilityPending = false;
    clearEndgameResult();
    const blockerTimer = setTimeout(() => { analysisBlocker.hidden = false; }, 250);
    try {
        await probabilityUpdateQueue;
        const state = activeState();
        const cells = mode === "play" ? state.cells.replace(/\*/g, "?") : state.cells;
        const result = await post("/api/endgame/analyze", { mode, rows: state.rows, cols: state.cols, mines: state.mines, cells, limit, algo: shapeAlgo.value, includeProbabilities: probabilityDisplay.checked, checkAllMoves: endgameCheckAllMoves.checked, multiThread: endgameMultithread.checked });
        if (probabilityDisplay.checked) {
            probabilities = result.probabilities;
            candidateCount = result.candidateCount;
        }
        showEndgameResult(result);
        setMessage("分析完成。", "success");
    } catch (error) {
        setMessage(error.message, "error");
    } finally {
        clearTimeout(blockerTimer);
        analysisBlocker.hidden = true;
        probabilityPending = false;
        operationBusy = false;
        render();
    }
}

document.addEventListener("mousedown", (event) => {
    if (operationBusy || mode !== "play" || !playState || playState.lost || playState.won)
        return;
    const cell = cellFromTarget(event.target);
    if (event.button === 2) {
        if (cell >= 0 && playState.cells[cell] === "?") {
            event.preventDefault();
            toggleFlag(cell);
        }
        return;
    }
    if (event.button !== 0 || cell < 0)
        return;
    event.preventDefault();
    leftMouseDown = true;
    updatePressedCells(cell);
});
document.addEventListener("mousemove", (event) => {
    if (operationBusy)
        return;
    if (mode === "analysis")
        selectAnalysisCell(cellFromTarget(event.target));
    if (leftMouseDown)
        updatePressedCells(cellFromTarget(event.target));
});
document.addEventListener("mouseup", (event) => {
    if (operationBusy || event.button !== 0 || !leftMouseDown)
        return;
    leftMouseDown = false;
    const cell = cellFromTarget(event.target);
    clearPressedCells();
    if (mode === "play")
        activatePlayCell(cell);
});

difficulty.addEventListener("change", () => {
    setDifficultyValues(difficulty.value);
    if (difficulty.value !== "custom")
        startNewGame();
});
document.getElementById("new-game").addEventListener("click", () => startNewGame());
document.getElementById("open-settings").addEventListener("click", () => {
    boardStringInput.value = currentBoardString();
    boardExportStatus.textContent = "";
    settingsDialog.showModal();
});
document.getElementById("close-settings").addEventListener("click", () => settingsDialog.close());
document.getElementById("export-davidnhill").addEventListener("click", () => {
    try {
        const board = JSON.parse(boardStringInput.value);
        if (!board || !Number.isInteger(board.rows) || !Number.isInteger(board.cols) || !Number.isInteger(board.mines) || board.rows < 1 || board.cols < 1 || board.rows > 50 || board.cols > 50 || board.mines < 0 || board.mines >= board.rows * board.cols)
            throw new Error("盘面参数无效");
        if (typeof board.cells !== "string" || board.cells.length !== board.rows * board.cols || /[^0-8?MS]/.test(board.cells))
            throw new Error("盘面数据无效");
        const lines = [`${board.cols}x${board.rows}x${board.mines}`];
        for (let row = 0; row < board.rows; ++row)
            lines.push(board.cells.slice(row * board.cols, (row + 1) * board.cols).replace(/[?MS]/g, "H"));
        boardStringInput.value = lines.join("\n");
        boardExportStatus.textContent = "已转换为 davidNhill 格式。";
    } catch (error) {
        boardExportStatus.textContent = `导出失败：${error.message}`;
    }
});
importBoardStringButton.addEventListener("click", () => {
    if (mode !== "analysis")
        return;
    try {
        const board = JSON.parse(boardStringInput.value);
        if (!board || typeof board !== "object")
            throw new Error("盘面参数无效");
        if (!Number.isInteger(board.rows) || !Number.isInteger(board.cols) || !Number.isInteger(board.mines))
            throw new Error("盘面参数无效");
        if (board.rows < 1 || board.cols < 1 || board.rows > 50 || board.cols > 50 || board.rows * board.cols > 2500 || board.mines < 0 || board.mines >= board.rows * board.cols)
            throw new Error("自定义盘面范围无效");
        if (typeof board.cells !== "string" || board.cells.length !== board.rows * board.cols)
            throw new Error("盘面数据长度无效");
        for (let cell = 0; cell < board.cells.length; ++cell) {
            const value = board.cells[cell];
            if (value !== "?" && value !== "M" && value !== "S" && !(value >= "0" && value <= "8"))
                throw new Error("盘面状态无效");
        }
        clearEndgameResult();
        analysisState = { rows: board.rows, cols: board.cols, mines: board.mines, cells: board.cells };
        hoveredCell = -1;
        selectedCell = -1;
        scheduleProbabilityUpdate(true);
        boardStringInput.value = currentBoardString();
        render();
        setMessage("盘面已导入。", "success");
        boardExportStatus.textContent = "盘面已导入到分析模式。";
    } catch (error) {
        boardExportStatus.textContent = `导入失败：${error.message}`;
    }
});
cellSizeSetting.addEventListener("change", render);
probabilityDisplay.addEventListener("change", () => {
    scheduleProbabilityUpdate();
    render();
});
shapeAlgo.addEventListener("change", () => {
    if (probabilityDisplay.checked) {
        scheduleProbabilityUpdate();
        render();
    }
});
regenerateSeedButton.addEventListener("click", () => {
    const seed = parseRngSeed(rngSeedInput.value.trim());
    if (!seed) {
        setMessage("RNG seed 必须是 32 位十六进制数。", "error");
        return;
    }
    if (seed.hi === rng.hi && seed.lo === rng.lo) {
        rngSeedInput.value = formatRngSeed(rng);
        return;
    }
    startNewGame(seed);
});
document.getElementById("play-mode").addEventListener("click", () => {
    if (mode !== "play") {
        mode = "play";
        scheduleProbabilityUpdate(true);
    }
    clearDetail();
    render();
    setMessage("");
});
document.getElementById("analysis-mode").addEventListener("click", openAnalysis);
document.getElementById("play-until-no-safe").addEventListener("click", playUntilNoSafe);
document.getElementById("endgame-analysis").addEventListener("click", analyzeEndgame);
document.getElementById("apply-edit").addEventListener("click", () => editAnalysisCell(selectedCell, cellState.value));

document.addEventListener("click", (event) => {
    if (operationBusy) {
        event.preventDefault();
        event.stopImmediatePropagation();
    }
}, true);
document.addEventListener("keydown", (event) => {
    if (operationBusy) {
        event.preventDefault();
        event.stopImmediatePropagation();
    }
}, true);
document.addEventListener("keydown", (event) => {
    if (operationBusy)
        return;
    if (mode === "analysis" && hoveredCell >= 0 && !event.repeat && !event.ctrlKey && !event.altKey && !event.metaKey && !event.target.closest("input, select, textarea, [contenteditable=true], button:not(.cell)")) {
        let state = "";
        if (event.key >= "0" && event.key <= "8")
            state = event.key;
        else if (event.key.toLowerCase() === "h")
            state = "?";
        else if (event.key.toLowerCase() === "s")
            state = "S";
        else if (event.key.toLowerCase() === "m")
            state = "M";
        if (state) {
            event.preventDefault();
            editAnalysisCell(hoveredCell, state);
            return;
        }
    }
    if (event.code !== "Space" || event.repeat || event.ctrlKey || event.altKey || event.metaKey)
        return;
    if (event.target.closest("input, select, textarea, [contenteditable=true], button:not(.cell)"))
        return;
    event.preventDefault();
    startNewGame();
});

setDifficultyValues(difficulty.value);
playState = createPlayState(Number(document.getElementById("rows").value), Number(document.getElementById("cols").value), Number(document.getElementById("mines").value));
rngSeedInput.value = formatRngSeed(rng);
scheduleProbabilityUpdate();
render();
