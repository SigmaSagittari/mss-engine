// 普通分析串行引用同一个计算 ID；候选标记 G 不改变观测约束。
function calculate(state, options = {}) {
    const config = options.suggest ? analysisConfig() : { algo: shapeAlgo.value };
    const snapshot = { rows: state.rows, cols: state.cols, mines: state.mines, cells: state.cells.replace(/[G*]/g, "?"), algo: config.algo };
    const key = JSON.stringify(snapshot);
    const includeProbabilities = options.includeProbabilities ?? true;
    const task = async () => {
        const cached = cachedAnalysis && cachedAnalysis.key === key ? cachedAnalysis : null;
        if (cached && !options.suggest && options.cell === undefined && cached.data.probabilities)
            return cached.data;
        const input = cached ? { analysisId: cached.id } : snapshot;
        const result = await post("/api/analyze", { ...input, ...config, ...options, includeProbabilities });
        cachedAnalysis = { key, id: result.analysisId, data: { ...(cached ? cached.data : {}), ...result } };
        return cachedAnalysis.data;
    };
    const result = analysisRequestQueue.then(task, task);
    analysisRequestQueue = result;
    return result;
}

function analysisConfig() {
    const limit = Number(endgameCandidateLimit.value);
    if (!Number.isInteger(limit) || limit < 1 || limit > 2147483647)
        throw new Error("JavaBrute 切换阈值必须是正整数。");
    const maxThreads = Number(endgameMaxThreads.value);
    if (!Number.isInteger(maxThreads) || maxThreads < 1 || maxThreads > 2147483647)
        throw new Error("BruteForce 核心数必须是正整数。");
    return { strategy: suggestStrategy.value, limit, maxThreads, algo: shapeAlgo.value, includeProbabilities: probabilityDisplay.checked, checkAllMoves: endgameCheckAllMoves.checked, multiThread: endgameMultithread.checked };
}

async function requestDetail(cell) {
    if (mode !== "analysis")
        return;
    const request = ++detailRequest;
    const state = analysisState;
    if (state.cells[cell] >= "0" && state.cells[cell] <= "8") {
        showDetail({ probability: "0.000000", candidateCount, observe: null }, cell);
        return;
    }
    detailEmpty.hidden = !analysisElapsedText;
    detailContent.hidden = true;
    try {
        const data = await calculate(state, { cell, includeProbabilities: false });
        if (request === detailRequest && mode === "analysis" && analysisState === state)
            showDetail(data, cell);
    } catch (error) {
        if (request === detailRequest) {
            detailEmpty.hidden = false;
            detailContent.hidden = true;
            detailEmpty.textContent = error.message;
        }
    }
}

function openAnalysis() {
    if (mode === "analysis")
        return;
    const snapshot = playState;
    analysisState = { rows: snapshot.rows, cols: snapshot.cols, mines: snapshot.mines, cells: snapshot.cells.replace(/\*/g, "?") };
    mode = "analysis";
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate();
    render();
    setMessage("已复制可见棋盘。", "success");
}

function scheduleProbabilityUpdate() {
    stopMcts(true);
    const revision = ++probabilityRevision;
    clearDetail();
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
            analysisElapsedText = `概率分析耗时（前端）：${(performance.now() - startedAt).toFixed(1)} ms`;
            probabilityPending = false;
            render();
            setMessage("概率已更新。", "success");
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
    clearEndgameResult();
    const previous = analysisState.cells[cell];
    const cells = analysisState.cells.split("");
    cells[cell] = state;
    analysisState = { ...analysisState, cells: cells.join("") };
    if (selectedCell >= 0)
        cellState.value = analysisState.cells[selectedCell];
    if ((previous === "?" || previous === "G") && (state === "?" || state === "G"))
        stopMcts(true);
    else
        scheduleProbabilityUpdate();
    render();
    setMessage("盘面已修改。", "success");
}

async function requestSuggestion() {
    if (operationBusy || (mode === "play" && (playState.lost || playState.won)))
        return;
    stopMcts(true);
    operationBusy = true;
    renderMcts();
    document.getElementById("play-until-no-safe").disabled = true;
    document.getElementById("endgame-analysis").disabled = true;
    ++probabilityRevision;
    probabilityPending = false;
    clearEndgameResult();
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
