const longPressFlag = document.getElementById("long-press-flag");
const longPressDuration = document.getElementById("long-press-duration");
longPressDuration.addEventListener("change", () => {
    if (longPressDuration.validity.valid)
        longPressDuration.defaultValue = longPressDuration.value;
    else
        longPressDuration.value = longPressDuration.defaultValue;
});

function activateBoardCell(cell, flag = false) {
    if (cell < 0 || operationBusy || (mode === "play" && (playState.lost || playState.won)))
        return;
    if (flag) {
        const value = activeState().cells[cell];
        if (mode === "analysis" ? !/[0-8]/.test(value) : value === "?")
            toggleFlag(cell);
    } else if (mode === "play") {
        activatePlayCell(cell);
    } else {
        selectedCell = cell;
        boardElement.children[cell].focus({ preventScroll: true });
        requestDetail(cell);
        render();
    }
}

function clearBoardPress(pointerId) {
    const press = boardPresses.get(pointerId);
    if (press) {
        clearTimeout(press.timer);
        boardPresses.delete(pointerId);
        updatePressedCells();
    }
    return press;
}

function scheduleBoardFlag(pointerId, press) {
    const remaining = Math.ceil(press.flagAt - performance.now());
    press.timer = setTimeout(() => {
        if (boardPresses.get(pointerId) !== press)
            return;
        if (performance.now() < press.flagAt) {
            scheduleBoardFlag(pointerId, press);
            return;
        }
        clearBoardPress(pointerId);
        if (mode !== "analysis" || press.cell === press.startCell)
            activateBoardCell(press.cell, true);
    }, Math.min(remaining, 0x7fffffff));
}

function beginBoardPress(event) {
    if ((event.button !== 0 && event.button !== 2) || !boardElement.contains(event.target))
        return;
    const cell = cellFromTarget(event.target);
    if (cell < 0 || operationBusy || (mode === "play" && (playState.lost || playState.won)))
        return;
    event.preventDefault();
    document.activeElement.blur();
    if (event.button === 2) {
        activateBoardCell(cell, true);
        return;
    }
    const flagAt = longPressFlag.checked ? event.timeStamp + longPressDuration.valueAsNumber : Infinity;
    const press = { startCell: cell, cell, flagAt, timer: undefined };
    boardPresses.set(event.pointerId, press);
    if (Number.isFinite(flagAt))
        scheduleBoardFlag(event.pointerId, press);
    updatePressedCells();
}

function finishBoardPress(event) {
    if (event.button !== 0)
        return;
    const press = clearBoardPress(event.pointerId);
    if (!press)
        return;
    const cell = cellFromTarget(document.elementFromPoint(event.clientX, event.clientY));
    if (mode === "analysis" && cell !== press.startCell)
        return;
    activateBoardCell(cell, event.timeStamp >= press.flagAt);
}

boardElement.addEventListener("pointerdown", beginBoardPress);
document.addEventListener("pointerup", finishBoardPress);
document.addEventListener("pointermove", (event) => {
    // 鼠标按住一个键再按另一个键时，浏览器用 pointermove 报告按键变化。
    if ((event.button === 0 && (event.buttons & 1)) || (event.button === 2 && (event.buttons & 2)))
        beginBoardPress(event);
    else if (event.button === 0)
        finishBoardPress(event);

    if (!operationBusy && mode === "analysis")
        selectAnalysisCell(cellFromTarget(event.target));
    const press = boardPresses.get(event.pointerId);
    if (!press)
        return;
    const cell = cellFromTarget(document.elementFromPoint(event.clientX, event.clientY));
    if (press.cell !== cell) {
        press.cell = cell;
        updatePressedCells();
    }
});
document.addEventListener("pointercancel", (event) => {
    clearBoardPress(event.pointerId);
});
window.addEventListener("blur", () => {
    boardPresses.clear();
    clearPressedCells();
});
boardElement.addEventListener("click", (event) => {
    // 指针事件已经处理棋盘操作，这里接收键盘和程序触发的点击。
    if (event.detail === 0)
        activateBoardCell(cellFromTarget(event.target));
});

