// 普通分析串行引用同一个计算 ID；候选标记 G 不改变观测约束。
function calculate(state, options = {}) {
    const config = options.suggest ? analysisConfig() : { algo: shapeAlgo.value };
    if (options.risk) {
        config.fiftyFifty = fiftyFifty.value;
        if (fiftyFifty.value === "templates") Object.assign(config, templateRiskSettings());
    }
    const snapshot = { ...boardSnapshot(state, /[G*]/g), algo: config.algo };
    const key = JSON.stringify(snapshot);
    const includeProbabilities = options.includeProbabilities ?? true;
    const task = async () => {
        const cached = cachedAnalysis && cachedAnalysis.key === key ? cachedAnalysis : null;
        if (cached && !options.suggest && options.cell === undefined && cached.data.probabilities)
            return cached.data;
        const input = cached ? { analysisId: cached.id } : snapshot;
        const targets = options.suggest ? Array.from(state.cells.matchAll(/G/g), match => match.index).join(",") : "";
        const result = await post("/api/analyze", { ...input, ...config, ...options, includeProbabilities, targets });
        cachedAnalysis = { key, id: result.analysisId, data: { ...(cached ? cached.data : {}), ...result } };
        return cachedAnalysis.data;
    };
    const result = analysisRequestQueue.then(task, task);
    analysisRequestQueue = result;
    return result;
}

function analysisConfig(strategy = suggestStrategy.value) {
    const limit = bruteEnabled.checked ? Number(endgameCandidateLimit.value) : 0;
    if (bruteEnabled.checked && (!Number.isInteger(limit) || limit < 1 || limit > 2147483647)) throw new Error("暴力接管阈值必须是正整数。");
    const maxThreads = limit > 0 && endgameMultithread.checked ? Number(endgameMaxThreads.value) : 1;
    if (!Number.isInteger(maxThreads) || maxThreads < 1 || maxThreads > 2147483647) throw new Error("线程上限必须是正整数。");
    const nodes = Number(policyNodeBudget.value);
    const width = searchWidths.map((input) => Number(input.value));
    if (strategy === "anytime") {
        if (!Number.isInteger(nodes) || nodes < 1 || nodes > 2147483647) throw new Error("Anytime 每步节点数必须是正整数。");
        if (width.some((value) => !Number.isInteger(value) || value < 1 || value > 2147483647)) throw new Error("候选宽度必须是正整数。");
    }
    const templateRisk = strategy !== "lowrisk" && fiftyFifty.value === "templates" ? templateRiskSettings() : {};
    return { strategy, fiftyFifty: fiftyFifty.value, ...templateRisk, limit, maxThreads, nodeBudget: nodes, rootWidth: width[0], middleWidth: width[1], deepWidth: width[2], algo: shapeAlgo.value, includeProbabilities: probabilityDisplay.checked, checkAllMoves: endgameCheckAllMoves.value === "all", multiThread: endgameMultithread.checked };
}

function templateRiskSettings() {
    const prefix = Number(templatePrefix.value);
    const eventLimit = Number(ltrEventLimit.value);
    if (!Number.isInteger(prefix) || prefix < 1 || prefix > Number(templatePrefix.max)) throw new Error("模板前缀超出目录。");
    if (!Number.isInteger(eventLimit) || eventLimit < 1 || eventLimit > 2147483647) throw new Error("LTR 模板匹配上限必须是正整数。");
    return { templatePrefix: prefix, ltrEventLimit: eventLimit };
}

async function requestDetail(cell) {
    if (mode !== "analysis") return;
    const request = ++detailRequest;
    const state = analysisState;
    detailData = null;
    detailStatus.textContent = "正在读取…";
    renderDetail();
    if (state.cells[cell] >= "0" && state.cells[cell] <= "8") {
        showDetail({ probability: 0, observe: null });
        return;
    }
    try {
        const data = await calculate(state, { cell, includeProbabilities: false, risk: true });
        if (request === detailRequest && mode === "analysis" && analysisState === state) {
            candidateCount = data.candidateCount;
            showDetail(data);
            renderAnalysisResult();
        }
    } catch (error) {
        if (request === detailRequest) {
            detailStatus.textContent = error.message;
            renderDetail();
        }
    }
}

class AnalysisStream {
    constructor(path, renderButton) {
        this.path = path;
        this.renderButton = renderButton;
        this.controller = null;
        this.stopQueue = Promise.resolve();
        this.result = null;
        this.moves = new Map();
        this.samples = [];
    }

    render() {
        this.renderButton();
        const cutoff = performance.now() - 1000;
        while (this.samples.length > 1 && this.samples[1].time <= cutoff) this.samples.shift();
        renderAnalysisResult();
        renderDetail();
    }

