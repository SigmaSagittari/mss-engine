const mcts = new AnalysisStream("/api/mcts", renderMctsButton);

function renderMctsButton() {
    mctsButton.textContent = mcts.controller ? "结束模拟" : "蒙特卡洛";
    mctsButton.setAttribute("aria-pressed", String(mcts.controller !== null));
    mctsButton.hidden = mode !== "analysis";
    mctsButton.disabled = operationBusy || (mode === "play" && (playState.lost || playState.won));
}

function updateMcts(result) {
    const time = performance.now();
    const last = mcts.samples[mcts.samples.length - 1];
    if (last?.time === time) last.count = result.simulations;
    else mcts.samples.push({ time, count: result.simulations });
    mcts.show(result);
    if (result.exact || result.complete) {
        mcts.stop();
        setMessage(result.exact ? "已返回暴力精确结果。" : "棋盘已完成，没有待研究招法。", "success");
    }
}

function requestMcts() {
    if (mcts.controller) {
        mcts.stop();
        return;
    }
    if (mode !== "analysis" || operationBusy) return;
    return mcts.start(search, () => ({ mode, ...boardSnapshot(activeState()), ...analysisConfig() }), updateMcts);
}
