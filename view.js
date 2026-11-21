function setMessage(element, key = "", kind = "", ...args) {
    element.hidden = !key;
    setLocalizedText(element, key, ...args);
    element.className = `message ${kind}`;
}

function setErrorMessage(element, error) {
    const message = localizedError(error);
    setMessage(element, message.key, "error", ...message.args);
}

function currentBoardString() {
    return JSON.stringify(boardSnapshot(activeState()));
}

function cellFromTarget(target) {
    const button = target?.closest(".cell");
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
    const result = search.result;
    const running = search.controller && result && !result.paused && !result.finished && !result.limitReached;
    let status = "";
    if (result?.finished) status = result.engine === SearchEngine.BruteForce ? "exactComplete" : "searchComplete";
    else if (result?.limitReached) status = "nodeLimitReached";
    else if (result?.paused) status = "searchPaused";
    else if (search.controller) status = result ? "searching" : "startingSearch";
    const sample = search.samples[0];
    const speed = running ? (result.nodes - sample.count) * 1000 / Math.max(1, performance.now() - sample.time) : 0;
    const safe = result?.safe === true;
    const recommended = safe ? result.moves.map((move) => move.cell) : result?.recommended >= 0 ? [result.recommended] : [];
    analysisSummary.hidden = !status;
    searchStatus.textContent = status ? tr(status) : "";
    searchCount.textContent = result?.nodes > 0 ? tr("nodeCount", formatCount(result.nodes)) : "";
    searchSpeed.textContent = speed > 0 ? tr("nodeSpeed", formatCount(Math.round(speed))) : "";
    searchDepth.textContent = result?.completedDepth > 0 ? tr("completedDepth", result.completedDepth) : "";
    const hasIoeDetails = !ioeResultPanel.hidden && !ioeResultDetails.hidden;
    const count = hasIoeDetails ? ioeResult.candidateCount : candidateCount;
    boardCountRow.hidden = !count;
    boardCount.textContent = count ? (hasIoeDetails ? Number(count).toLocaleString(locale) : formatCount(count)) : "";
    analysisRecommendation.hidden = recommended.length === 0;
    const state = activeState();
    analysisRecommendation.textContent = safe ? tr("safeCells", recommended.length) :
        recommended.length ? tr("recommendedCells", recommended.map(cell => cellPosition(cell, state))) : "";
}

function cellPosition(cell, state) {
    return tr("cellPosition", Math.floor(cell / state.cols) + 1, cell % state.cols + 1);
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
        row.cells[0].textContent = digit === 9 ? tr("mineHit") : tr("numberOutcome", digit);
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
    detailPosition.textContent = cellPosition(selectedCell, state);
    detailStatus.hidden = !detailStatus.textContent;
    detailContent.hidden = !detailData;
    if (detailData) detailRisk.textContent = formatPercent(detailData.probability);
    const groupTable = document.getElementById("detail-group-mines");
    const counts = detailData?.groupMineCounts;
    groupTable.hidden = !highlightDisplay.value || !counts?.length;
    if (!groupTable.hidden) {
        groupTable.caption.textContent = highlightDisplay.selectedOptions[0].textContent;
        const body = groupTable.tBodies[0];
        body.replaceChildren();
        for (const { mines, ways, probability } of counts) {
            const row = body.insertRow();
            row.insertCell().textContent = mines;
            row.insertCell().textContent = Number(ways) < 1e15 ? Number(ways).toLocaleString(locale) : ways;
            row.insertCell().textContent = formatPercent(probability);
        }
    }
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
            row.textContent = tr("restrictionsHit", k, probability);
            distribution.appendChild(row);
        }
        distribution.previousElementSibling.hidden = distribution.childElementCount === 0;
    }
    const engine = search.result?.engine;
    const move = search.moves.get(selectedCell);
    detailScoreRow.hidden = !move;
    const names = { [SearchEngine.Safe]: "guaranteedSafe", [SearchEngine.Anytime]: "anytime", [SearchEngine.BruteForce]: "exactEnumeration" };
    detailEngine.textContent = move ? tr(names[engine]) : "";
    if (move) {
        const labels = { [SearchEngine.Safe]: "safeChance", [SearchEngine.Anytime]: "currentScore", [SearchEngine.BruteForce]: "exactWinRate" };
        detailScoreLabel.textContent = tr(labels[engine]);
        const score = move.value;
        detailScore.textContent = score === null ? "—" : formatPercent(score);
    }
    renderObserve(engine === SearchEngine.Anytime && move ? move.branches : []);
    const history = engine === SearchEngine.Anytime && move ? move.history : [];
    searchHistory.hidden = history.length === 0;
    detailSide.hidden = searchHistory.hidden;
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
    setLocalizedText(detailStatus);
    renderDetail();
}

