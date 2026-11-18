const SearchEngine = { Safe: 1, Anytime: 3, BruteForce: 4 };
const SearchAction = { Pause: 1, Resume: 2 };
const IoePhase = { Enumeration: 0, Root: 1, Search: 2 };
const IoeStatus = { Exact: 0, CandidateLimit: 1, DomLimit: 2 };
const IoeAction = { Open: 1, Unflag: 2, Chord: 3 };

const ApiError = {
    TemplatePrefix: 1,
    LtrEventLimit: 2,
    BruteLimit: 3,
    ThreadLimit: 4,
    NodeLimit: 5,
    SearchWidth: 6,
    BoardParameters: 7,
    BoardRange: 8,
    BoardCells: 9,
    SolverOrder: 10,
    AnalysisExpired: 11,
    CellIndex: 12,
    IoeBeginner: 13,
    IoeClicks: 14,
    IoeMissingClicks: 15,
    PlayerFlags: 16,
    IoeCandidateLimit: 17,
    IoeDomLimit: 18,
    IoeFlagOnOpen: 19,
    IoeUnsolvable: 20,
    EffThreshold: 21,
    Seed: 22,
    Minefield: 23,
    MineCount: 24,
    BoardMismatch: 25,
    SearchTargetMine: 26,
    SearchAction: 27,
    UnknownEndpoint: 28,
    BoardLogicConflict: 29,
    BoardMineCountConflict: 30,
    BoardComponentConflict: 31,
    HttpMethod: 32,
    RequestOrigin: 33,
    WorkerFailed: 34,
};