const newDialog = document.getElementById("new-dialog");
const dataDialog = document.getElementById("data-dialog");
const ioeDialog = document.getElementById("ioe-dialog");
const rowsInput = document.getElementById("rows");
const colsInput = document.getElementById("cols");
const minesInput = document.getElementById("mines");
const newForm = document.getElementById("new-form");
const effInput = document.getElementById("eff-zne");
const effDom = document.getElementById("eff-dom");
const effWarning = document.getElementById("eff-zne-warning");
const effProgress = document.getElementById("eff-progress");
const reuseSeed = document.getElementById("reuse-seed");
const effDefaults = { beginner: 200, intermediate: 180, advanced: 160 };
let playDifficulty = difficulty.value;
let effDifficulty = playDifficulty;
effDom.checked = effDifficulty === "beginner";
let effActive = false;
let effThreshold = "";
let effGeneration = null;

function updateGameForm() {
    customSettings.hidden = difficulty.value !== "custom";
    customSettings.disabled = customSettings.hidden || !!effGeneration;
    minesInput.max = Math.max(0, rowsInput.valueAsNumber * colsInput.valueAsNumber - 1);
}

function updateEffInputFeedback() {
    effInput.setCustomValidity(effThreshold && !(effInput.valueAsNumber > 0) ? tr("invalidEffInput") : "");
    effWarning.hidden = !(effInput.valueAsNumber > effDefaults[effDifficulty]);
}

function updateEffForm() {
    if (effDifficulty !== difficulty.value) {
        effDifficulty = difficulty.value;
        effDom.checked = effDifficulty === "beginner";
        effActive = false;
        effThreshold = "";
    }
    effInput.value = effThreshold;
    effInput.placeholder = effActive ? tr("noEffLimit") : effDefaults[effDifficulty] ?? tr("noEffLimit");
    effInput.classList.toggle("eff-inactive", !effActive);
    effDom.disabled = effDifficulty === "custom" || !!effGeneration;
    updateEffInputFeedback();
}

function showNewGame() {
    const { rows, cols, mines } = playState;
    difficulty.value = playDifficulty;
    rowsInput.value = rows;
    colsInput.value = cols;
    minesInput.value = mines;
    rngSeedInput.value = "";
    reuseSeed.disabled = !playState.seed;
    setLocalizedText(effProgress);
    updateGameForm();
    updateEffForm();
    newDialog.showModal();
    difficulty.focus();
}

function setGeneration(controller) {
    effGeneration?.abort();
    effGeneration = controller;
    const busy = !!controller;
    for (const control of [difficulty, effInput, rngSeedInput, newForm.querySelector('[type="submit"]')])
        control.disabled = busy;
    reuseSeed.disabled = busy || !playState.seed;
    effDom.disabled = busy || effDifficulty === "custom";
    updateGameForm();
}

async function generateNewGame(size, level, seed, threshold, dom) {
    const controller = new AbortController();
    setGeneration(controller);
    let nextSeed = formatRngSeed(seed ?? rng);
    let attempts = 0, domCalls = 0;
    let refreshAt = performance.now() + 150;
    const showProgress = () => {
        setLocalizedText(effProgress, "effProgress", attempts, domCalls);
    };
    showProgress();
    const [rows, cols, mines] = size;
    try {
        await pauseSearch();
        while (newDialog.open && !controller.signal.aborted) {
            const result = await post("/api/eff-board", { rows, cols, mines, zne: threshold, seed: nextSeed, difficulty: level, dom }, controller.signal);
            if (!newDialog.open || controller.signal.aborted) return;
            attempts += result.attempts;
            domCalls += result.domCalls;
            if (result.minefield) {
                showProgress();
                playDifficulty = level;
                startNewGame(...size, parseRngSeed(result.nextSeed), result);
                newDialog.close();
                return;
            }
            nextSeed = result.nextSeed;
            if (performance.now() >= refreshAt) {
                showProgress();
                refreshAt = performance.now() + 150;
            }
        }
    } catch (error) {
        if (newDialog.open && !controller.signal.aborted) {
            const message = localizedError(error);
            setLocalizedText(effProgress, message.key, ...message.args);
        }
    } finally {
        if (effGeneration === controller) setGeneration(null);
    }
}

