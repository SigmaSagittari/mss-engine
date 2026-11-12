function setMessage(text, kind = "") {
    messageElement.hidden = !text;
    messageElement.textContent = text;
    messageElement.className = `message ${kind}`;
}

function currentBoardString() {
    return JSON.stringify(boardSnapshot(activeState()));
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
    const size = presets[value];
    if (size) {
        const [rows, cols, mines] = size;
        document.getElementById("rows").value = rows;
        document.getElementById("cols").value = cols;
        document.getElementById("mines").value = mines;
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

function formatPercent(value) {
    const percent = Number(value) * 100;
    return `${percent > 0 && percent < 0.0001 ? percent.toExponential(3) : percent.toFixed(4)}%`;
}

function renderAnalysisResult() {
    const now = performance.now();
    let summary = "";
    let recommended = [];
    let safe = false;
    if (mcts.result || mcts.controller) {
        const result = mcts.result;
        const samples = result?.simulations || 0;
        let speed = 0;
        if (mcts.controller && mcts.samples.length > 1) {
            const first = mcts.samples[0];
            const next = mcts.samples[1];
            const start = Math.max(first.time, now - 1000);
            const baseline = first.count + (next.count - first.count) * (start - first.time) / (next.time - first.time);
            speed = (samples - baseline) * 1000 / (now - start);
        }
        summary = result?.exact ? "精确枚举 · 已完成" : `蒙特卡洛 · ${formatCount(samples)} 次模拟 · ${formatCount(Math.round(speed))} 局/s · ${mcts.controller ? "运行中" : "已停止"}`;
        if (result?.winrate && samples > 0) summary += ` · 整局胜率 ${formatPercent(result.value)}`;
        if (result && result.recommended >= 0) recommended = [result.recommended];
    } else if (search.result || search.controller) {
        const result = search.result;
        const speed = search.controller && result && !result.paused && !result.finished ? (result.nodes - search.samples[0].count) * 1000 / Math.max(1, now - search.samples[0].time) : 0;
        const engine = result?.engine === "BruteForce" ? "精确枚举" : result?.safe ? "确定安全" : "Anytime";
        summary = result ? `${engine} · ${formatCount(result.nodes)} 节点 · ${formatCount(Math.round(speed))} 节点/s · ${result.finished ? "已完成" : result.paused ? "已暂停" : "运行中"}` : "Anytime · 正在开始";
        if (result?.engine === "Anytime") summary += ` · 全部候选完成第 ${result.completedDepth} 层`;
        safe = result?.safe === true;
        if (safe) recommended = result.moves.map((move) => move.cell);
        else if (result && result.recommended >= 0) recommended = [result.recommended];
    } else if (suggestionResult) {
        const result = suggestionResult;
        const names = { Finished: "棋盘已完成", Safe: "确定安全", LowRisk: "LowRisk", Anytime: "Anytime", BruteForce: "精确枚举" };
        summary = `${names[result.engine]} · 分析耗时 ${result.elapsedMs.toFixed(1)} ms`;
        if (result.engine === "Anytime" || result.engine === "BruteForce") summary += ` · ${formatCount(result.nodes)} 节点`;
        recommended = result.recommended;
        safe = result.engine === "Safe";
    }
    analysisSummary.hidden = !summary;
    analysisSummary.textContent = summary;
    analysisCountRow.hidden = !candidateCount;
    analysisCount.textContent = candidateCount ? formatCount(candidateCount) : "";
    analysisRecommendation.hidden = recommended.length === 0;
    const state = activeState();
    analysisRecommendation.textContent = safe ? `全部安全格（${recommended.length} 个）` : recommended.length ? `推荐：${recommended.map((cell) => `第 ${Math.floor(cell / state.cols) + 1} 行，第 ${cell % state.cols + 1} 列`).join("；")}` : "";
}

function renderObserve(branches) {
    const observation = detailData?.observe;
    observeResult.hidden = !observation;
    if (!observation) return;
    const hasScores = branches.length > 0;
    document.getElementById("observe-score-heading").hidden = !hasScores;
    const body = observeResult.tBodies[0];
    let index = 0;
    for (let digit = 0; digit <= 9; ++digit) {
        if (Number(observation[digit]) === 0) continue;
        let row = body.rows[index];
        if (!row) {
            row = body.insertRow();
            row.insertCell(); row.insertCell(); row.insertCell();
        }
        row.cells[0].textContent = digit === 9 ? "爆炸" : `数字 ${digit}`;
        row.cells[1].textContent = formatPercent(observation[digit]);
        row.cells[2].hidden = !hasScores;
        if (hasScores) {
            const branch = branches.find((item) => item.digit === digit);
            row.cells[2].textContent = digit === 9 ? "0%" : formatPercent(branch.value);
        }
        ++index;
    }
    while (body.rows.length > index) body.deleteRow(index);
}

function renderDetail() {
    detailPanel.hidden = mode !== "analysis" || selectedCell < 0;
    if (detailPanel.hidden) return;
    const state = activeState();
    detailPosition.textContent = `第 ${Math.floor(selectedCell / state.cols) + 1} 行，第 ${selectedCell % state.cols + 1} 列`;
    detailStatus.hidden = !detailStatus.textContent;
    detailContent.hidden = !detailData;
    if (detailData) detailRisk.textContent = formatPercent(detailData.probability);
    const risk = detailData?.longTermRisk;
    document.getElementById("detail-long-term-risk").hidden = !risk;
    if (risk) {
        const bodyProbability = document.getElementById("detail-risk-body-union");
        bodyProbability.textContent = formatPercent(risk.bodyUnion);
        bodyProbability.parentElement.hidden = Number.parseFloat(bodyProbability.textContent) === 0;
        const distribution = document.getElementById("detail-risk-restriction-distribution");
        distribution.textContent = "";
        const counts = risk.restrictionCounts;
        for (let k = 0; k < counts.length; ++k) {
            const probability = formatPercent(counts[k]);
            if (Number.parseFloat(probability) === 0) continue;
            const row = document.createElement("div");
            row.textContent = `命中 ${k} 个限制：${probability}`;
            distribution.appendChild(row);
        }
        distribution.previousElementSibling.hidden = distribution.childElementCount === 0;
    }
    let engine = null;
    let move = null;
    if (search.result) {
        engine = search.result.engine;
        move = search.moves.get(selectedCell);
    } else if (mcts.result) {
        engine = mcts.result.exact ? "BruteForce" : "MonteCarlo";
        move = mcts.moves.get(selectedCell);
    } else if (suggestionResult) {
        engine = suggestionResult.engine;
        move = suggestionMoves.get(selectedCell);
    }
    detailScoreRow.hidden = !move;
    const names = { Safe: "确定安全", LowRisk: "LowRisk", Anytime: "Anytime", BruteForce: "精确枚举", MonteCarlo: "蒙特卡洛" };
    detailEngine.textContent = move ? names[engine] : "";
    if (move) {
        const labels = { Safe: "安全率", LowRisk: "安全率", Anytime: "当前评分", BruteForce: "精确胜率", MonteCarlo: "胜率" };
        detailScoreLabel.textContent = labels[engine];
        const score = engine === "MonteCarlo" || (mcts.result && engine === "BruteForce") ? move.winrate : move.value;
        detailScore.textContent = score === null ? "—" : formatPercent(score);
    }
    mctsDetail.hidden = engine !== "MonteCarlo" || !move;
    if (!mctsDetail.hidden) {
        mctsVisits.textContent = move.visits;
        mctsWins.textContent = move.wins;
    }
    renderObserve(engine === "Anytime" && move ? move.branches : []);
    const history = engine === "Anytime" && move ? move.history : [];
    searchHistory.hidden = history.length === 0;
    detailSide.hidden = mctsDetail.hidden && searchHistory.hidden;
    detailColumns.classList.toggle("split", !detailSide.hidden);
    const body = searchHistory.tBodies[0];
    for (let index = 0; index < history.length; ++index) {
        let row = body.rows[index];
        if (!row) { row = body.insertRow(); row.insertCell(); row.insertCell(); }
        row.cells[0].textContent = history[index].depth;
        row.cells[1].textContent = formatPercent(history[index].value);
        row.classList.toggle("latest", index === history.length - 1);
    }
    while (body.rows.length > history.length) body.deleteRow(history.length);
}

function showDetail(data) {
    detailData = data;
    detailStatus.textContent = "";
    renderDetail();
}

function clearDetail() {
    ++detailRequest;
    selectedCell = -1;
    detailData = null;
    detailStatus.textContent = "";
    detailPanel.hidden = true;
}

function clearSuggestion() {
    suggestionResult = null;
    for (const cell of suggestionMoves.keys())
        boardElement.children[cell].classList.remove("analysis-candidate", "analysis-best");
    suggestionMoves.clear();
    endgameBestCells.clear();
    renderAnalysisResult();
    renderDetail();
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
    const move = mode === "analysis" ? mcts.moves.get(cell) : null;
    const showMcts = (isHidden || value === "S") && move;
    const searchMove = mode === "analysis" ? search.moves.get(cell) : null;
    const showSearch = (isHidden || value === "S") && searchMove;
    const suggested = suggestionMoves.get(cell);
    const showSuggestion = (isHidden || value === "S") && suggested;
    // 先算出最终样式和文字；概率刷新时只写变化项，不清空再恢复整格。
    let className = "cell";
    let text = "";
    if (value === "G")
        className += " mcts-goal";
    if (!showSearch && !showMcts && !showSuggestion) button.removeAttribute("data-analysis-detail");
    button.removeAttribute("title");
    button.setAttribute("aria-label", `第 ${Math.floor(cell / state.cols) + 1} 行，第 ${cell % state.cols + 1} 列`);
    if (value >= "0" && value <= "8")
        className += " open";
    if (value >= "1" && value <= "8")
        className += ` number-${value}`;
    if (value === "S")
        className += " forced-safe";
    else if (value === "M")
        className += " forced-mine";
    if (mode === "play" && isHidden && flags.has(cell)) {
        className += " flagged";
        text = "⚑";
    } else if (value === "*") {
        className += " mine";
        if (cell === playState.lostCell)
            className += " hit-mine";
        text = "✹";
    } else if (showSearch || showMcts || showSuggestion) {
        className += " analysis-candidate";
        let score;
        let detail;
        let best;
        if (showSearch) {
            score = searchMove.value;
            detail = search.result.engine === "BruteForce" ? "精确" : `${searchMove.depth} 层`;
            best = search.result.safe || cell === search.result.recommended;
            button.title = `评分 ${(Number(score) * 100).toFixed(4)}%，${detail}`;
        } else if (showMcts) {
            score = move.winrate;
            detail = mcts.result.exact ? "精确" : mctsCountFormat.format(BigInt(move.visits));
            best = cell === mcts.result.recommended;
            button.title = mcts.result.exact ? `精确胜率 ${(Number(score) * 100).toFixed(4)}%` : `采样 ${move.visits} 次，胜出 ${move.wins} 次，胜率 ${score === null ? "尚未采样" : `${(Number(score) * 100).toFixed(4)}%`}`;
        } else {
            score = suggested.value;
            detail = suggested.depth === undefined ? `${(Number(suggested.probability) * 100).toFixed(1)}%` : `${suggested.depth} 层`;
            best = endgameBestCells.has(cell);
            button.title = `评分 ${(Number(score) * 100).toFixed(4)}%，${suggested.depth === undefined ? "雷率 " : ""}${detail}`;
        }
        if (best) className += " analysis-best";
        text = score === null ? "—" : `${(Number(score) * 100).toFixed(1)}%`;
        button.dataset.analysisDetail = detail;
        button.setAttribute("aria-label", `${button.getAttribute("aria-label")}，${button.title}`);
    } else if (isHidden && probabilityDisplay.checked && (probabilities || probabilityPending)) {
        className += " probability-cell";
        if (probabilities) {
            const probability = probabilities[cell];
            if (probability === 0)
                className += " prob-safe";
            else if (probability === 1)
                className += " prob-mine";
            text = (probability * 100).toFixed(1).replace(/^100\.0$/, "100");
        } else {
            // 概率请求串行等待时保留当前显示，结果返回后再覆盖数字和底色。
            if (button.classList.contains("prob-safe"))
                className += " prob-safe";
            else if (button.classList.contains("prob-mine"))
                className += " prob-mine";
            text = button.classList.contains("probability-cell") ? button.textContent : "";
        }
    } else if (!isHidden) {
        text = value === "0" ? "" : value;
    }
    if (mode === "analysis" && selectedCell === cell)
        className += " selected";
    if (mode === "analysis" && hoveredCell === cell)
        className += " hovered";
    if (button.className !== className)
        button.className = className;
    if (button.textContent !== text)
        button.textContent = text;
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
            requestDetail(cell);
            render();
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
    const cellSize = cellSizeSetting.value === "auto" ? Math.max(25, Math.min(38, Math.floor((boardElement.closest(".game-area").clientWidth - 60) / state.cols))) : Number(cellSizeSetting.value);
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
    rngSeedInput.disabled = mode !== "play";
    regenerateSeedButton.disabled = mode !== "play";
    importBoardStringButton.disabled = mode !== "analysis";
    document.getElementById("board-status").textContent = mode === "play" ? playState.lost ? "本局结束" : playState.won ? "已获胜" : "游玩模式" : "分析模式";
    const marked = mode === "play" ? flags.size : 0;
    document.getElementById("mine-counter").textContent = `${state.mines - marked} 雷`;
    mcts.render();
    search.render();
}

function showSuggestion(result, elapsedMs) {
    suggestionResult = { ...result, elapsedMs };
    suggestionMoves = new Map(result.moves.map((move) => [move.cell, move]));
    endgameBestCells = new Set(result.recommended);
    candidateCount = result.candidateCount;
    renderAnalysisResult();
    renderDetail();
}
