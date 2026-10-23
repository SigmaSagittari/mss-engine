function renderMcts() {
    mctsButton.textContent = mctsController ? "结束模拟" : "蒙特卡洛";
    mctsButton.setAttribute("aria-pressed", String(mctsController !== null));
    mctsButton.hidden = mode !== "analysis";
    mctsButton.disabled = operationBusy || (mode === "play" && (playState.lost || playState.won));
    const now = performance.now();
    while (mctsHistory.length > 1 && mctsHistory[1].time <= now - 1000) mctsHistory.shift();
    renderAnalysisResult();
    renderDetail();
}

function stopMcts(clear = false) {
    clearInterval(mctsTimer);
    mctsTimer = null;
    if (mctsController) {
        mctsController.abort();
        mctsController = null;
        mctsStopQueue = mctsStopQueue.then(() => post("/api/mcts/stop")).catch((error) => setMessage(error.message, "error"));
    }
    if (clear) {
        mctsResult = null;
        mctsMoves.clear();
    }
    renderMcts();
}

function updateMcts(result) {
    const time = performance.now();
    if (mctsHistory.length && mctsHistory[mctsHistory.length - 1].time === time)
        mctsHistory[mctsHistory.length - 1].simulations = result.simulations;
    else
        mctsHistory.push({ time, simulations: result.simulations });
    const changed = new Set(mctsMoves.keys());
    mctsResult = result;
    if (result.candidateCount !== undefined) candidateCount = result.candidateCount;
    mctsMoves = new Map(result.moves.map((move) => [move.cell, move]));
    for (const cell of mctsMoves.keys())
        changed.add(cell);
    const state = activeState();
    for (const cell of changed)
        updateCell(boardElement.children[cell], cell, state);
    renderMcts();
    if (result.exact) {
        stopMcts();
        setMessage("已返回暴力精确结果。", "success");
    } else if (result.complete) {
        stopMcts();
        setMessage("棋盘已完成，没有待研究招法。", "success");
    }
}

async function requestMcts() {
    if (mctsController) {
        stopMcts();
        return;
    }
    if (mode !== "analysis" || operationBusy)
        return;
    stopSearch(true);
    setMessage("");
    clearSuggestion();
    mctsResult = null;
    mctsMoves.clear();
    const controller = new AbortController();
    mctsController = controller;
    mctsHistory = [{ time: performance.now(), simulations: 0 }];
    mctsTimer = setInterval(renderMcts, 100);
    ++probabilityRevision;
    probabilityPending = false;
    render();
    try {
        await Promise.all([mctsStopQueue, searchStopQueue]);
        if (mctsController !== controller)
            return;
        const state = activeState();
        const cells = mode === "play" ? state.cells.replace(/\*/g, "?") : state.cells;
        const response = await fetch("/api/mcts/start", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded", "X-Mss-Token": token },
            body: new URLSearchParams({ mode, rows: state.rows, cols: state.cols, mines: state.mines, cells, ...analysisConfig() }),
            signal: controller.signal
        });
        await readUpdates(response, controller, updateMcts);
    } catch (error) {
        if (!controller.signal.aborted)
            setMessage(error.message, "error");
    } finally {
        if (mctsController === controller) {
            mctsController = null;
            clearInterval(mctsTimer);
            mctsTimer = null;
            renderMcts();
        }
    }
}