function clearDetail() {
    ++detailRequest;
    selectedCell = -1;
    detailData = null;
    setLocalizedText(detailStatus);
    detailPanel.hidden = true;
}

let ioeResult = null;
let ioeDetailsRequest = null;
const ioeDetailsButton = document.getElementById("ioe-calculate-details");
const ioeResultDetails = document.getElementById("ioe-result-details");

function clearIoeResult() {
    ioeResult = null;
    ioeDetailsRequest = null;
    ioeMove = null;
    ioeResultPanel.hidden = true;
    setMessage(analysisMessage, "");
}

const ioeBars = document.querySelector("#ioe-distribution ul");

function fitIoeLabels() {
    const count = ioeBars.children.length;
    const width = ioeBars.clientWidth;
    if (!count || !width) return;
    const maxLabels = Math.max(2, Math.floor(width / 64));
    const dense = count > maxLabels;
    ioeBars.classList.toggle("ioe-dense", dense);
    const labels = new Set();
    if (dense)
        for (let i = 0; i < maxLabels; ++i)
            labels.add(Math.round(i * (count - 1) / (maxLabels - 1)));
    let previousRight = -Infinity;
    for (let i = 0; i < count; ++i) {
        const label = ioeBars.children[i].lastElementChild;
        label.hidden = dense && !labels.has(i);
        if (label.hidden) continue;
        const { left, right } = label.getBoundingClientRect();
        if (left < previousRight + 4) label.hidden = true;
        else previousRight = right;
    }
}

window.addEventListener("resize", fitIoeLabels);

function showIoeResult(result) {
    if (result.status === IoeStatus.ProofOnly || result.status === IoeStatus.TooManyLayouts) {
        ioeResult = result;
        ioeResultDetails.hidden = true;
        ioeDetailsButton.hidden = false;
        ioeResultPanel.hidden = false;
        return;
    }
    const buckets = result.winningIoe;
    if (!Number.isInteger(result.completedLayouts) || buckets.some(bucket => bucket.length !== 5 || !Array.isArray(bucket[2]))) {
        clearIoeResult();
        throw new UiError("outdatedWasm");
    }
    ioeResult = result;
    const formatRate = count => {
        const percent = (count / result.candidateCount).toLocaleString(locale, { style: "percent", maximumFractionDigits: 2 });
        return `${count.toLocaleString(locale)}(${percent})`;
    };
    setLocalizedText(document.getElementById("ioe-rate"), "ioeRate", result.minIoePercent);
    document.getElementById("ioe-wins").textContent = `${formatRate(result.wins)}\u00a0/\u00a0${formatRate(result.rootEligible)}`;
    document.getElementById("ioe-completed-layouts").textContent = `${numberText(result.completedLayouts)}\u00a0/\u00a0${numberText(result.rootEligible)}`;
    document.getElementById("ioe-unsearched-legend").hidden = result.completedLayouts === result.rootEligible;
    const score = result.expectedScore === null ? Infinity : result.expectedScore;
    const notation = score >= 1e8 ? "scientific" : "standard";
    document.getElementById("ioe-score").textContent = result.rootEligible && !result.completedLayouts
        ? "—" : score.toLocaleString(locale, { notation, maximumSignificantDigits: 8 });
    const figure = document.getElementById("ioe-distribution");
    setLocalizedText(figure.querySelector("figcaption"), "ioeDistribution", result.minIoePercent);
    const maxExtra = Math.max(0, ...buckets.map(([, , wins]) => wins.length - 1));
    document.getElementById("ioe-extra-legend").hidden = maxExtra === 0;
    if (buckets.length) {
        const clickColors = Array.from({ length: maxExtra + 1 }, (_, extra) =>
            `color-mix(in hsl, var(--ioe-won-color), var(--ioe-extra-color) ${100 * extra / Math.max(1, maxExtra)}%)`);
        const chips = clickColors.slice(1).map((color, index) => {
            const chip = document.createElement("span");
            chip.className = "ioe-extra-chip";
            chip.style.setProperty("--ioe-color", color);
            chip.textContent = numberText(index + 1);
            return chip;
        });
        document.getElementById("ioe-extra-colors").replaceChildren(...chips);
        buckets.sort(([a, b], [c, d]) => a / b - c / d);
        const totals = buckets.map(([, , wins, discarded, unsearched]) => wins.reduce((sum, count) => sum + count, discarded + unsearched));
        const maxCount = Math.max(...totals);
        const axisMax = 10 ** Math.ceil(Math.log10(maxCount));
        const height = count => 100 * Math.log1p(count) / Math.log1p(axisMax);
        const axis = figure.querySelector(".ioe-y-axis");
        const ticks = document.createDocumentFragment();
        const maxLabel = axisMax.toLocaleString(locale);
        axis.style.minWidth = `${maxLabel.length + 1}ch`;
        for (let count = 0; count <= axisMax; count = count ? count * 10 : 1) {
            const tick = document.createElement("span");
            tick.textContent = count.toLocaleString(locale);
            tick.style.top = `${100 - height(count)}%`;
            ticks.append(tick);
        }
        axis.replaceChildren(ticks);

        const rows = document.createDocumentFragment();
        for (const [index, [numerator, denominator, wins, discarded, unsearched]] of buckets.entries()) {
            const label = document.createElement("span");
            label.textContent = (numerator / denominator).toLocaleString(locale, { style: "percent", maximumFractionDigits: 2 });
            const track = document.createElement("div");
            track.className = "ioe-bar-track";
            const total = totals[index];
            const totalHeight = height(total);
            track.style.setProperty("--ioe-bar-height", `${totalHeight}%`);
            let bottom = 0;
            const segments = [
                ...wins.map((count, extra) => [count, extra ? tr("ioeExtraClicks", extra) : tr("ioeFeasible"), clickColors[extra]]),
                [discarded, tr("ioeDiscarded"), "var(--ioe-discarded-color)"],
                [unsearched, tr("ioeUnsearched"), "var(--ioe-unsearched-color)"]
            ];
            const details = [];
            for (const [count, description, color] of segments) {
                if (!count) continue;
                const barHeight = totalHeight * count / total;
                const bar = document.createElement("span");
                bar.className = "ioe-bar";
                bar.style.setProperty("--ioe-color", color);
                details.push(`${description} · ${numberText(count)}`);
                bar.style.bottom = `${bottom}%`;
                bar.style.height = `${barHeight}%`;
                track.append(bar);
                bottom += barHeight;
            }
            const value = document.createElement("span");
            value.className = "ioe-bar-value";
            value.textContent = total.toLocaleString(locale);
            track.append(value);
            const row = document.createElement("li");
            row.title = [tr("ioeBar", label.textContent, value.textContent), ...details].join("\n");
            row.setAttribute("aria-label", row.title);
            row.append(track, label);
            rows.append(row);
        }
        ioeBars.replaceChildren(rows);
    }
    figure.hidden = !buckets.length;
    ioeDetailsButton.hidden = true;
    ioeResultDetails.hidden = false;
    ioeResultPanel.hidden = false;
    renderAnalysisResult();
    if (buckets.length) fitIoeLabels();
}

