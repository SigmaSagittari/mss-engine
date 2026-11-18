const presets = {
    beginner: [9, 9, 10],
    intermediate: [16, 16, 40],
    advanced: [16, 30, 99]
};

function isBeginner({ rows, cols, mines }) {
    const [beginnerRows, beginnerCols, beginnerMines] = presets.beginner;
    return rows === beginnerRows && cols === beginnerCols && mines === beginnerMines;
}

function parseRngSeed(value) {
    if (!/^[0-9a-f]{32}$/i.test(value))
        return null;
    // 十六进制格式按高 64 位、低 64 位排列；C++ Random 初始化顺序仍为 lo、hi。
    return { hi: BigInt(`0x${value.slice(0, 16)}`), lo: BigInt(`0x${value.slice(16)}`) };
}

function formatRngSeed(seed) {
    return seed.hi.toString(16).padStart(16, "0") + seed.lo.toString(16).padStart(16, "0");
}

function neighbors(cell, state) {
    const row = Math.floor(cell / state.cols);
    const col = cell % state.cols;
    const result = [];
    for (let rowOffset = -1; rowOffset <= 1; ++rowOffset)
        for (let colOffset = -1; colOffset <= 1; ++colOffset) {
            if (rowOffset === 0 && colOffset === 0)
                continue;
            const nextRow = row + rowOffset;
            const nextCol = col + colOffset;
            if (nextRow >= 0 && nextRow < state.rows && nextCol >= 0 && nextCol < state.cols)
                result.push(nextRow * state.cols + nextCol);
        }
    return result;
}

function numberCells(minefield, state) {
    let result = "";
    for (let cell = 0; cell < minefield.length; ++cell) {
        if (minefield[cell] === "1") {
            result += "*";
            continue;
        }
        let count = 0;
        for (const neighbor of neighbors(cell, state))
            count += minefield[neighbor] === "1" ? 1 : 0;
        result += count;
    }
    return result;
}

function countThreeBV(state) {
    const { numbers } = state;
    const covered = new Uint8Array(numbers.length);
    const pending = [];
    let count = 0;
    // 每个零区计一次，零区边缘的数字格已随零区揭开。
    for (let cell = 0; cell < numbers.length; ++cell) {
        if (numbers[cell] !== "0" || covered[cell])
            continue;
        ++count;
        covered[cell] = 1;
        pending.push(cell);
        while (pending.length > 0)
            for (const neighbor of neighbors(pending.pop(), state)) {
                if (covered[neighbor])
                    continue;
                covered[neighbor] = 1;
                if (numbers[neighbor] === "0")
                    pending.push(neighbor);
            }
    }
    for (let cell = 0; cell < numbers.length; ++cell)
        if (numbers[cell] !== "*" && !covered[cell])
            ++count;
    return count;
}

function createPlayState(rows, cols, mines, generated) {
    const state = { rows, cols, mines, cells: "?".repeat(rows * cols), clicks: 0, startedAt: null, firstMove: true, lost: false, won: false, lostCell: -1 };
    if (generated) {
        state.seed = generated.seed;
        state.minefield = generated.minefield;
        state.effOpening = new Set(generated.opening);
        state.numbers = numberCells(state.minefield, state);
    }
    return state;
}

async function generate(state, first) {
    const { rows, cols, mines } = state;
    const { seed, nextSeed, minefield } = await post("/api/generate-board", { rows, cols, mines, first, seed: formatRngSeed(rng) });
    return { seed, nextSeed: parseRngSeed(nextSeed), minefield, numbers: numberCells(minefield, state) };
}

function revealLocally(cell) {
    revealLocallyBatch([cell]);
}

function revealLocallyBatch(seedCells) {
    const cells = playState.cells.split("");
    playState.firstMove = false;
    const pending = [...seedCells];
    const visited = new Set();
    while (pending.length > 0) {
        const current = pending.pop();
        if (visited.has(current) || cells[current] !== "?")
            continue;
        visited.add(current);
        if (playState.numbers[current] === "*") {
            playState.lost = true;
            playState.won = false;
            playState.lostCell = current;
            for (let index = 0; index < playState.minefield.length; ++index)
                if (playState.minefield[index] === "1")
                    cells[index] = "*";
            playState.cells = cells.join("");
            return;
        }
        cells[current] = playState.numbers[current];
        if (cells[current] === "0")
            for (const neighbor of neighbors(current, playState))
                if (cells[neighbor] === "?" && !flags.has(neighbor))
                    pending.push(neighbor);
    }
    playState.cells = cells.join("");
    playState.won = true;
    for (let index = 0; index < cells.length; ++index)
        if (playState.minefield[index] === "0" && cells[index] === "?") {
            playState.won = false;
            break;
        }
}

function showVictory(finishedAt) {
    playState.finishedAt = finishedAt;
    const seconds = Math.max(1, finishedAt - playState.startedAt) / 1000;
    const threeBV = countThreeBV(playState);
    const cls = playState.clicks;
    setMessage(boardMessage, "victoryStats", "success", seconds.toFixed(3), threeBV, (threeBV / seconds).toFixed(2), cls, (100 * threeBV / cls).toFixed(2));
}

function finishPlayMove() {
    const finishedAt = performance.now();
    scheduleProbabilityUpdate();
    render();
    if (playState.lost)
        setMessage(boardMessage, "lostMessage", "error");
    else if (playState.won)
        showVictory(finishedAt);
    else
        setMessage(boardMessage, "");
}

