function renderMcts() {
    mctsButton.textContent = mctsController ? "停止 MC" : "MC 分析";
    mctsButton.setAttribute("aria-pressed", String(mctsController !== null));
    mctsButton.hidden = mode !== "analysis";
    mctsButton.disabled = operationBusy || (mode === "play" && (playState.lost || playState.won));
    mctsSummary.hidden = mode !== "analysis" || (!mctsController && !mctsResult);
    const now = performance.now();
    while (mctsHistory.length > 1 && mctsHistory[1].time <= now - 1000)
        mctsHistory.shift();
    let speed = 0;
    if (mctsController && mctsHistory.length > 1) {
        const first = mctsHistory[0];
        const next = mctsHistory[1];
        const start = Math.max(first.time, now - 1000);
        const baseline = first.simulations + (next.simulations - first.simulations) * (start - first.time) / (next.time - first.time);
        speed = (mctsResult.simulations - baseline) * 1000 / (now - start);
    }
    mctsSummary.textContent = mctsResult && mctsResult.simulations > 0 ? `MC · ${formatCount(mctsResult.simulations)} 次模拟 · ${mctsCountFormat.format(Math.round(speed))} 局/s · ${mctsResult.winrate ? "整局胜率" : "最佳胜率"} ${(mctsResult.value * 100).toFixed(2)}% · ${mctsController ? "搜索中" : "已停止"}` : "MC 正在开始分析…";
    const move = mode === "analysis" ? mctsMoves.get(selectedCell) : null;
    mctsDetail.hidden = !move;
    if (move) {
        mctsVisits.textContent = move.visits;
        mctsWins.textContent = move.wins;
        mctsWinrate.textContent = Number(move.winrate).toFixed(6);
    }
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
    mctsMoves = new Map(result.moves.filter((move) => BigInt(move.visits) > 0n).map((move) => [move.cell, move]));
    for (const cell of mctsMoves.keys())
        changed.add(cell);
    const state = activeState();
    for (const cell of changed)
        updateCell(boardElement.children[cell], cell, state);
    renderMcts();
    if (result.complete) {
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
    clearEndgameResult();
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
        await mctsStopQueue;
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
        if (!response.ok || !response.headers.get("Content-Type").startsWith("text/event-stream")) {
            const result = await response.json();
            throw new Error(result.error || `请求失败（${response.status}）`);
        }
        const reader = response.body.getReader();
        const decoder = new TextDecoder();
        let pending = "";
        while (mctsController === controller) {
            const { value, done } = await reader.read();
            if (done || mctsController !== controller)
                break;
            pending += decoder.decode(value, { stream: true });
            let end;
            while ((end = pending.indexOf("\n\n")) >= 0) {
                const event = pending.slice(0, end);
                pending = pending.slice(end + 2);
                updateMcts(JSON.parse(event.slice(6)));
            }
        }
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
