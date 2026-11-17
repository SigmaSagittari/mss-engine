// 普通分析串行引用同一个计算 ID；候选标记 G 不改变观测约束。
function calculate(state, options = {}) {
    const config = options.suggest ? analysisConfig() : { algo: shapeAlgo.value };
    if (options.risk) Object.assign(config, templateRiskSettings());
    const snapshot = { ...boardSnapshot(state, /[G*]/g), algo: config.algo };
    const key = JSON.stringify(snapshot);
    const includeProbabilities = options.includeProbabilities ?? true;
    const highlight = options.highlight;
    const task = async () => {
        const cached = cachedAnalysis && cachedAnalysis.key === key ? cachedAnalysis : null;
        if (cached && !options.suggest && options.cell === undefined && (!includeProbabilities || cached.data.probabilities) && (!highlight || cached.data[highlight]))
            return cached.data;
        const input = cached ? { analysisId: cached.id } : snapshot;
        const targets = options.suggest ? Array.from(state.cells.matchAll(/G/g), match => match.index).join(",") : "";
        const result = await post("/api/analyze", { ...input, ...config, ...options, includeProbabilities, targets });
        // Anytime 选招会标记必雷，同一盘面之前的分组可能已经变化。
        cachedAnalysis = { key, id: result.analysisId, data: { ...(cached && !options.suggest ? cached.data : {}), ...result } };
        return options.cell === undefined ? cachedAnalysis.data : result;
    };
    const result = analysisRequestQueue.then(task, task);
    analysisRequestQueue = result;
    if (!highlight) return result;
    return result.then(data => {
        const active = activeState();
        if (highlight === highlightDisplay.value && !(mode === "play" && (active.lost || active.won)) &&
            key === JSON.stringify({ ...boardSnapshot(active, /[G*]/g), algo: shapeAlgo.value }))
            showHighlight(data[highlight]);
        return data;
    });
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
    const templateRisk = strategy === "anytime" ? templateRiskSettings() : {};
    return { strategy, ...templateRisk, limit, maxThreads, nodeBudget: nodes, rootWidth: width[0], middleWidth: width[1], deepWidth: width[2], algo: shapeAlgo.value, includeProbabilities: probabilityDisplay.checked, multiThread: endgameMultithread.checked };
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
    const groupDetail = highlightDisplay.value;
    const revealed = state.cells[cell] >= "0" && state.cells[cell] <= "8";
    detailData = null;
    detailStatus.textContent = "正在读取…";
    renderDetail();
    if (revealed && !groupDetail) {
        showDetail({ probability: 0, observe: null });
        return;
    }
    try {
        const data = await calculate(state, { cell, includeProbabilities: false, risk: !revealed, groupDetail });
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

    async start(fields, update, nodes = 0) {
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
            await this.stopQueue;
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
    search.stop(true);
}

function openAnalysis() {
    if (mode === "analysis")
        return;
    analysisState = { ...boardSnapshot(playState), clicks: playState.clicks, playerFlags: new Set(flags) };
    clearIoeResult();
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
    const includeProbabilities = probabilityDisplay.checked;
    probabilityPending = includeProbabilities;
    if (highlightDisplay.value) showHighlight();
    if ((!includeProbabilities && !highlightDisplay.value) || (mode === "play" && (playState.lost || playState.won))) {
        probabilityPending = false;
        return;
    }
    const requestedMode = mode;
    const state = activeState();
    probabilityUpdateQueue = probabilityUpdateQueue.then(async () => {
        const highlight = highlightDisplay.value;
        if (revision !== probabilityRevision || mode !== requestedMode || (!includeProbabilities && !highlight))
            return;
        const startedAt = performance.now();
        try {
            const result = await calculate(state, { includeProbabilities, highlight });
            if (revision !== probabilityRevision || mode !== requestedMode)
                return;
            if (includeProbabilities) {
                probabilities = result.probabilities;
                candidateCount = result.candidateCount;
                setMessage(`概率已更新 · ${(performance.now() - startedAt).toFixed(1)} ms`, "success");
            }
            probabilityPending = false;
            render();
            if (mode === "analysis" && selectedCell >= 0)
                requestDetail(selectedCell);
        } catch (error) {
            if (revision !== probabilityRevision || mode !== requestedMode || (!includeProbabilities && highlight !== highlightDisplay.value))
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
    const changedReveal = /[0-8]/.test(previous + state);
    analysisState = { ...analysisState, cells: cells.join(""), clicks: changedReveal ? null : analysisState.clicks };
    if (/[0-8]/.test(state)) analysisState.playerFlags.delete(cell);
    if ((previous === "?" || previous === "G") && (state === "?" || state === "G")) {
        stopAnalysis();
    } else
        scheduleProbabilityUpdate();
    render();
    setMessage("盘面已修改。", "success");
}

function showIoeProgress(progress) {
    const count = value => value.toLocaleString();
    if (progress.phase === "enumeration") {
        setMessage(`正在枚举候选摆法 · ${count(progress.enumerated)} / ${count(progress.candidateCount)} 张`);
    } else if (progress.phase === "root") {
        setMessage(`正在筛选根部 · 已分析 ${count(progress.rootAnalyzed)} / ${count(progress.candidateCount)} 张 · 已找到 ${count(progress.rootEligible)} 张可达 200% 的摆法 · DOM ${count(progress.rootDomCalls)} 次`);
    } else {
        setMessage(`正在后续搜索 · DOM ${count(progress.searchDomCalls)} / ${count(progress.domLimit)} 次 · 根部找到 ${count(progress.rootEligible)} 张可达 200% 的摆法`);
    }
}

function lockAnalysis(message) {
    operationBusy = true;
    stopAnalysis();
    clearSuggestion();
    ++probabilityRevision;
    probabilityPending = false;
    setMessage(message);
    render();
}

async function requestIoeAnalysis() {
    const state = activeState();
    if (operationBusy || !isBeginner(state) || (mode === "play" && (state.lost || state.won)))
        return;
    const playerFlags = mode === "play" ? flags : state.playerFlags;
    const candidateLimitInput = document.getElementById("ioe-candidate-limit");
    const domLimitInput = document.getElementById("ioe-dom-limit");
    const candidateLimit = candidateLimitInput.valueAsNumber;
    const domLimit = domLimitInput.valueAsNumber;
    if (!Number.isInteger(candidateLimit) || !Number.isInteger(domLimit) || !candidateLimitInput.validity.valid || !domLimitInput.validity.valid) {
        setMessage("请在设置中填写有效的 IOE 上限。", "error");
        return;
    }
    let clicks = state.clicks;
    if (clicks === null) {
        const input = window.prompt("输入此局面已花的累计点击数（含插旗、拔旗）", "");
        if (input === null) return;
        clicks = Number(input.trim());
        if (!/^\d+$/.test(input.trim()) || !Number.isSafeInteger(clicks) || clicks > 2147483647) {
            setMessage("累计点击数无效。", "error");
            return;
        }
    }
    lockAnalysis("正在计算候选摆法数…");
    try {
        await search.stopQueue;
        const mask = Array.from(state.cells, (_, cell) => playerFlags.has(cell) ? "1" : "0").join("");
        const result = await analyzeIoeWithProgress({ ...boardSnapshot(state, /[G*]/g), playerFlags: mask, clicks, candidateLimit, domLimit }, showIoeProgress);
        if (result.candidateLimitExceeded) {
            setMessage(`有 ${result.candidateCount.toLocaleString()} 张候选摆法，超过设置的上限。`);
            return;
        }
        const coordinate = cell => `${String.fromCharCode(65 + cell % state.cols)}${1 + Math.floor(cell / state.cols)}`;
        const move = result.move;
        ioeMove = move;
        let action = result.wins > 0 ? "当前盘面已完成。" : "没有可达到 200% 的首招。";
        if (move?.kind === "open") action = `左键 ${coordinate(move.cell)}。`;
        else if (move?.kind === "unflag") action = `拔旗 ${coordinate(move.cell)}。`;
        else if (move?.kind === "chord") {
            const steps = [];
            if (move.removeFlags.length) steps.push(`拔旗 ${move.removeFlags.map(coordinate).join("、")}`);
            if (move.addFlags.length) steps.push(`插旗 ${move.addFlags.map(coordinate).join("、")}`);
            steps.push(`双击 ${coordinate(move.cell)}`);
            action = steps.join("，") + "。";
        }
        if (!result.exact)
            action = move ? "已到搜索上限，当前最好招法是" + action : "已到搜索上限，尚未完成任何首招。";
        showIoeResult(result);
        setMessage(`${action} 根部找到 ${result.rootEligible.toLocaleString()} 张可达 200% 的摆法。根部 DOM ${result.rootDomCalls.toLocaleString()} 次，搜索 DOM ${result.searchDomCalls.toLocaleString()} 次。`);
    } catch (error) {
        setMessage(error.message, "error");
    } finally {
        operationBusy = false;
        render();
    }
}

async function requestSuggestion() {
    if (operationBusy || (mode === "play" && (playState.lost || playState.won)))
        return;
    lockAnalysis("正在分析…");
    try {
        await search.stopQueue;
        const state = activeState();
        const highlight = highlightDisplay.value;
        const startedAt = performance.now();
        const result = await calculate(state, { suggest: true, includeProbabilities: probabilityDisplay.checked, highlight });
        if (probabilityDisplay.checked) {
            probabilities = result.probabilities;
            candidateCount = result.candidateCount;
        }
        showSuggestion(result, performance.now() - startedAt);
        if (highlight && mode === "analysis" && selectedCell >= 0) requestDetail(selectedCell);
        setMessage("分析完成。", "success");
    } catch (error) {
        setMessage(error.message, "error");
    } finally {
        probabilityPending = false;
        operationBusy = false;
        render();
    }
}