document.getElementById("new-game").addEventListener("click", showNewGame);
newDialog.addEventListener("close", () => {
    if (newDialog.open || !effGeneration) return;
    setGeneration(null);
});
difficulty.addEventListener("change", () => {
    updateGameForm();
    updateEffForm();
    setLocalizedText(effProgress);
});
effInput.addEventListener("focus", () => {
    if (effActive) return;
    effActive = true;
    effThreshold = effDefaults[effDifficulty]?.toString() ?? "";
    updateEffForm();
});
effInput.addEventListener("input", () => {
    effThreshold = effInput.value;
    updateEffInputFeedback();
});
customSettings.addEventListener("input", updateGameForm);
reuseSeed.addEventListener("click", () => {
    rngSeedInput.value = playState.seed;
    rngSeedInput.select();
});
newForm.addEventListener("submit", event => {
    event.preventDefault();
    if (effGeneration) return;
    const level = difficulty.value;
    const size = presets[level] || [rowsInput.valueAsNumber, colsInput.valueAsNumber, minesInput.valueAsNumber];
    const seed = parseRngSeed(rngSeedInput.value);
    if (effThreshold)
        return generateNewGame(size, level, seed, effThreshold, effDom.checked);
    newDialog.close();
    playDifficulty = level;
    startNewGame(...size, seed);
});
document.getElementById("open-settings").addEventListener("click", () => settingsDialog.showModal());
const language = document.getElementById("language");
language.addEventListener("change", () => {
    applyLanguage(language.value);
    updateEffForm();
    if (ioeResult) showIoeResult(ioeResult);
    render();
});
applyLanguage(language.value);
settingsDialog.addEventListener("click", event => {
    if (event.target !== settingsDialog) return;
    const bounds = settingsDialog.getBoundingClientRect();
    if (event.clientX < bounds.left || event.clientX >= bounds.right || event.clientY < bounds.top || event.clientY >= bounds.bottom)
        settingsDialog.close();
});
for (const button of document.querySelectorAll("[data-close]"))
    button.addEventListener("click", () => button.closest("dialog").close());
