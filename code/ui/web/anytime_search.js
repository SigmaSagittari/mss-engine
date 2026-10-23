function renderSearch() {
    searchButton.hidden = mode !== "analysis";
    searchButton.disabled = operationBusy;
    searchButton.textContent = searchController && searchResult?.paused !== true ? "结束搜索" : Number(searchNodeBudget.value) === -1 ? "开始搜索" : `搜索 ${Number(searchNodeBudget.value)} 节点`;
    const now = performance.now();
    while (searchSamples.length > 1 && searchSamples[1].time <= now - 1000) searchSamples.shift();
    renderAnalysisResult();
    renderDetail();
}

function stopSearch(clear = false) {
    if (searchController) {
        searchController.abort();
        searchController = null;
        searchStopQueue = searchStopQueue.then(() => post("/api/anytime-search/stop")).catch((error) => setMessage(error.message, "error"));
    }
    if (clear) { searchResult = null; searchMoves.clear(); }
    renderSearch();
}

function updateSearch(result) {
    // 暂停期间重复推送无需刷新。
    if (JSON.stringify(result) === JSON.stringify(searchResult)) return;
    searchSamples.push({ time: performance.now(), nodes: result.nodes });
    const changed = new Set(searchMoves.keys());
    searchResult = result;
    if (result.candidateCount !== undefined) candidateCount = result.candidateCount;
    searchMoves = new Map(result.moves.map((move) => [move.cell, move]));
    for (const cell of searchMoves.keys()) changed.add(cell);
    const state = activeState();
    for (const cell of changed) updateCell(boardElement.children[cell], cell, state);
    renderSearch();
    if (result.finished) stopSearch();
}

async function requestSearch() {
    if (mode !== "analysis" || operationBusy) return;
    try {
        if (searchController && searchResult?.paused !== true) {
            await post("/api/anytime-search/control", { action: "pause" });
            return;
        }
        const nodes = Number(searchNodeBudget.value);
        if (!Number.isInteger(nodes) || (nodes !== -1 && nodes < 1) || nodes > 2147483647) throw new Error("节点数必须是 -1 或正整数。");
        const run = nodes === -1;
        if (searchController) {
            searchSamples = [{ time: performance.now(), nodes: searchResult.nodes }];
            await post("/api/anytime-search/control", { action: run ? "resume" : "step", nodes });
            return;
        }
        const config = analysisConfig("anytime");
        setMessage("");
        stopMcts(true);
        clearSuggestion();
        searchResult = null;
        searchMoves.clear();
        searchSamples = [{ time: performance.now(), nodes: 0 }];
        const controller = new AbortController();
        searchController = controller;
        ++probabilityRevision;
        probabilityPending = false;
        render();
        const timer = setInterval(renderSearch, 100);
        try {
            await Promise.all([mctsStopQueue, searchStopQueue]);
            if (searchController !== controller) return;
            const state = activeState();
            const response = await fetch("/api/anytime-search/start", {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded", "X-Mss-Token": token },
                body: new URLSearchParams({ rows: state.rows, cols: state.cols, mines: state.mines, cells: state.cells, ...config, run }),
                signal: controller.signal
            });
            if (!run && response.ok && response.headers.get("Content-Type").startsWith("text/event-stream")) await post("/api/anytime-search/control", { action: "step", nodes });
            await readUpdates(response, controller, updateSearch);
        } catch (error) {
            if (!controller.signal.aborted) setMessage(error.message, "error");
        } finally {
            clearInterval(timer);
            if (searchController === controller) { searchController = null; renderSearch(); }
        }
    } catch (error) {
        setMessage(error.message, "error");
    }
}
