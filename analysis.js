// 普通分析串行引用同一个计算 ID；候选标记 G 不改变观测约束。
function calculate(state, options = {}) {
    const config = { algo: shapeAlgo.value };
    if (options.risk) Object.assign(config, templateRiskSettings());
    const snapshot = { ...boardSnapshot(state, /[G*]/g), algo: config.algo };
    const key = JSON.stringify(snapshot);
    const includeProbabilities = options.includeProbabilities ?? true;
    const highlight = options.highlight;
    const task = async () => {
        const cached = cachedAnalysis && cachedAnalysis.key === key ? cachedAnalysis : null;
        if (cached && options.cell === undefined && (!includeProbabilities || cached.data.probabilities) && (!highlight || cached.data[highlight]))
            return cached.data;
        const input = cached ? { analysisId: cached.id } : snapshot;
        const result = await post("/api/analyze", { ...input, ...config, ...options, includeProbabilities });
        cachedAnalysis = { key, id: result.analysisId, data: { ...cached?.data, ...result } };
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

function searchConfig() {
    const limit = bruteEnabled.checked ? Number(endgameCandidateLimit.value) : 0;
    if (bruteEnabled.checked && (!Number.isInteger(limit) || limit < 1 || limit > 2147483647)) throw new UiError("positiveBruteLimit");
    const maxThreads = limit > 0 && endgameMultithread.checked ? Number(endgameMaxThreads.value) : 1;
    if (!Number.isInteger(maxThreads) || maxThreads < 1 || maxThreads > 2147483647) throw new UiError(ApiError.ThreadLimit);
    const nodes = Number(policyNodeBudget.value);
    const width = searchWidths.map((input) => Number(input.value));
    if (!Number.isInteger(nodes) || (nodes !== -1 && nodes < 1) || nodes > 2147483647) throw new UiError(ApiError.NodeLimit);
    if (width.some((value) => !Number.isInteger(value) || value < 1 || value > 2147483647)) throw new UiError(ApiError.SearchWidth);
    return { ...templateRiskSettings(), limit, maxThreads, nodeBudget: nodes, rootWidth: width[0], middleWidth: width[1], deepWidth: width[2], algo: shapeAlgo.value, multiThread: endgameMultithread.checked, checkAllMoves: endgameCheckAllMoves.value === "all" };
}

function templateRiskSettings() {
    const prefix = Number(templatePrefix.value);
    const eventLimit = Number(ltrEventLimit.value);
    if (!Number.isInteger(prefix) || prefix < 1 || prefix > Number(templatePrefix.max)) throw new UiError(ApiError.TemplatePrefix);
    if (!Number.isInteger(eventLimit) || eventLimit < 1 || eventLimit > 2147483647) throw new UiError(ApiError.LtrEventLimit);
    return { templatePrefix: prefix, ltrEventLimit: eventLimit };
}

async function requestDetail(cell) {
    if (mode !== "analysis") return;
    const request = ++detailRequest;
    const state = analysisState;
    const groupDetail = highlightDisplay.value;
    const revealed = state.cells[cell] >= "0" && state.cells[cell] <= "8";
    detailData = null;
    setLocalizedText(detailStatus, "reading");
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
            const message = localizedError(error);
            setLocalizedText(detailStatus, message.key, ...message.args);
            renderDetail();
        }
    }
}

class AnalysisStream {
    constructor(path, renderButton) {
        this.path = path;
        this.renderButton = renderButton;
        this.controller = null;
        this.startRequest = null;
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
            this.startRequest = null;
            this.stopQueue = this.stopQueue.then(() => post(`${this.path}/stop`)).catch((error) => setErrorMessage(analysisMessage, error));
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

    async start(fields, update) {
        clearIoeResult();
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
        this.startRequest = this.stopQueue.then(() => {
            if (this.controller === controller)
                return startUpdates(this.path, fields(), controller.signal);
        });
        try {
            const response = await this.startRequest;
            if (this.controller !== controller) return;
            await readUpdates(this.path, response, controller, update);
        } catch (error) {
            if (!controller.signal.aborted) setErrorMessage(analysisMessage, error);
        } finally {
            clearInterval(timer);
            if (this.controller === controller) {
                this.controller = null;
                this.startRequest = null;
                this.render();
            }
        }
    }
}

function stopAnalysis() {
    search.stop(true);
}

function openAnalysis() {
    clearIoeResult();
    analysisState = { ...boardSnapshot(playState), clicks: playState.clicks, playerFlags: new Set(flags) };
    mode = "analysis";
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate();
    render();
    setMessage(boardMessage, "");
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
        try {
            const result = await calculate(state, { includeProbabilities, highlight });
            if (revision !== probabilityRevision || mode !== requestedMode)
                return;
            if (includeProbabilities) {
                probabilities = result.probabilities;
                candidateCount = result.candidateCount;
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
            setErrorMessage(boardMessage, error);
        }
    });
}