const messages = {
    workspace: ["工作区", "Workspace"],
    play: ["游玩", "Play"],
    analysis: ["分析", "Analysis"],
    openSafe: ["开到无安全格", "Open all safe cells"],
    startSearch: ["开始搜索", "Start search"],
    analyzeIoe: ["分析 IOE", "Analyze IOE"],
    newGame: ["新游戏", "New game"],
    boardData: ["导入/导出", "Import / Export"],
    settings: ["设置", "Settings"],
    playBoard: ["游玩盘面", "Game board"],
    editCell: ["编辑格子", "Edit cell"],
    cellContents: ["格子内容", "Cell contents"],
    unknown: ["未知", "Unknown"],
    safe: ["安全", "Safe"],
    mine: ["雷", "Mine"],
    candidate: ["候选", "Candidate"],
    boardStatus: ["棋盘状态", "Board status"],
    layoutCount: ["可行摆法数", "Possible layouts"],
    results: ["求解结果", "Solver results"],
    ioeRate: ["IOE ≥ 200%（实际 / 可达）", "IOE ≥ 200% (achieved / achievable)"],
    weightedScore: ["加权评分", "Weighted score"],
    ioeDistribution: ["IOE 分布（≥ 200%）", "IOE distribution (≥ 200%)"],
    ioeReachedTip: ["按此策略实际达到的 IOE", "IOE achieved by this strategy"],
    ioeReachableTip: ["按已知雷盘时可达到的最高 IOE", "Highest IOE achievable with a known mine layout"],
    ioeReached: ["实际达标", "Achieved"],
    ioeDiscarded: ["能达标但被舍弃", "Achievable but missed"],
    layoutLog: ["摆法数（对数）", "Layouts (log scale)"],
    cellDetails: ["格子详情", "Cell details"],
    mineProbability: ["雷率", "Mine probability"],
    observe: ["Observe", "Observe"],
    outcome: ["结局", "Outcome"],
    probability: ["概率", "Probability"],
    childScore: ["子评分", "Child score"],
    scoreHistory: ["各层评分", "Scores by depth"],
    depth: ["层", "Depth"],
    score: ["评分", "Score"],
    groupMines: ["组内雷数", "Mines in group"],
    groupWays: ["组内摆法数", "Group layouts"],
    longTermRisk: ["长期风险", "Long-term risk"],
    riskGivenMine: ["LTR 长期风险（已知此格是雷）", "Long-term risk (given this cell is a mine)"],
    riskBody: ["命中本体死猜", "Forced guess in the core pattern"],
    riskRemainder: ["其余情况（未命中本体）：", "Other cases (outside the core pattern):"],
    cancel: ["取消", "Cancel"],
    difficulty: ["难度", "Difficulty"],
    beginner: ["初级 9 × 9（10 雷）", "Beginner 9 × 9 (10 mines)"],
    intermediate: ["中级 16 × 16（40 雷）", "Intermediate 16 × 16 (40 mines)"],
    advanced: ["高级 30 × 16（99 雷）", "Expert 30 × 16 (99 mines)"],
    custom: ["自定义", "Custom"],
    minimumZne: ["Eff Board 最低 ZNE (%)", "Eff Board minimum ZNE (%)"],
    effDom: ["精确 DOM", "Exact DOM"],
    effZneWarning: ["过于高的 ZNE 要求可能导致筛选器无法找到盘面，具体概率见", "A high ZNE requirement may prevent the filter from finding a board. See the odds in the "],
    effZneWarningEnd: ["。", "."],
    customBoard: ["自定义盘面", "Custom board"],
    rows: ["行", "Rows"],
    cols: ["列", "Columns"],
    mines: ["雷数", "Mines"],
    seedLabel: ["开局种子（选填）", "Seed (optional)"],
    seedTip: ["32 位十六进制数", "32 hexadecimal digits"],
    seedPlaceholder: ["留空随机开局", "Leave blank for a random seed"],
    reuseSeed: ["填入本局种子", "Use current seed"],
    startGame: ["开始新局", "Start game"],
    close: ["关闭", "Close"],
    visibleBoard: ["可见盘面", "Visible board"],
    exportJson: ["导出 JSON", "Export JSON"],
    exportDavid: ["导出 davidNhill", "Export davidNhill"],
    importDraft: ["导入到分析草稿", "Import into analysis draft"],
    totalClicks: ["累计点击数（含插旗、拔旗）", "Clicks so far (including placing and removing flags)"],
    startAnalysis: ["开始分析", "Start analysis"],
    settingsOptions: ["设置选项", "Settings options"],
    solver: ["求解", "Solver"],
    display: ["显示", "Display"],
    advancedSettings: ["高级设置", "Advanced"],
    acknowledgements: ["致谢", "Credits"],
    nodeBudget: ["分析到指定节点自动停止", "Stop search at node count"],
    nodeBudgetTip: ["填 -1 表示不限节点数。", "Enter -1 for no node limit."],
    bruteForce: ["暴力接管", "Brute-force takeover"],
    enable: ["启用", "Enable"],
    belowLayouts: ["可行摆法数小于", "Fewer possible layouts than"],
    scoringScope: ["评分范围", "Moves to score"],
    bestOnly: ["仅最佳招", "Best move only"],
    allCandidates: ["全部候选", "All candidates"],
    multithreading: ["多线程", "Multithreading"],
    threadLimit: ["线程上限", "Thread limit"],
    ioeCandidateLimit: ["候选摆法上限", "Candidate layout limit"],
    ioeDomLimit: ["后续搜索 DOM 调用上限", "Search DOM call limit"],
    showProbability: ["棋盘概率", "Board probabilities"],
    groupHighlight: ["分组高亮", "Highlight groups"],
    noHighlight: ["不高亮", "None"],
    cellSize: ["格子大小", "Cell size"],
    automatic: ["自适应", "Automatic"],
    anytimeWidth: ["Anytime 候选格上限", "Anytime candidate limits"],
    firstMove: ["第一步", "First move"],
    secondMove: ["第二步", "Second move"],
    laterMoves: ["后续每步", "Each later move"],
    fiftyFiftyRisk: ["50-50 与长期风险", "50-50 and long-term risk"],
    templateCount: ["模板数量", "Template count"],
    templateTip: ["从目录开头取这些模板，用于识别 50-50 和计算长期风险。", "Use this many templates from the start of the catalog to detect 50-50s and calculate long-term risk."],
    riskEventLimit: ["长期风险事件数上限", "Long-term risk event limit"],
    riskEventTip: ["计算长期风险时，优先计入最可能发生的情况。", "Start with the most likely events when calculating long-term risk."],
    probabilitySolver: ["概率求解器", "Probability solver"],
    solverOrderTip: ["autoSA 会为复杂局面多试几种计算顺序，算出的雷率相同。", "autoSA tries more evaluation orders for complex boards and returns the same mine probabilities."],
    ackChatgpt: ["chatGPT，帮我实现所有代码，倾听我的所有胡说八道，帮我制作所有的 BUG。", "chatGPT, for implementing all my code, listening to all my nonsense, and helping me create all my bugs."],
    ackDom: ["DOM，完成了 Zini-IOE Algo 的最后一环，使用了很有启发式的 dp。", "DOM, for completing the final piece of the Zini-IOE algorithm with an insightful dynamic programming approach."],
    ackDavid: ["davidhill Solver，我的几乎所有工作都基于此，给予了巨大的启发。", "davidhill Solver, for inspiring and serving as the basis of nearly all my work."],

    httpFailed: [status => "请求失败（HTTP " + status + "）。", status => "Request failed (HTTP " + status + ")."],
    requestFailed: ["请求失败。", "Request failed."],
    unknownErrorCode: [code => "请求失败（错误码 " + code + "）。", code => "Request failed (error code " + code + ")."],
    ioeDisconnected: ["IOE 分析连接已中断。", "The IOE analysis connection was interrupted."],
    outdatedWasm: ["载入的 Wasm 版本过旧，请刷新页面后重新分析。", "The loaded Wasm version is outdated. Reload and try again."],
    exactComplete: ["精确枚举已完成", "Exact enumeration complete"],
    searchComplete: ["搜索已完成", "Search complete"],
    nodeLimitReached: ["已达到节点上限", "Node limit reached"],
    searchPaused: ["搜索已暂停", "Search paused"],
    searching: ["搜索中", "Searching"],
    startingSearch: ["正在开始搜索", "Starting search"],
    resumeSearch: ["继续搜索", "Resume search"],
    pauseSearch: ["暂停搜索", "Pause search"],
    nodeCount: [count => count + " 节点", count => count + " nodes"],
    nodeSpeed: [count => count + " 节点/秒", count => count + " nodes/s"],
    completedDepth: [depth => "完成第 " + depth + " 层", depth => "Completed depth " + depth],
    safeCells: [count => "全部安全格（" + count + " 个）", count => "All safe cells (" + count + ")"],
    recommendedCells: [cells => "推荐：" + cells.join("；"), cells => "Recommended: " + cells.join("; ")],
    cellPosition: [(row, col) => "第 " + row + " 行，第 " + col + " 列", (row, col) => "Row " + row + ", column " + col],
    mineHit: ["爆炸", "Mine"],
    numberOutcome: [digit => "数字 " + digit, digit => "Number " + digit],
    restrictionsHit: [(count, probability) => "命中 " + count + " 个限制：" + probability, (count, probability) => "Matches " + count + " restrictions: " + probability],
    guaranteedSafe: ["确定安全", "Guaranteed safe"],
    anytime: ["Anytime", "Anytime"],
    exactEnumeration: ["精确枚举", "Exact enumeration"],
    safeChance: ["安全率", "Safety probability"],
    currentScore: ["当前评分", "Current score"],
    exactWinRate: ["精确胜率", "Exact win probability"],
    ioeBar: [(rate, total, won) => rate + "，总共 " + total + " 张，实际达标 " + won + " 张", (rate, total, won) => rate + ", " + total + " total layouts, " + won + " achieved"],
    exact: ["精确", "Exact"],
    moveDepth: [depth => depth + " 层", depth => "Depth " + depth],
    moveScore: [(score, detail) => "评分 " + score + "%，" + detail, (score, detail) => "Score " + score + "%, " + detail],
    cellDescription: [(position, detail) => position + "，" + detail, (position, detail) => position + ", " + detail],
    recommendedOpening: ["推荐首点", "Recommended first click"],
    ioeOpen: ["IOE 推荐左键开格", "IOE recommends opening this cell"],
    ioeChord: ["IOE 推荐左键点数字联开", "IOE recommends chording this number"],
    ioeFlag: ["IOE 推荐插旗", "IOE recommends placing a flag"],
    ioeUnflag: ["IOE 推荐拔旗", "IOE recommends removing the flag"],
    gameOver: ["本局结束", "Game over"],
    victory: ["已获胜", "Won"],
    analysisDraft: ["分析草稿", "Analysis draft"],
    boardDimensions: [(cols, rows, mines) => cols + " 列 × " + rows + " 行，" + mines + " 雷", (cols, rows, mines) => cols + " columns × " + rows + " rows, " + mines + (mines === 1 ? " mine" : " mines")],
    remainingMines: [count => "剩余雷数　" + count, count => "Mines remaining: " + count],
    victoryStats: [
        (seconds, threeBV, speed, cls, ioe) => "已获胜\n时间 " + seconds + " s\n3BV " + threeBV + "\n3BV/s " + speed + "\nCLS " + cls + "\nIOE " + ioe + "%",
        (seconds, threeBV, speed, cls, ioe) => "Won\nTime " + seconds + " s\n3BV " + threeBV + "\n3BV/s " + speed + "\nCLS " + cls + "\nIOE " + ioe + "%"
    ],
    lostMessage: ["踩到地雷，本局结束。", "You hit a mine. Game over."],
    openingSafe: ["正在揭开确定安全格…", "Opening confirmed safe cells…"],
    openedSafe: [count => "自动揭开 " + count + " 格；没有确定安全格。", count => "Opened " + count + (count === 1 ? " cell" : " cells") + ". No confirmed safe cells remain."],
    positiveBruteLimit: ["暴力接管阈值必须是正整数。", "The brute-force takeover threshold must be a positive integer."],
    reading: ["正在读取…", "Loading…"],
    editedBoard: ["盘面已修改。", "Board updated."],
    ioeEnumerating: [
        p => "正在枚举候选摆法，已完成 " + numberText(p.enumerated) + " / " + numberText(p.candidateCount) + " 张",
        p => "Enumerating layouts: " + numberText(p.enumerated) + " / " + numberText(p.candidateCount)
    ],
    ioeRoot: [
        p => "正在筛选根部，已分析 " + numberText(p.rootAnalyzed) + " / " + numberText(p.candidateCount) + " 张。已找到 " + numberText(p.rootEligible) + " 张可达 200% 的摆法，DOM 调用 " + numberText(p.rootDomCalls) + " 次",
        p => "Checking root layouts: " + numberText(p.rootAnalyzed) + " / " + numberText(p.candidateCount) + ". " + numberText(p.rootEligible) + " can reach 200% IOE. DOM calls: " + numberText(p.rootDomCalls) + "."
    ],
    ioeSearching: [
        p => "正在后续搜索，已调用 DOM " + numberText(p.searchDomCalls) + " / " + numberText(p.domLimit) + " 次。根部找到 " + numberText(p.rootEligible) + " 张可达 200% 的摆法",
        p => "Searching: " + numberText(p.searchDomCalls) + " / " + numberText(p.domLimit) + " DOM calls. " + numberText(p.rootEligible) + " root layouts can reach 200% IOE."
    ],
    invalidIoeLimits: ["请在设置中填写有效的 IOE 上限。", "Enter valid IOE limits in Settings."],
    countingLayouts: ["正在计算候选摆法数…", "Counting possible layouts…"],
    tooManyLayouts: [count => "有 " + numberText(count) + " 张候选摆法，超过设置的上限。", count => numberText(count) + " possible layouts exceed the configured limit."],
    ioeLimitReached: ["已到搜索上限。", "Search limit reached."],
    ioeLimitNoMove: ["已到搜索上限，尚未完成任何首招。", "Search limit reached before any first move was fully evaluated."],
    ioeBoardComplete: ["当前盘面已完成。", "This board is complete."],
    ioeNoOpening: ["没有可达到 200% 的首招。", "No first move can reach 200% IOE."],
    ioeDomCalls: [
        (status, root, search) => (status ? tr(status) + " " : "") + "根部 DOM " + numberText(root) + " 次，搜索 DOM " + numberText(search) + " 次。",
        (status, root, search) => (status ? tr(status) + " " : "") + "DOM calls: " + numberText(root) + " at root, " + numberText(search) + " in search."
    ],
    noEffLimit: ["留空不限", "Leave blank for no minimum"],
    invalidEffInput: ["请输入正数或留空。", "Enter a positive number or leave blank."],
    effProgress: [
        (attempts, dom) => "已尝试 " + numberText(attempts) + " 局 · DOM " + numberText(dom) + " 次",
        (attempts, dom) => numberText(attempts) + " tried · " + numberText(dom) + " DOM calls"
    ],
    currentVisibleBoard: ["当前可见盘面", "Current visible board"],
    pasteableDraft: ["分析草稿（可粘贴 JSON 或 davidNhill 盘面）", "Analysis draft (paste JSON or a davidNhill board)"],
    selectedJson: ["已选中 JSON，可直接复制。", "JSON selected. Ready to copy."],
    selectedDavid: ["已选中 davidNhill 文本，可直接复制。", "davidNhill text selected. Ready to copy."],
    unknownBoardFormat: ["无法识别盘面格式", "Unrecognized board format"],
    invalidDavidSize: ["davidNhill 盘面行数或宽度无效", "Invalid davidNhill board dimensions"],
    invalidBoardLength: ["盘面数据长度无效", "Invalid board data length"],
    invalidCellState: ["盘面状态无效", "Invalid cell state"],
    importedBoard: ["盘面已导入到分析草稿。", "Board imported into the analysis draft."],
    importFailed: [error => "导入失败：" + tr(error.key, ...error.args), error => "Import failed: " + tr(error.key, ...error.args)],

    // Session::Error
    [ApiError.TemplatePrefix]: ["模板前缀超出目录", "The template prefix is out of range."],
    [ApiError.LtrEventLimit]: ["LTR 模板匹配上限必须是正整数", "The LTR template match limit must be a positive integer."],
    [ApiError.BruteLimit]: ["暴力接管阈值必须是非负整数", "The brute-force takeover threshold must be a nonnegative integer."],
    [ApiError.ThreadLimit]: ["BruteForce 核心数必须是正整数", "The brute-force thread limit must be a positive integer."],
    [ApiError.NodeLimit]: ["自动停止节点数请填正整数或 -1", "Enter a positive node limit or -1."],
    [ApiError.SearchWidth]: ["搜索候选宽度必须是正整数", "Search widths must be positive integers."],
    [ApiError.BoardParameters]: ["盘面参数无效", "Invalid board parameters."],
    [ApiError.BoardRange]: ["自定义盘面范围无效", "Custom board dimensions or mine count are out of range."],
    [ApiError.BoardCells]: ["盘面数据无效", "Invalid board data."],
    [ApiError.SolverOrder]: ["ShapeSolver 算法无效", "Invalid ShapeSolver algorithm."],
    [ApiError.AnalysisExpired]: ["计算上下文已失效", "The analysis context has expired."],
    [ApiError.CellIndex]: ["格子编号无效", "Invalid cell index."],
    [ApiError.IoeBeginner]: ["IOE 分析只支持初级盘面", "IOE analysis is available for beginner boards only."],
    [ApiError.IoeClicks]: ["累计点击数无效", "Invalid total click count."],
    [ApiError.IoeMissingClicks]: ["已揭开的盘面至少有一次点击", "A board with open cells needs at least one click."],
    [ApiError.PlayerFlags]: ["玩家旗数据无效", "Invalid player flag data."],
    [ApiError.IoeCandidateLimit]: ["IOE 候选摆法上限无效", "Invalid IOE candidate layout limit."],
    [ApiError.IoeDomLimit]: ["IOE DOM 调用上限无效", "Invalid IOE DOM call limit."],
    [ApiError.IoeFlagOnOpen]: ["已揭格上不能有玩家旗", "An open cell cannot have a player flag."],
    [ApiError.IoeUnsolvable]: ["IOE 盘面无解", "This board has no solution for IOE analysis."],
    [ApiError.EffThreshold]: ["最低 ZNE 请填正数", "Enter a positive minimum ZNE."],
    [ApiError.Seed]: ["开局种子无效", "Invalid seed."],
    [ApiError.Minefield]: ["真实雷盘数据无效", "Invalid mine layout data."],
    [ApiError.MineCount]: ["真实雷盘与总雷数不符", "The mine layout does not match the total mine count."],
    [ApiError.BoardMismatch]: ["游玩观测与真实雷盘不符", "The visible board does not match the mine layout."],
    [ApiError.SearchTargetMine]: ["Search 目标格已确定为雷", "The search target is a confirmed mine."],
    [ApiError.SearchAction]: ["搜索操作无效", "Invalid search action."],
    [ApiError.UnknownEndpoint]: ["未知接口", "Unknown endpoint."],
    [ApiError.BoardLogicConflict]: ["基础逻辑冲突", "The clues contradict one another."],
    [ApiError.BoardMineCountConflict]: ["连通块都有解，却与雷数冲突", "The components have solutions, but their mine counts conflict with the total."],
    [ApiError.BoardComponentConflict]: ["某连通块无解", "A component has no solution."],
    [ApiError.HttpMethod]: ["只接受 POST 请求", "This endpoint accepts POST requests only."],
    [ApiError.RequestOrigin]: ["请求来源无效", "Invalid request origin."],
    [ApiError.WorkerFailed]: ["求解器运行失败。", "The solver failed."],
};