    stop(clear = false) {
        if (this.controller) {
            this.controller.abort();
            this.controller = null;
            this.stopQueue = this.stopQueue.then(() => post(`${this.path}/stop`)).catch((error) => setMessage(error.message, "error"));
        }
        if (clear) {
            this.result = null;
            this.moves.clear();
        }
        this.render();
    }

    show(result) {
        const changed = new Set(this.moves.keys());
        this.result = result;
        if (result.candidateCount !== undefined) candidateCount = result.candidateCount;
        this.moves = new Map(result.moves.map((move) => [move.cell, move]));
        for (const cell of this.moves.keys()) changed.add(cell);
        const state = activeState();
        for (const cell of changed) updateCell(boardElement.children[cell], cell, state);
        this.render();
    }

    async start(other, fields, update, nodes = 0) {
        other.stop(true);
        setMessage("");
        clearSuggestion();
        this.result = null;
        this.moves.clear();
        this.samples = [{ time: performance.now(), count: 0 }];
        const controller = new AbortController();
        this.controller = controller;
        ++probabilityRevision;
        probabilityPending = false;
        render();
        const timer = setInterval(() => this.render(), 100);
        controller.signal.addEventListener("abort", () => clearInterval(timer), { once: true });
        try {
            await Promise.all([this.stopQueue, other.stopQueue]);
            if (this.controller !== controller) return;
            await readUpdates(this.path, fields(), controller, update, nodes);
        } catch (error) {
            if (!controller.signal.aborted) setMessage(error.message, "error");
        } finally {
            clearInterval(timer);
            if (this.controller === controller) {
                this.controller = null;
                this.render();
            }
        }
    }
}

function stopAnalysis() {
    mcts.stop(true);
    search.stop(true);
}

function openAnalysis() {
    if (mode === "analysis")
        return;
    analysisState = boardSnapshot(playState);
    mode = "analysis";
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate();
    render();
    setMessage("已复制可见棋盘。", "success");
}

function scheduleProbabilityUpdate(preserveSelection = false) {
    stopAnalysis();
    const revision = ++probabilityRevision;
    if (!preserveSelection) clearDetail();
    probabilities = null;
    candidateCount = "";
    probabilityPending = probabilityDisplay.checked;
    if (!probabilityDisplay.checked || (mode === "play" && (playState.lost || playState.won))) {
        probabilityPending = false;
        return;
    }
    const requestedMode = mode;
    const state = activeState();
    probabilityUpdateQueue = probabilityUpdateQueue.then(async () => {
        if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
            return;
        const startedAt = performance.now();
        try {
            const result = await calculate(state);
            if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
                return;
            probabilities = result.probabilities;
            candidateCount = result.candidateCount;
            probabilityPending = false;
            render();
            setMessage(`概率已更新 · ${(performance.now() - startedAt).toFixed(1)} ms`, "success");
            if (mode === "analysis" && selectedCell >= 0)
                requestDetail(selectedCell);
        } catch (error) {
            if (revision !== probabilityRevision || !probabilityDisplay.checked || mode !== requestedMode)
                return;
            probabilityPending = false;
            render();
            setMessage(error.message, "error");
        }
    });
}

function editAnalysisCell(cell, state) {
    if (cell < 0 || analysisState.cells[cell] === state)
        return;
    clearSuggestion();
    const previous = analysisState.cells[cell];
    const cells = analysisState.cells.split("");
    cells[cell] = state;
    analysisState = { ...analysisState, cells: cells.join("") };
    if ((previous === "?" || previous === "G") && (state === "?" || state === "G")) {
        stopAnalysis();
    } else
        scheduleProbabilityUpdate();
    render();
    setMessage("盘面已修改。", "success");
}

async function requestSuggestion() {
    if (operationBusy || (mode === "play" && (playState.lost || playState.won)))
        return;
    stopAnalysis();
    operationBusy = true;
    setMessage("正在分析…");
    mcts.render();
    search.render();
    document.getElementById("play-until-no-safe").disabled = true;
    document.getElementById("endgame-analysis").disabled = true;
    ++probabilityRevision;
    probabilityPending = false;
    clearSuggestion();
    const blockerTimer = setTimeout(() => { analysisBlocker.hidden = false; }, 250);
    try {
        const state = activeState();
        const startedAt = performance.now();
        const result = await calculate(state, { suggest: true, includeProbabilities: probabilityDisplay.checked });
        if (probabilityDisplay.checked) {
            probabilities = result.probabilities;
            candidateCount = result.candidateCount;
        }
        showSuggestion(result, performance.now() - startedAt);
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