async function revealCell(cell) {
    const startedAt = playState.startedAt ?? performance.now();
    if (!playState.minefield) {
        operationBusy = true;
        render();
        try {
            const { nextSeed, ...generated } = await generate(playState, cell);
            Object.assign(playState, generated);
            rng = nextSeed;
        } catch (error) {
            operationBusy = false;
            render();
            setErrorMessage(boardMessage, error);
            return;
        }
        operationBusy = false;
    }
    clearIoeResult();
    playState = { ...playState, startedAt, clicks: playState.clicks + 1 };
    flags.delete(cell);
    revealLocally(cell);
    finishPlayMove();
}

function chord(cell) {
    const number = Number(playState.cells[cell]);
    const around = neighbors(cell, playState);
    const flagCount = around.filter((next) => flags.has(next)).length;
    if (flagCount !== number)
        return;
    const targets = around.filter((next) => playState.cells[next] === "?" && !flags.has(next));
    if (targets.length === 0)
        return;
    clearIoeResult();
    playState = { ...playState, clicks: playState.clicks + 1 };
    for (const target of targets) {
        if (playState.cells[target] !== "?" || flags.has(target))
            continue;
        flags.delete(target);
        revealLocally(target);
        if (playState.lost || playState.won)
            break;
    }
    finishPlayMove();
}

function activatePlayCell(cell) {
    if (operationBusy || cell < 0 || playState.lost || playState.won)
        return;
    const value = playState.cells[cell];
    if (value === "?") {
        if (!flags.has(cell))
            revealCell(cell);
    } else if (value >= "1" && value <= "8")
        chord(cell);
}

function toggleFlag(cell) {
    const state = activeState();
    if (mode === "play")
        state.startedAt ??= performance.now();
    const playerFlags = mode === "play" ? flags : state.playerFlags;
    const wasFlagged = playerFlags.has(cell);
    if (wasFlagged)
        playerFlags.delete(cell);
    else
        playerFlags.add(cell);
    if (state.clicks !== null)
        ++state.clicks;
    const operation = wasFlagged ? "removeFlags" : "addFlags";
    if (ioeMove?.kind === IoeAction.Chord && ioeMove[operation].includes(cell)) {
        ioeMove = { ...ioeMove, [operation]: ioeMove[operation].filter(target => target !== cell) };
    } else if (!ioeResultPanel.hidden) {
        clearIoeResult();
    }
    render();
}

function startNewGame(rows, cols, mines, seed = null, generated = null) {
    if (seed)
        rng = { lo: seed.lo, hi: seed.hi };
    if (boardElement.contains(document.activeElement))
        document.activeElement.blur();
    playState = createPlayState(rows, cols, mines, generated);
    mode = "play";
    clearDetail();
    clearIoeResult();
    flags = new Set();
    leftMouseDown = false;
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate();
    render();
    setMessage(boardMessage, "");
}

async function playUntilNoSafe() {
    if (mode !== "play" || operationBusy || playState.lost || playState.won)
        return;
    const needsBoard = !playState.minefield;
    if (needsBoard && playState.mines > 0) {
        setMessage(boardMessage, "openedSafe", "success", 0);
        return;
    }
    stopAnalysis();
    operationBusy = true;
    ++probabilityRevision;
    clearIoeResult();
    clearDetail();
    const highlight = highlightDisplay.value;
    const startedAt = playState.startedAt ?? performance.now();
    render();
    setMessage(boardMessage, "openingSafe");
    try {
        const { nextSeed, ...generated } = needsBoard ? await generate(playState, 0) : {};
        const state = needsBoard ? { ...playState, ...generated } : playState;
        const result = await post("/api/play-until-no-safe", {
            rows: state.rows, cols: state.cols, mines: state.mines, cells: state.cells,
            minefield: state.minefield, algo: shapeAlgo.value, includeProbabilities: probabilityDisplay.checked, highlight
        });
        const finishedAt = performance.now();
        const opened = [...state.cells].filter((value, cell) => value === "?" && result.cells[cell] !== "?").length;
        if (needsBoard) rng = nextSeed;
        // Game 批量逐格揭开。被自动清除的玩家旗也各计一次拔旗。
        playState = { ...state, rows: result.rows, cols: result.cols, mines: result.mines, minefield: result.minefield, cells: result.cells, clicks: state.clicks + opened, lost: result.lost, won: result.won, firstMove: state.firstMove && opened === 0 };
        if (opened > 0)
            playState.startedAt = startedAt;
        for (const cell of flags)
            if (result.cells[cell] !== "?") {
                flags.delete(cell);
                ++playState.clicks;
            }
        if (highlight) showHighlight(result[highlight]);
        // 接口一次返回盘面和雷率，同一轮渲染写入，等待期间沿用原盘面的显示。
        probabilityPending = false;
        probabilities = result.probabilities ?? null;
        candidateCount = result.candidateCount ?? "";
        if (result.won)
            showVictory(finishedAt);
        else
            setMessage(boardMessage, "openedSafe", "success", opened);
    } catch (error) {
        if (probabilityPending) scheduleProbabilityUpdate();
        setErrorMessage(boardMessage, error);
    } finally {
        operationBusy = false;
        render();
    }
}