function clearPressedCells() {
    for (const cell of pressedCells)
        boardElement.children[cell].classList.remove("open");
    pressedCells.clear();
}

function showHighlight(groups = []) {
    for (const button of boardElement.querySelectorAll('[style*="--group-background"]'))
        button.style.removeProperty("--group-background");
    groups.forEach((cells, group) => {
        const background = `hsl(${30 + group * 103.132 % 270} 75% 88%)`;
        for (const cell of cells)
            boardElement.children[cell].style.setProperty("--group-background", background);
    });
}

function updatePressedCells() {
    clearPressedCells();
    if (mode !== "play" || operationBusy || playState.lost || playState.won)
        return;
    for (const { cell } of boardPresses.values()) {
        const value = playState.cells[cell];
        const targets = value >= "1" && value <= "8" ? neighbors(cell, playState) : [cell];
        for (const target of targets) {
            if (playState.cells[target] !== "?" || flags.has(target))
                continue;
            boardElement.children[target].classList.add("open");
            pressedCells.add(target);
        }
    }
}

function updateCell(button, cell, state) {
    const value = state.cells[cell];
    const isHidden = value === "?" || value === "G";
    const searchMove = search.moves.get(cell);
    const showSearch = (isHidden || value === "S") && searchMove;
    const ioeTarget = ioeMove?.cell === cell;
    const playerFlag = mode === "play" ? isHidden && flags.has(cell) : state.playerFlags.has(cell);
    const ioeFlag = !playerFlag && ioeMove?.kind === IoeAction.Chord && ioeMove.addFlags.includes(cell);
    const ioeUnflag = ioeMove?.kind === IoeAction.Chord && ioeMove.removeFlags.includes(cell);
    // 先算出最终样式和文字；概率刷新时只写变化项，不清空再恢复整格。
    let className = "cell";
    let text = "";
    let best = false;
    if (value === "G")
        className += " analysis-goal";
    if (!showSearch) button.removeAttribute("data-analysis-detail");
    button.removeAttribute("title");
    button.setAttribute("aria-label", cellPosition(cell, state));
    if ((value >= "0" && value <= "8") || pressedCells.has(cell))
        className += " open";
    if (value >= "1" && value <= "8")
        className += ` number-${value}`;
    if (value === "S")
        className += " forced-safe";
    else if (value === "M")
        className += " forced-mine";
    if (ioeFlag)
        className += " ioe-flag";
    if (playerFlag) {
        className += " flagged";
        text = "⚑";
    } else if (value === "*") {
        className += " mine";
        if (cell === playState.lostCell)
            className += " hit-mine";
        text = "✹";
    } else if (showSearch) {
        className += " analysis-candidate";
        const score = searchMove.value;
        const detail = search.result.engine === SearchEngine.BruteForce ? tr("exact") : tr("moveDepth", searchMove.depth);
        best = search.result.safe || cell === search.result.recommended;
        button.title = tr("moveScore", (Number(score) * 100).toFixed(4), detail);
        text = score === null ? "—" : `${(Number(score) * 100).toFixed(1)}%`;
        button.dataset.analysisDetail = detail;
        button.setAttribute("aria-label", tr("cellDescription", button.getAttribute("aria-label"), button.title));
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
    if (best || ioeTarget)
        className += " analysis-best";
    if (mode === "play" && state.firstMove && isHidden && state.effOpening?.has(cell)) {
        className += " eff-opening";
        button.setAttribute("aria-label", tr("cellDescription", button.getAttribute("aria-label"), tr("recommendedOpening")));
    }
    if (ioeTarget) {
        const action = { [IoeAction.Open]: "ioeOpen", [IoeAction.Chord]: "ioeChord", [IoeAction.Unflag]: "ioeUnflag" }[ioeMove.kind];
        button.setAttribute("aria-label", tr("cellDescription", button.getAttribute("aria-label"), tr(action)));
    } else if (ioeFlag) {
        button.setAttribute("aria-label", tr("cellDescription", button.getAttribute("aria-label"), tr("ioeFlag")));
    } else if (ioeUnflag) {
        button.setAttribute("aria-label", tr("cellDescription", button.getAttribute("aria-label"), tr("ioeUnflag")));
    }
    if (button.className !== className)
        button.className = className;
    if (button.textContent !== text)
        button.textContent = text;
    button.tabIndex = mode === "play" || cell === Math.max(0, selectedCell) ? 0 : -1;
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
    const playing = mode === "play";
    const finished = playing && (state.lost || state.won);
    if (operationBusy || finished)
        boardPresses.clear();
    updatePressedCells();
    document.getElementById("play-mode").checked = playing;
    document.getElementById("analysis-mode").checked = !playing;
    for (const id of ["play-mode", "analysis-mode", "open-settings", "new-game", "board-data"])
        document.getElementById(id).disabled = operationBusy;
    boardElement.inert = operationBusy;
    document.getElementById("new-game").hidden = !playing;
    const autoPlay = document.getElementById("play-until-no-safe");
    autoPlay.hidden = !playing;
    autoPlay.disabled = operationBusy || finished;
    const ioeAnalysis = document.getElementById("ioe-analysis");
    ioeAnalysis.hidden = !isBeginner(state);
    ioeAnalysis.dataset.i18n = ioeController ? "stopIoe" : "analyzeIoe";
    ioeAnalysis.textContent = tr(ioeAnalysis.dataset.i18n);
    ioeAnalysis.disabled = (operationBusy && !ioeController) || finished;
    ioeDetailsButton.disabled = operationBusy || finished;
    document.getElementById("board-status").textContent = tr(playing ? state.lost ? "gameOver" : state.won ? "victory" : "playBoard" : "analysisDraft");
    const boardMeta = document.getElementById("board-meta");
    boardMeta.hidden = playing;
    if (!playing) boardMeta.textContent = tr("boardDimensions", state.cols, state.rows, state.mines);
    const remaining = state.mines - (playing ? flags : state.playerFlags).size;
    document.getElementById("mine-counter").textContent = playing ? tr("boardDimensions", state.cols, state.rows, remaining) : tr("remainingMines", remaining);
    const editor = document.getElementById("cell-editor");
    editor.hidden = playing;
    if (!playing) {
        editor.disabled = operationBusy || selectedCell < 0;
        const editTarget = document.getElementById("edit-target");
        editTarget.hidden = selectedCell < 0;
        if (!editTarget.hidden) editTarget.textContent = cellPosition(selectedCell, state);
        for (const key of editor.querySelectorAll("[data-value]"))
            key.setAttribute("aria-pressed", String(key.dataset.value === state.cells[selectedCell]));
    }
    search.render();
}
