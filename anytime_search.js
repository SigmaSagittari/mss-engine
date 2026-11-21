const search = new AnalysisStream("/api/anytime-search", renderSearchButton);

function renderSearchButton() {
    searchButton.disabled = operationBusy || (mode === "play" && (playState.lost || playState.won));
    searchButton.textContent = tr(search.controller ? search.result?.paused ? "resumeSearch" : "pauseSearch" : "startSearch");
}

function updateSearch(result) {
    if (result.engine !== undefined && !Object.values(SearchEngine).includes(result.engine))
        throw new UiError("outdatedWasm");
    // 暂停期间重复推送无需刷新。
    if (JSON.stringify(result) === JSON.stringify(search.result)) return;
    search.samples.push({ time: performance.now(), count: result.nodes });
    search.show(result);
    if (result.finished || result.limitReached) search.stop();
}

async function pauseSearch() {
    const { controller, startRequest } = search;
    if (!controller) return;
    try {
        await startRequest;
    } catch {
        return; // 搜索启动失败由 AnalysisStream.start 报告。
    }
    if (search.controller === controller && !search.result?.paused)
        await post("/api/anytime-search/control", { action: SearchAction.Pause });
}

async function requestSearch() {
    if (searchButton.disabled) return;
    try {
        if (search.controller) {
            if (search.result?.paused) {
                search.samples = [{ time: performance.now(), count: search.result.nodes }];
                await post("/api/anytime-search/control", { action: SearchAction.Resume });
            } else {
                await pauseSearch();
            }
            return;
        }
        const config = searchConfig();
        await search.start(() => ({ ...boardSnapshot(activeState()), ...config }), updateSearch);
    } catch (error) {
        setErrorMessage(analysisMessage, error);
    }
}