document.getElementById("board-data").addEventListener("click", () => {
    boardStringInput.value = currentBoardString();
    boardStringInput.readOnly = mode === "play";
    const dataLabel = document.getElementById("data-label");
    dataLabel.dataset.i18n = mode === "play" ? "currentVisibleBoard" : "pasteableDraft";
    dataLabel.textContent = tr(dataLabel.dataset.i18n);
    importBoardStringButton.hidden = mode !== "analysis";
    setLocalizedText(boardExportStatus);
    dataDialog.showModal();
});
document.getElementById("export-json").addEventListener("click", () => {
    boardStringInput.value = currentBoardString();
    boardStringInput.select();
    setLocalizedText(boardExportStatus, "selectedJson");
});
document.getElementById("export-davidnhill").addEventListener("click", () => {
    const board = JSON.parse(currentBoardString());
    const lines = [`${board.cols}x${board.rows}x${board.mines}`];
    for (let row = 0; row < board.rows; ++row)
        lines.push(board.cells.slice(row * board.cols, (row + 1) * board.cols).replace(/[?MSG]/g, "H"));
    boardStringInput.value = lines.join("\n");
    boardStringInput.select();
    setLocalizedText(boardExportStatus, "selectedDavid");
});
function importBoardString(text) {
    const source = text.trim();
    let board;
    if (source.startsWith("{")) {
        try {
            board = JSON.parse(source);
        } catch (error) {
            if (!(error instanceof SyntaxError)) throw error;
            throw new UiError("unknownBoardFormat");
        }
    } else {
        const lines = source.split(/\r?\n/);
        const header = lines[0].match(/^(\d+)x(\d+)x(\d+)$/i);
        if (!header)
            throw new UiError("unknownBoardFormat");
        if (lines.length !== Number(header[2]) + 1 || lines.slice(1).some((line) => line.length !== Number(header[1])))
            throw new UiError("invalidDavidSize");
        board = { rows: Number(header[2]), cols: Number(header[1]), mines: Number(header[3]), cells: lines.slice(1).join("").replace(/H/g, "?") };
    }
    if (!board || typeof board !== "object")
        throw new UiError(ApiError.BoardParameters);
    if (!Number.isInteger(board.rows) || !Number.isInteger(board.cols) || !Number.isInteger(board.mines))
        throw new UiError(ApiError.BoardParameters);
    if (board.rows < 1 || board.cols < 1 || board.rows > 100 || board.cols > 100 || board.rows * board.cols > 10000 || board.mines < 0 || board.mines >= board.rows * board.cols)
        throw new UiError(ApiError.BoardRange);
    if (typeof board.cells !== "string" || board.cells.length !== board.rows * board.cols)
        throw new UiError("invalidBoardLength");
    for (let cell = 0; cell < board.cells.length; ++cell) {
        const value = board.cells[cell];
        if (value !== "?" && value !== "M" && value !== "S" && value !== "G" && !(value >= "0" && value <= "8"))
            throw new UiError("invalidCellState");
    }
    clearIoeResult();
    boardPresses.clear();
    analysisState = { rows: board.rows, cols: board.cols, mines: board.mines, cells: board.cells, clicks: null, playerFlags: new Set() };
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate();
    render();
    setMessage(boardMessage, "importedBoard", "success");
}

importBoardStringButton.addEventListener("click", () => {
    if (mode !== "analysis" || operationBusy) return;
    try {
        importBoardString(boardStringInput.value);
        dataDialog.close();
    } catch (error) {
        setLocalizedText(boardExportStatus, "importFailed", localizedError(error));
    }
});

