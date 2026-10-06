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

function formatCount(value) {
    const scientific = typeof value === "string" && value.includes("e") ? value : Number(value).toExponential(6);
    const [mantissa, exponentText] = scientific.split("e");
    const exponent = Number(exponentText);
    if (exponent >= 15)
        return scientific;
    const unit = Math.max(0, Math.floor(exponent / 3));
    const digits = (Number(mantissa) * 10 ** (exponent - unit * 3)).toPrecision(7);
    return (unit <= 1 ? Number(digits).toString() : digits) + ["", "K", "M", "B", "T"][unit];
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
    const count = data.candidateCount || candidateCount;
    detailCandidates.textContent = count ? formatCount(count) : "—";
    observeResult.replaceChildren();
    if (!data.observe) {
        const item = document.createElement("div");
        item.className = "muted";
        item.textContent = state.cells[cell] === "M" ? "该格确定为雷，没有安全的观察分布。" : "该格已揭开，没有新的观察分布。";
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

function updateCell(button, cell, state) {
    const value = state.cells[cell];
    const isHidden = value === "?" || value === "G";
    const move = mode === "analysis" ? mctsMoves.get(cell) : null;
    const showMcts = (isHidden || value === "S") && move;
    const wasMcts = button.classList.contains("mcts-cell");
    button.className = "cell";
    if (value === "G")
        button.classList.add("mcts-goal");
    if (!showMcts || !wasMcts)
        button.textContent = "";
    if (wasMcts && !showMcts) {
        button.removeAttribute("data-mcts-winrate");
        button.removeAttribute("data-mcts-visits");
    }
    button.removeAttribute("title");
    button.setAttribute("aria-label", `第 ${Math.floor(cell / state.cols) + 1} 行，第 ${cell % state.cols + 1} 列`);
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
    } else if (showMcts) {
        button.classList.add("mcts-cell");
        if (cell === mctsResult.recommended)
            button.classList.add("mcts-best");
        button.dataset.mctsWinrate = `${(Number(move.winrate) * 100).toFixed(1).replace(/\.0$/, "")}%`;
        button.dataset.mctsVisits = mctsCountFormat.format(BigInt(move.visits));
        button.title = `采样 ${move.visits} 次，胜出 ${move.wins} 次，胜率 ${Number(move.winrate).toFixed(6)}`;
        button.setAttribute("aria-label", `第 ${Math.floor(cell / state.cols) + 1} 行，第 ${cell % state.cols + 1} 列，${button.title}`);
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
    renderMcts();
}

function showSuggestion(result, elapsedMs) {
    const bruteForce = result.engine === "BruteForce";
    endgameSummary.textContent = `分析耗时（前端）：${elapsedMs.toFixed(1)} ms；候选方案 ${formatCount(result.candidateCount)}；` + (result.engine === "Safe" ? "确定安全格" : bruteForce ? `搜索节点 ${formatCount(result.nodes)}` : `${result.engine} 推荐`);
    endgameMoves.replaceChildren();
    endgameCandidateCells = new Set(result.candidates);
    endgameBestCells = new Set(result.recommended);
    const moves = bruteForce ? result.moves.slice().sort((left, right) => right.wins - left.wins || left.cell - right.cell) : result.recommended.map((cell) => ({ cell }));
    if (moves.length === 0) {
        const item = document.createElement("li");
        item.textContent = "没有可推荐的隐藏格。";
        endgameMoves.append(item);
    }
    const state = activeState();
    for (let i = 0; i < moves.length; ++i) {
        const move = moves[i];
        endgameCandidateCells.add(move.cell);
        const item = document.createElement("li");
        const row = Math.floor(move.cell / state.cols) + 1;
        const col = move.cell % state.cols + 1;
        item.textContent = `第 ${row} 行，第 ${col} 列：` + (result.engine === "Safe" ? "确定安全" : bruteForce ? `最终获胜 ${formatCount(move.wins)} / ${formatCount(result.configurationCount)}（${(move.wins / result.configurationCount * 100).toFixed(3)}%）` : `${result.engine} 推荐`);
        endgameMoves.append(item);
    }
    endgameResultAvailable = true;
    endgameResult.hidden = false;
}
