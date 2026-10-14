function renderSearch() {
    searchButton.hidden = mode !== "analysis";
    searchButton.disabled = operationBusy;
    searchButton.textContent = searchController && searchResult?.paused !== true ? "结束搜索" : Number(searchNodeBudget.value) === -1 ? "开始搜索" : `搜索 ${Number(searchNodeBudget.value)} 节点`;
    searchSummary.hidden = mode !== "analysis" || (!searchController && !searchResult);
    const now = performance.now();
    while (searchSamples.length > 1 && searchSamples[1].time <= now - 1000) searchSamples.shift();
    const speed = searchController && searchResult && !searchResult.paused && !searchResult.finished ? (searchResult.nodes - searchSamples[0].nodes) * 1000 / Math.max(1, now - searchSamples[0].time) : 0;
    searchSummary.textContent = searchResult?.moves.length ? `逐层搜索 · ${formatCount(searchResult.nodes)} 节点 · ${formatCount(Math.round(speed))} 节点/s · 完成第 ${searchResult.completedDepth} 层 · ${searchResult.safe ? "全部安全格" : searchResult.finished ? "已完成" : searchResult.paused ? "已暂停" : "搜索中"}` : searchResult?.finished ? "逐层搜索 · 棋盘已完成，没有待分析招法" : "正在完成第零层…";
    const move = searchMoves.get(selectedCell);
    searchHistory.hidden = mode !== "analysis" || !move;
    observeResult.classList.toggle("search-observe", !!move);
    for (const item of observeResult.querySelectorAll(".java-score")) item.hidden = !!move;
    for (const item of observeResult.querySelectorAll(".search-branch-heading")) item.hidden = !move;
    for (const item of observeResult.querySelectorAll(".search-branch-score")) {
        item.hidden = !move;
        if (!move) continue;
        const digit = Number(item.dataset.digit);
        const branch = move.branches.find((branch) => branch.digit === digit);
        item.textContent = digit === 9 ? "0%" : branch ? `${(Number(branch.value) * 100).toFixed(4)}%` : "—";
        item.title = digit === 9 ? "爆炸结局评分为 0" : branch ? `第 ${branch.depth} 层子评分` : "该结局尚无搜索评分";
    }
    if (!move) return;
    const body = searchHistory.querySelector("tbody");
    while (body.rows.length > move.value.length) body.deleteRow(-1);
    // 只更新所选格的分数，保留表格节点和页面滚动位置。
    move.value.forEach((value, depth) => {
        let row = body.rows[depth];
        if (!row) {
            row = body.insertRow();
            const label = document.createElement("th");
            label.scope = "row";
            label.textContent = `${depth} 层`;
            row.append(label);
            row.insertCell();
        }
        const score = Number(value).toFixed(6);
        if (row.cells[1].textContent !== score) row.cells[1].textContent = score;
    });
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
        const width = searchWidths.map((input) => Number(input.value));
        if (width.some((value) => !Number.isInteger(value) || value < 1 || value > 2147483647)) throw new Error("候选宽度必须是正整数。");
        stopMcts(true);
        clearEndgameResult();
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
                body: new URLSearchParams({ rows: state.rows, cols: state.cols, mines: state.mines, cells: state.cells, algo: shapeAlgo.value, rootWidth: width[0], middleWidth: width[1], deepWidth: width[2], run }),
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
