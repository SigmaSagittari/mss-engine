const search = new AnalysisStream("/api/anytime-search", renderSearchButton);

function renderSearchButton() {
    searchButton.hidden = mode !== "analysis";
    searchButton.disabled = operationBusy;
    const nodes = Number(searchNodeBudget.value);
    searchButton.textContent = search.controller && search.result?.paused !== true ? "结束搜索" : nodes === -1 ? "开始搜索" : `搜索 ${nodes} 节点`;
}

function updateSearch(result) {
    // 暂停期间重复推送无需刷新。
    if (JSON.stringify(result) === JSON.stringify(search.result)) return;
    search.samples.push({ time: performance.now(), count: result.nodes });
    search.show(result);
    if (result.finished) search.stop();
}

async function requestSearch() {
    if (mode !== "analysis" || operationBusy) return;
    try {
        if (search.controller && search.result?.paused !== true) {
            await post("/api/anytime-search/control", { action: "pause" });
            return;
        }
        const nodes = Number(searchNodeBudget.value);
        if (!Number.isInteger(nodes) || (nodes !== -1 && nodes < 1) || nodes > 2147483647) throw new Error("节点数必须是 -1 或正整数。");
        const run = nodes === -1;
        if (search.controller) {
            search.samples = [{ time: performance.now(), count: search.result.nodes }];
            await post("/api/anytime-search/control", { action: run ? "resume" : "step", nodes });
            return;
        }
        const config = analysisConfig("anytime");
        await search.start(mcts, () => ({ ...boardSnapshot(activeState()), ...config, run }), updateSearch, run ? 0 : nodes);
    } catch (error) {
        setMessage(error.message, "error");
    }
}
