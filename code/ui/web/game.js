function parseRngSeed(value) {
    if (!/^[0-9a-f]{32}$/i.test(value))
        return null;
    // 十六进制格式按高 64 位、低 64 位排列；C++ Random 初始化顺序仍为 lo、hi。
    return { hi: BigInt(`0x${value.slice(0, 16)}`), lo: BigInt(`0x${value.slice(16)}`) };
}

function formatRngSeed(seed) {
    return seed.hi.toString(16).padStart(16, "0") + seed.lo.toString(16).padStart(16, "0");
}

// 与 core/utility/rng.h 的 splitmix64 位运算保持一致。
function splitmix64(value) {
    value = (value + 0x9e3779b97f4a7c15n) & rngMask;
    value = ((value ^ (value >> 30n)) * 0xbf58476d1ce4e5b9n) & rngMask;
    value = ((value ^ (value >> 27n)) * 0x94d049bb133111ebn) & rngMask;
    return value ^ (value >> 31n);
}

function nextRandom() {
    rng.lo = splitmix64(rng.lo + 0x9e3779b97f4a7c15n);
    rng.hi = splitmix64(rng.hi + 0xd1b54a32d192ed03n);
    return rng.lo ^ rng.hi;
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

function createPlayState(rows, cols, mines) {
    const seed = formatRngSeed(rng);
    const cells = new Array(rows * cols);
    for (let cell = 0; cell < cells.length; ++cell)
        cells[cell] = cell;
    // 本地使用 Fisher-Yates 洗牌；首开踩雷时由 moveFirstMine 挪雷，后端任务直接接收已生成的雷盘。
    for (let cell = cells.length - 1; cell > 0; --cell) {
        const other = Number(nextRandom() % BigInt(cell + 1));
        const value = cells[cell];
        cells[cell] = cells[other];
        cells[other] = value;
    }
    const minefield = new Array(cells.length).fill("0");
    for (let mine = 0; mine < mines; ++mine)
        minefield[cells[mine]] = "1";
    const state = { rows, cols, mines, seed, minefield: minefield.join(""), cells: "?".repeat(rows * cols), firstMove: true, lost: false, won: false, lostCell: -1 };
    state.numbers = numberCells(state.minefield, state);
    return state;
}

function moveFirstMine(cell) {
    const safeCells = [];
    for (let target = 0; target < playState.minefield.length; ++target)
        if (playState.minefield[target] === "0")
            safeCells.push(target);
    const safeCell = safeCells[Number(nextRandom() % BigInt(safeCells.length))];
    const minefield = playState.minefield.split("");
    minefield[cell] = "0";
    minefield[safeCell] = "1";
    playState.minefield = minefield.join("");
    playState.numbers = numberCells(playState.minefield, playState);
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

function revealCell(cell) {
    clearSuggestion();
    playState = { ...playState };
    if (playState.firstMove && playState.minefield[cell] === "1")
        moveFirstMine(cell);
    flags.delete(cell);
    revealLocally(cell);
    scheduleProbabilityUpdate();
    render();
    if (playState.lost)
        setMessage("踩到地雷，本局结束。", "error");
    else if (playState.won)
        setMessage("恭喜，所有安全格都已揭开。", "success");
    else
        setMessage("");
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
    clearSuggestion();
    playState = { ...playState };
    for (const target of targets) {
        if (playState.cells[target] !== "?" || flags.has(target))
            continue;
        if (playState.firstMove && playState.minefield[target] === "1")
            moveFirstMine(target);
        flags.delete(target);
        revealLocally(target);
        if (playState.lost || playState.won)
            break;
    }
    scheduleProbabilityUpdate();
    render();
    if (playState.lost)
        setMessage("踩到地雷，本局结束。", "error");
    else if (playState.won)
        setMessage("恭喜，所有安全格都已揭开。", "success");
    else
        setMessage("");
}

function activatePlayCell(cell) {
    if (cell < 0 || playState.lost || playState.won)
        return;
    const value = playState.cells[cell];
    if (value === "?") {
        if (!flags.has(cell))
            revealCell(cell);
    } else if (value >= "1" && value <= "8")
        chord(cell);
}

function toggleFlag(cell) {
    if (flags.has(cell))
        flags.delete(cell);
    else
        flags.add(cell);
    render();
}

function startNewGame(seed = null) {
    let rows = Number(document.getElementById("rows").value);
    let cols = Number(document.getElementById("cols").value);
    let mines = Number(document.getElementById("mines").value);
    if (difficulty.value !== "custom") {
        const presets = {
            beginner: [9, 9, 10],
            intermediate: [16, 16, 40],
            advanced: [16, 30, 99]
        };
        [rows, cols, mines] = presets[difficulty.value];
    }
    if (!Number.isInteger(rows) || !Number.isInteger(cols) || !Number.isInteger(mines) || rows < 1 || cols < 1 || rows > 100 || cols > 100 || rows * cols > 10000 || mines < 0 || mines >= rows * cols) {
        setMessage("自定义盘面范围无效。", "error");
        return;
    }
    if (seed)
        rng = { lo: seed.lo, hi: seed.hi };
    if (boardElement.contains(document.activeElement))
        document.activeElement.blur();
    playState = createPlayState(rows, cols, mines);
    rngSeedInput.value = playState.seed;
    mode = "play";
    clearDetail();
    clearSuggestion();
    flags = new Set();
    leftMouseDown = false;
    hoveredCell = -1;
    selectedCell = -1;
    scheduleProbabilityUpdate();
    render();
    setMessage("新局已开始，首开安全。", "success");
}

async function playUntilNoSafe() {
    if (mode !== "play" || operationBusy || playState.lost || playState.won)
        return;
    stopMcts(true);
    stopSearch(true);
    operationBusy = true;
    ++probabilityRevision;
    probabilityPending = false;
    clearSuggestion();
    clearDetail();
    probabilities = null;
    candidateCount = "";
    const state = playState;
    render();
    setMessage("");
    try {
        const result = await post("/api/play-until-no-safe", {
            rows: state.rows, cols: state.cols, mines: state.mines, cells: state.cells,
            minefield: state.minefield, algo: shapeAlgo.value, includeProbabilities: probabilityDisplay.checked
        });
        const opened = [...state.cells].filter((value, cell) => value === "?" && result.cells[cell] !== "?").length;
        playState = { ...state, rows: result.rows, cols: result.cols, mines: result.mines, minefield: result.minefield, cells: result.cells, lost: result.lost, won: result.won, firstMove: state.firstMove && opened === 0 };
        for (const cell of flags)
            if (result.cells[cell] !== "?")
                flags.delete(cell);
        if (result.probabilities) {
            probabilities = result.probabilities;
            candidateCount = result.candidateCount;
        }
        setMessage(result.won ? "恭喜，所有安全格都已揭开。" : `自动揭开 ${opened} 格；没有确定安全格。`, "success");
    } catch (error) {
        setMessage(error.message, "error");
    } finally {
        operationBusy = false;
        render();
    }
}
