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
        if (typeof board.cells !== "string" || board.cells.length !== board.rows * board.cols || /[^0-8?MSG]/.test(board.cells))
            throw new Error("盘面数据无效");
        const lines = [`${board.cols}x${board.rows}x${board.mines}`];
        for (let row = 0; row < board.rows; ++row)
            lines.push(board.cells.slice(row * board.cols, (row + 1) * board.cols).replace(/[?MSG]/g, "H"));
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
        const source = boardStringInput.value.trim();
        let board;
        if (source.startsWith("{")) {
            board = JSON.parse(source);
        } else {
            const lines = source.split(/\r?\n/);
            const header = lines[0].match(/^(\d+)x(\d+)x(\d+)$/i);
            if (!header)
                throw new Error("无法识别盘面格式");
            if (lines.length !== Number(header[2]) + 1 || lines.slice(1).some((line) => line.length !== Number(header[1])))
                throw new Error("davidNhill 盘面行数或宽度无效");
            board = { rows: Number(header[2]), cols: Number(header[1]), mines: Number(header[3]), cells: lines.slice(1).join("").replace(/H/g, "?") };
        }
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
            if (value !== "?" && value !== "M" && value !== "S" && value !== "G" && !(value >= "0" && value <= "8"))
                throw new Error("盘面状态无效");
        }
        clearEndgameResult();
        analysisState = { rows: board.rows, cols: board.cols, mines: board.mines, cells: board.cells };
        hoveredCell = -1;
        selectedCell = -1;
        scheduleProbabilityUpdate();
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
        clearEndgameResult();
        mode = "play";
        scheduleProbabilityUpdate();
    }
    clearDetail();
    render();
    setMessage("");
});
document.getElementById("analysis-mode").addEventListener("click", openAnalysis);
document.getElementById("play-until-no-safe").addEventListener("click", playUntilNoSafe);
document.getElementById("endgame-analysis").addEventListener("click", requestSuggestion);
mctsButton.addEventListener("click", requestMcts);
cellState.addEventListener("change", () => editAnalysisCell(selectedCell, cellState.value));

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
        else if (event.key.toLowerCase() === "g")
            state = "G";
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