document.addEventListener("paste", (event) => {
    if (mode !== "analysis" || operationBusy || document.querySelector("dialog[open]")) return;
    const target = event.target;
    if (target.isContentEditable || target.closest?.("input, textarea, select")) return;
    if (!boardElement.contains(target) && !boardElement.matches(":hover")) return;
    const text = event.clipboardData?.getData("text/plain");
    if (!text) return;
    event.preventDefault();
    try {
        importBoardString(text);
    } catch (error) {
        setMessage(boardMessage, "importFailed", "error", localizedError(error));
    }
});
window.addEventListener("resize", render);
probabilityDisplay.addEventListener("change", () => {
    clearIoeResult();
    scheduleProbabilityUpdate(true);
    render();
});
highlightDisplay.addEventListener("change", async () => {
    showHighlight();
    if (mode === "analysis" && selectedCell >= 0) requestDetail(selectedCell);
    const highlight = highlightDisplay.value;
    if (!highlight || (mode === "play" && (playState.lost || playState.won))) return;
    const state = activeState();
    try {
        await calculate(state, { includeProbabilities: false, highlight });
    } catch (error) {
        if (highlight === highlightDisplay.value && state === activeState()) setErrorMessage(boardMessage, error);
    }
});
shapeAlgo.addEventListener("change", () => {
    clearIoeResult();
    scheduleProbabilityUpdate(true);
    if (!probabilityDisplay.checked && !highlightDisplay.value && mode === "analysis" && selectedCell >= 0) requestDetail(selectedCell);
    render();
});
function updatePolicySettings() {
    document.getElementById("brute-settings").hidden = !bruteEnabled.checked;
    document.getElementById("endgame-max-threads-row").hidden = !endgameMultithread.checked;
    for (const input of [endgameCandidateLimit, endgameCheckAllMoves, endgameMultithread]) input.disabled = !bruteEnabled.checked;
    endgameMaxThreads.disabled = !bruteEnabled.checked || !endgameMultithread.checked;
}
cellSizeSetting.addEventListener("change", render);
for (const input of [templatePrefix, ltrEventLimit, policyNodeBudget, bruteEnabled, endgameCandidateLimit, endgameCheckAllMoves, endgameMultithread, endgameMaxThreads, ...searchWidths]) input.addEventListener("change", () => {
    stopAnalysis();
    clearIoeResult();
    updatePolicySettings();
    if ((input === templatePrefix || input === ltrEventLimit) && mode === "analysis" && selectedCell >= 0) requestDetail(selectedCell);
    render();
});
bruteEnabled.checked = Number(endgameCandidateLimit.value) > 0;
endgameCheckAllMoves.value = endgameCheckAllMoves.dataset.all === "checked" ? "all" : "best";
updatePolicySettings();
document.getElementById("play-mode").addEventListener("change", () => {
    clearIoeResult();
    boardPresses.clear();
    mode = "play";
    hoveredCell = -1;
    scheduleProbabilityUpdate();
    render();
    if (playState.won) showVictory(playState.finishedAt);
    else setMessage(boardMessage, playState.lost ? "lostMessage" : "", playState.lost ? "error" : "");
});
document.getElementById("analysis-mode").addEventListener("change", () => {
    boardPresses.clear();
    openAnalysis();
});
document.getElementById("cell-editor").addEventListener("click", event => {
    const key = event.target.closest("button[data-value]");
    if (!key || selectedCell < 0) return;
    editAnalysisCell(selectedCell, key.dataset.value);
    boardElement.children[selectedCell].focus({ preventScroll: true });
});
document.getElementById("play-until-no-safe").addEventListener("click", playUntilNoSafe);
document.getElementById("ioe-analysis").addEventListener("click", () => {
    const clicks = activeState().clicks;
    if (clicks !== null) return requestIoeAnalysis(clicks);
    const input = document.getElementById("ioe-clicks");
    input.value = "";
    ioeDialog.showModal();
    input.focus();
});
document.getElementById("ioe-form").addEventListener("submit", event => {
    event.preventDefault();
    ioeDialog.close();
    requestIoeAnalysis(document.getElementById("ioe-clicks").valueAsNumber);
});
searchButton.addEventListener("click", requestSearch);

document.addEventListener("keydown", (event) => {
    if (operationBusy || document.querySelector("dialog[open]") || event.repeat || event.ctrlKey || event.altKey || event.metaKey)
        return;
    if (mode === "analysis") {
        const cell = cellFromTarget(event.target);
        if (event.key.startsWith("Arrow")) {
            if (cell < 0) return;
            const { rows, cols } = analysisState;
            let target = cell;
            if (event.key === "ArrowLeft" && cell % cols > 0) --target;
            else if (event.key === "ArrowRight" && cell % cols < cols - 1) ++target;
            else if (event.key === "ArrowUp" && cell >= cols) target -= cols;
            else if (event.key === "ArrowDown" && cell < (rows - 1) * cols) target += cols;
            event.preventDefault();
            boardElement.children[target].click();
            boardElement.children[target].scrollIntoView({ block: "nearest", inline: "nearest" });
            return;
        }
        const key = event.key.toUpperCase();
        if (!/^[0-8HSMG]$/.test(key) || event.target.isContentEditable ||
            event.target.closest("input:not(#analysis-mode), select, textarea"))
            return;
        const target = hoveredCell >= 0 ? hoveredCell : cell === selectedCell ? cell : -1;
        if (target < 0) return;
        event.preventDefault();
        editAnalysisCell(target, key === "H" ? "?" : key);
        return;
    }
    if (event.code !== "Space")
        return;
    if (event.target.closest("input, select, textarea, [contenteditable=true], button:not(.cell)"))
        return;
    event.preventDefault();
    const size = [playState.rows, playState.cols, playState.mines];
    if (effDifficulty === playDifficulty && effThreshold) {
        showNewGame();
        newForm.requestSubmit();
    } else {
        startNewGame(...size);
    }
});

startNewGame(...presets.advanced);