const translatedText = new Map();
const translatedAttributes = ["aria-label", "title", "placeholder"];
let locale = "zh-CN";

function tr(key, ...args) {
    const entry = messages[key][locale === "en" ? 1 : 0];
    return typeof entry === "function" ? entry(...args) : entry;
}

function numberText(value, options) {
    return value.toLocaleString(locale, options);
}

function setLocalizedText(element, key = "", ...args) {
    if (key) translatedText.set(element, [key, args]);
    else translatedText.delete(element);
    element.textContent = key ? tr(key, ...args) : "";
}

function applyLanguage(language) {
    locale = language;
    document.documentElement.lang = language;
    for (const element of document.querySelectorAll("[data-i18n]"))
        element.textContent = tr(element.dataset.i18n);
    for (const attribute of translatedAttributes)
        for (const element of document.querySelectorAll("[data-i18n-" + attribute + "]"))
            element.setAttribute(attribute, tr(element.getAttribute("data-i18n-" + attribute)));
    for (const [element, [key, args]] of translatedText)
        element.textContent = tr(key, ...args);
}

class UiError extends Error {
    constructor(key, ...args) {
        super(tr(key, ...args));
        this.key = key;
        this.args = args;
    }
}

function localizedError(error) {
    if (error instanceof UiError) return error;
    console.error(error);
    return new UiError("requestFailed");
}