function editAnalysisCell(cell, state) {
    if (cell < 0 || analysisState.cells[cell] === state)
        return;
    clearIoeResult();
    const previous = analysisState.cells[cell];
    const cells = analysisState.cells.split("");
    cells[cell] = state;
    const changedReveal = /[0-8]/.test(previous + state);
    analysisState = { ...analysisState, cells: cells.join(""), clicks: changedReveal ? null : analysisState.clicks };
    if (/[0-8]/.test(state)) analysisState.playerFlags.delete(cell);
    if ((previous === "?" || previous === "G") && (state === "?" || state === "G")) {
        stopAnalysis();
    } else
        scheduleProbabilityUpdate(true);
    requestDetail(cell);
    render();
    setMessage(boardMessage, "editedBoard", "success");
}

function showIoeProgress(progress) {
    const key = { [IoePhase.Enumeration]: "ioeEnumerating", [IoePhase.Root]: "ioeRoot", [IoePhase.Search]: "ioeSearching" }[progress.phase];
    if (key === undefined) throw new UiError("outdatedWasm");
    setMessage(analysisMessage, key, "", progress);
}

function lockAnalysis(message) {
    operationBusy = true;
    stopAnalysis();
    clearIoeResult();
    ++probabilityRevision;
    probabilityPending = false;
    setMessage(analysisMessage, message);
    render();
}

async function requestIoeAnalysis(clicks) {
    const state = activeState();
    if (operationBusy || !isBeginner(state) || (mode === "play" && (state.lost || state.won)))
        return;
    const playerFlags = mode === "play" ? flags : state.playerFlags;
    const candidateLimitInput = document.getElementById("ioe-candidate-limit");
    const domLimitInput = document.getElementById("ioe-dom-limit");
    const candidateLimit = candidateLimitInput.valueAsNumber;
    const domLimit = domLimitInput.valueAsNumber;
    if (!Number.isInteger(candidateLimit) || !Number.isInteger(domLimit) || !candidateLimitInput.validity.valid || !domLimitInput.validity.valid) {
        setMessage(analysisMessage, "invalidIoeLimits", "error");
        return;
    }
    lockAnalysis("countingLayouts");
    try {
        await search.stopQueue;
        const mask = Array.from(state.cells, (_, cell) => playerFlags.has(cell) ? "1" : "0").join("");
        const result = await analyzeIoeWithProgress({ ...boardSnapshot(state, /[G*]/g), playerFlags: mask, clicks, candidateLimit, domLimit }, showIoeProgress);
        if (result.status === IoeStatus.CandidateLimit) {
            setMessage(analysisMessage, "tooManyLayouts", "", result.candidateCount);
            return;
        }
        if (result.status !== IoeStatus.Exact && result.status !== IoeStatus.DomLimit)
            throw new UiError("outdatedWasm");
        if (result.move && !Object.values(IoeAction).includes(result.move.kind))
            throw new UiError("outdatedWasm");
        ioeMove = result.move;
        let status = "";
        if (result.status === IoeStatus.DomLimit)
            status = ioeMove ? "ioeLimitReached" : "ioeLimitNoMove";
        else if (!ioeMove)
            status = result.wins > 0 ? "ioeBoardComplete" : "ioeNoOpening";
        showIoeResult(result);
        setMessage(analysisMessage, "ioeDomCalls", "", status, result.rootDomCalls, result.searchDomCalls);
    } catch (error) {
        setErrorMessage(analysisMessage, error);
    } finally {
        operationBusy = false;
        render();
    }
}
