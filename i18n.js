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
    workspace: ["工作区", "Workspace", "Không gian làm việc"],
    play: ["游玩", "Play", "Chơi"],
    analysis: ["分析", "Analysis", "Phân tích"],
    openSafe: ["开到无安全格", "Open all safe cells", "Mở hết ô an toàn"],
    startSearch: ["开始搜索", "Start search", "Bắt đầu tìm kiếm"],
    analyzeIoe: ["分析 IOE", "Analyze IOE", "Phân tích IOE"],
    newGame: ["新游戏", "New game", "Ván mới"],
    boardData: ["导入/导出", "Import / Export", "Nhập / Xuất"],
    settings: ["设置", "Settings", "Cài đặt"],
    playBoard: ["游玩盘面", "Game board", "Bàn chơi"],
    editCell: ["编辑格子", "Edit cell", "Sửa ô"],
    cellContents: ["格子内容", "Cell contents", "Nội dung ô"],
    unknown: ["未知", "Unknown", "Chưa rõ"],
    safe: ["安全", "Safe", "An toàn"],
    mine: ["雷", "Mine", "Mìn"],
    candidate: ["候选", "Candidate", "Ứng viên"],
    boardStatus: ["棋盘状态", "Board status", "Trạng thái bàn chơi"],
    layoutCount: ["可行摆法数", "Possible layouts", "Số cách bố trí mìn hợp lệ"],
    results: ["求解结果", "Solver results", "Kết quả giải"],
    ioeRate: ["IOE ≥ 200%（实际 / 可达）", "IOE ≥ 200% (achieved / achievable)", "IOE ≥ 200% (thực tế / có thể đạt)"],
    weightedScore: ["加权评分", "Weighted score", "Điểm có trọng số"],
    ioeDistribution: ["IOE 分布（≥ 200%）", "IOE distribution (≥ 200%)", "Phân bố IOE (≥ 200%)"],
    ioeReachedTip: ["按此策略实际达到的 IOE", "IOE achieved by this strategy", "IOE thực tế đạt được với chiến lược này"],
    ioeReachableTip: ["按已知雷盘时可达到的最高 IOE", "Highest IOE achievable with a known mine layout", "IOE cao nhất có thể đạt khi biết vị trí mìn"],
    ioeReached: ["实际达标", "Achieved", "Đã đạt mục tiêu"],
    ioeDiscarded: ["能达标但被舍弃", "Achievable but missed", "Có thể đạt nhưng đã bị loại"],
    layoutLog: ["摆法数（对数）", "Layouts (log scale)", "Số cách bố trí (thang log)"],
    cellDetails: ["格子详情", "Cell details", "Chi tiết ô"],
    mineProbability: ["雷率", "Mine probability", "Xác suất có mìn"],
    observe: ["Observe", "Observe", "Quan sát"],
    outcome: ["结局", "Outcome", "Kết quả"],
    probability: ["概率", "Probability", "Xác suất"],
    childScore: ["子评分", "Child score", "Điểm của trạng thái con"],
    scoreHistory: ["各层评分", "Scores by depth", "Điểm theo độ sâu"],
    depth: ["层", "Depth", "Độ sâu"],
    score: ["评分", "Score", "Điểm"],
    groupMines: ["组内雷数", "Mines in group", "Số mìn trong nhóm"],
    groupWays: ["组内摆法数", "Group layouts", "Số cách bố trí trong nhóm"],
    longTermRisk: ["长期风险", "Long-term risk", "Rủi ro dài hạn"],
    riskGivenMine: ["LTR 长期风险（已知此格是雷）", "Long-term risk (given this cell is a mine)", "Rủi ro dài hạn (biết ô này có mìn)"],
    riskBody: ["命中本体死猜", "Forced guess in the core pattern", "Buộc đoán trong mẫu chính"],
    riskRemainder: ["其余情况（未命中本体）：", "Other cases (outside the core pattern):", "Các trường hợp ngoài mẫu chính:"],
    cancel: ["取消", "Cancel", "Hủy"],
    difficulty: ["难度", "Difficulty", "Độ khó"],
    beginner: ["初级 9 × 9（10 雷）", "Beginner 9 × 9 (10 mines)", "Dễ 9 × 9 (10 mìn)"],
    intermediate: ["中级 16 × 16（40 雷）", "Intermediate 16 × 16 (40 mines)", "Trung bình 16 × 16 (40 mìn)"],
    advanced: ["高级 30 × 16（99 雷）", "Expert 30 × 16 (99 mines)", "Khó 30 × 16 (99 mìn)"],
    custom: ["自定义", "Custom", "Tùy chỉnh"],
    minimumZne: ["Eff Board 最低 ZNE (%)", "Eff Board minimum ZNE (%)", "ZNE tối thiểu của Eff Board (%)"],
    effDom: ["精确 DOM", "Exact DOM", "DOM chính xác"],
    effZneWarning: ["过于高的 ZNE 要求可能导致筛选器无法找到盘面，具体概率见", "A high ZNE requirement may prevent the filter from finding a board. See the odds in the ", "ZNE yêu cầu quá cao có thể khiến bộ lọc không tìm được bàn phù hợp. Xem xác suất trong "],
    effZneWarningEnd: ["。", ".", "."],
    customBoard: ["自定义盘面", "Custom board", "Bàn chơi tùy chỉnh"],
    rows: ["行", "Rows", "Số hàng"],
    cols: ["列", "Columns", "Số cột"],
    mines: ["雷数", "Mines", "Số mìn"],
    seedLabel: ["开局种子（选填）", "Seed (optional)", "Seed (không bắt buộc)"],
    seedTip: ["32 位十六进制数", "32 hexadecimal digits", "32 chữ số thập lục phân"],
    seedPlaceholder: ["留空随机开局", "Leave blank for a random seed", "Để trống để dùng seed ngẫu nhiên"],
    reuseSeed: ["填入本局种子", "Use current seed", "Dùng seed của ván này"],
    startGame: ["开始新局", "Start game", "Bắt đầu ván mới"],
    close: ["关闭", "Close", "Đóng"],
    visibleBoard: ["可见盘面", "Visible board", "Bàn chơi đang hiển thị"],
    exportJson: ["导出 JSON", "Export JSON", "Xuất JSON"],
    exportDavid: ["导出 davidNhill", "Export davidNhill", "Xuất davidNhill"],
    importDraft: ["导入到分析草稿", "Import into analysis draft", "Nhập vào bản nháp phân tích"],
    totalClicks: ["累计点击数（含插旗、拔旗）", "Clicks so far (including placing and removing flags)", "Tổng lượt bấm (gồm cả cắm và gỡ cờ)"],
    startAnalysis: ["开始分析", "Start analysis", "Bắt đầu phân tích"],
    settingsOptions: ["设置选项", "Settings options", "Tùy chọn cài đặt"],
    winrateSolver: ["胜率求解","Winrate Solver","Bộ giải tỷ lệ thắng"],
    ioeSolver: ["IOE 求解","IOE Solver","Bộ giải IOE"],
    interaction: ["交互","Interaction","Thao tác"],
    longPressFlag: ["长按标雷","Long-press to flag","Nhấn giữ để cắm cờ"],
    longPressDuration: ["长按时长（毫秒）","Long-press duration in milliseconds","Thời gian nhấn giữ (mili giây)"],
    information: ["信息","Info","Thông tin"],
    buildTime: ["编译时间","Build time","Thời điểm biên dịch"],
    display: ["显示", "Display", "Hiển thị"],
    acknowledgements: ["致谢", "Credits", "Lời cảm ơn"],
    nodeBudget: ["分析到指定节点自动停止", "Stop search at node count", "Dừng tìm kiếm khi đạt số nút"],
    nodeBudgetTip: ["填 -1 表示不限节点数。", "Enter -1 for no node limit.", "Nhập -1 để không giới hạn số nút."],
    bruteForce: ["暴力接管", "Brute-force takeover", "Vét cạn"],
    enable: ["启用", "Enable", "Bật"],
    belowLayouts: ["可行摆法数小于", "Fewer possible layouts than", "Khi số cách bố trí hợp lệ ít hơn"],
    scoringScope: ["评分范围", "Moves to score", "Các nước đi cần chấm điểm"],
    bestOnly: ["仅最佳招", "Best move only", "Chỉ nước đi tốt nhất"],
    allCandidates: ["全部候选", "All candidates", "Tất cả ứng viên"],
    multithreading: ["多线程", "Multithreading", "Đa luồng"],
    threadLimit: ["线程上限", "Thread limit", "Giới hạn số luồng"],
    ioeCandidateLimit: ["候选摆法上限", "Candidate layout limit", "Giới hạn số cách bố trí mìn"],
    ioeDomLimit: ["后续搜索 DOM 调用上限", "Search DOM call limit", "Giới hạn số lần gọi DOM khi tìm kiếm tiếp"],
    showProbability: ["棋盘概率", "Board probabilities", "Xác suất trên bàn chơi"],
    groupHighlight: ["分组高亮", "Highlight groups", "Tô nổi nhóm"],
    noHighlight: ["不高亮", "None", "Không tô nổi"],
    cellSize: ["格子大小", "Cell size", "Kích thước ô"],
    automatic: ["自适应", "Automatic", "Tự động"],
    anytimeWidth: ["Anytime 候选格上限", "Anytime candidate limits", "Giới hạn số ô ứng viên của Anytime"],
    firstMove: ["第一步", "First move", "Nước đầu"],
    secondMove: ["第二步", "Second move", "Nước thứ hai"],
    laterMoves: ["后续每步", "Each later move", "Mỗi nước tiếp theo"],
    fiftyFiftyRisk: ["50-50 与长期风险", "50-50 and long-term risk", "50-50 và rủi ro dài hạn"],
    templateCount: ["模板数量", "Template count", "Số mẫu"],
    templateTip: ["从目录开头取这些模板，用于识别 50-50 和计算长期风险。", "Use this many templates from the start of the catalog to detect 50-50s and calculate long-term risk.", "Dùng số mẫu này từ đầu danh mục để nhận diện tình huống 50-50 và tính rủi ro dài hạn."],
    riskEventLimit: ["长期风险事件数上限", "Long-term risk event limit", "Giới hạn số sự kiện rủi ro dài hạn"],
    riskEventTip: ["计算长期风险时，优先计入最可能发生的情况。", "Start with the most likely events when calculating long-term risk.", "Ưu tiên các sự kiện có khả năng xảy ra cao nhất khi tính rủi ro dài hạn."],
    probabilitySolver: ["概率求解器", "Probability solver", "Bộ giải xác suất"],
    solverOrderTip: ["autoSA 会为复杂局面多试几种计算顺序，算出的雷率相同。", "autoSA tries more evaluation orders for complex boards and returns the same mine probabilities.", "autoSA thử thêm thứ tự tính toán cho bàn phức tạp và cho cùng xác suất có mìn."],
    ackChatgpt: ["chatGPT，帮我实现所有代码，倾听我的所有胡说八道，帮我制作所有的 BUG。", "chatGPT, for implementing all my code, listening to all my nonsense, and helping me create all my bugs.", "chatGPT, vì đã viết toàn bộ mã cho tôi, lắng nghe mọi lời nói linh tinh của tôi và giúp tôi tạo ra mọi lỗi."],
    ackDom: ["DOM，完成了 Zini-IOE Algo 的最后一环，使用了很有启发式的 dp。", "DOM, for completing the final piece of the Zini-IOE algorithm with an insightful dynamic programming approach.", "DOM, vì đã hoàn thiện phần cuối của thuật toán Zini-IOE bằng một cách quy hoạch động rất gợi mở."],
    ackDavid: ["davidhill Solver，我的几乎所有工作都基于此，给予了巨大的启发。", "davidhill Solver, for inspiring and serving as the basis of nearly all my work.", "davidhill Solver, vì đã truyền cảm hứng và là nền tảng cho gần như toàn bộ công việc của tôi."],

    httpFailed: [status => "请求失败（HTTP " + status + "）。", status => "Request failed (HTTP " + status + ").", status => "Yêu cầu thất bại (HTTP " + status + ")."],
    requestFailed: ["请求失败。", "Request failed.", "Yêu cầu thất bại."],
    unknownErrorCode: [code => "请求失败（错误码 " + code + "）。", code => "Request failed (error code " + code + ").", code => "Yêu cầu thất bại (mã lỗi " + code + ")."],
    ioeDisconnected: ["IOE 分析连接已中断。", "The IOE analysis connection was interrupted.", "Kết nối phân tích IOE đã bị gián đoạn."],
    outdatedWasm: ["载入的 Wasm 版本过旧，请刷新页面后重新分析。", "The loaded Wasm version is outdated. Reload and try again.", "Phiên bản Wasm đang dùng đã cũ. Hãy tải lại trang rồi thử lại."],
    exactComplete: ["精确枚举已完成", "Exact enumeration complete", "Đã liệt kê chính xác xong"],
    searchComplete: ["搜索已完成", "Search complete", "Đã tìm kiếm xong"],
    nodeLimitReached: ["已达到节点上限", "Node limit reached", "Đã đạt giới hạn số nút"],
    searchPaused: ["搜索已暂停", "Search paused", "Đã tạm dừng tìm kiếm"],
    searching: ["搜索中", "Searching", "Đang tìm kiếm"],
    startingSearch: ["正在开始搜索", "Starting search", "Đang bắt đầu tìm kiếm"],
    resumeSearch: ["继续搜索", "Resume search", "Tiếp tục tìm kiếm"],
    pauseSearch: ["暂停搜索", "Pause search", "Tạm dừng tìm kiếm"],
    nodeCount: [count => count + " 节点", count => count + " nodes", count => count + " nút"],
    nodeSpeed: [count => count + " 节点/秒", count => count + " nodes/s", count => count + " nút/giây"],
    completedDepth: [depth => "完成第 " + depth + " 层", depth => "Completed depth " + depth, depth => "Đã hoàn thành độ sâu " + depth],
    safeCells: [count => "全部安全格（" + count + " 个）", count => "All safe cells (" + count + ")", count => "Tất cả ô an toàn (" + count + " ô)"],
    recommendedCells: [cells => "推荐：" + cells.join("；"), cells => "Recommended: " + cells.join("; "), cells => "Đề xuất: " + cells.join("; ")],
    cellPosition: [(row, col) => "第 " + row + " 行，第 " + col + " 列", (row, col) => "Row " + row + ", column " + col, (row, col) => "Hàng " + row + ", cột " + col],
    mineHit: ["爆炸", "Mine", "Trúng mìn"],
    numberOutcome: [digit => "数字 " + digit, digit => "Number " + digit, digit => "Số " + digit],
    restrictionsHit: [(count, probability) => "命中 " + count + " 个限制：" + probability, (count, probability) => "Matches " + count + " restrictions: " + probability, (count, probability) => "Khớp " + count + " ràng buộc: " + probability],
    guaranteedSafe: ["确定安全", "Guaranteed safe", "Chắc chắn an toàn"],
    anytime: ["Anytime", "Anytime", "Anytime"],
    exactEnumeration: ["精确枚举", "Exact enumeration", "Liệt kê chính xác"],
    safeChance: ["安全率", "Safety probability", "Xác suất an toàn"],
    currentScore: ["当前评分", "Current score", "Điểm hiện tại"],
    exactWinRate: ["精确胜率", "Exact win probability", "Xác suất thắng chính xác"],
    ioeBar: [(rate, total, won) => rate + "，总共 " + total + " 张，实际达标 " + won + " 张", (rate, total, won) => rate + ", " + total + " total layouts, " + won + " achieved", (rate, total, won) => rate + ", tổng cộng " + total + " cách bố trí, " + won + " đạt mục tiêu"],
    exact: ["精确", "Exact", "Chính xác"],
    moveDepth: [depth => depth + " 层", depth => "Depth " + depth, depth => "Độ sâu " + depth],
    moveScore: [(score, detail) => "评分 " + score + "%，" + detail, (score, detail) => "Score " + score + "%, " + detail, (score, detail) => "Điểm " + score + "%, " + detail],
    cellDescription: [(position, detail) => position + "，" + detail, (position, detail) => position + ", " + detail, (position, detail) => position + ", " + detail],
    recommendedOpening: ["推荐首点", "Recommended first click", "Ô nên mở đầu tiên"],
    ioeOpen: ["IOE 推荐左键开格", "IOE recommends opening this cell", "IOE đề xuất mở ô này"],
    ioeChord: ["IOE 推荐左键点数字联开", "IOE recommends chording this number", "IOE đề xuất mở các ô lân cận bằng ô số này"],
    ioeFlag: ["IOE 推荐插旗", "IOE recommends placing a flag", "IOE đề xuất cắm cờ"],
    ioeUnflag: ["IOE 推荐拔旗", "IOE recommends removing the flag", "IOE đề xuất gỡ cờ"],
    gameOver: ["本局结束", "Game over", "Kết thúc ván"],
    victory: ["已获胜", "Won", "Đã thắng"],
    analysisDraft: ["分析草稿", "Analysis draft", "Bản nháp phân tích"],
    boardDimensions: [(cols, rows, mines) => cols + " 列 × " + rows + " 行，" + mines + " 雷", (cols, rows, mines) => cols + " columns × " + rows + " rows, " + mines + (mines === 1 ? " mine" : " mines"), (cols, rows, mines) => cols + " cột × " + rows + " hàng, " + mines + " mìn"],
    remainingMines: [count => "剩余雷数　" + count, count => "Mines remaining: " + count, count => "Số mìn còn lại: " + count],
    victoryStats: [
        (seconds, threeBV, speed, cls, ioe) => "已获胜\n时间 " + seconds + " s\n3BV " + threeBV + "\n3BV/s " + speed + "\nCLS " + cls + "\nIOE " + ioe + "%",
        (seconds, threeBV, speed, cls, ioe) => "Won\nTime " + seconds + " s\n3BV " + threeBV + "\n3BV/s " + speed + "\nCLS " + cls + "\nIOE " + ioe + "%",
        (seconds, threeBV, speed, cls, ioe) => "Đã thắng\nThời gian " + seconds + " giây\n3BV " + threeBV + "\n3BV/s " + speed + "\nCLS " + cls + "\nIOE " + ioe + "%"
    ],
    lostMessage: ["踩到地雷，本局结束。", "You hit a mine. Game over.", "Bạn đã trúng mìn. Ván chơi kết thúc."],
    openingSafe: ["正在揭开确定安全格…", "Opening confirmed safe cells…", "Đang mở các ô chắc chắn an toàn…"],
    openedSafe: [count => "自动揭开 " + count + " 格；没有确定安全格。", count => "Opened " + count + (count === 1 ? " cell" : " cells") + ". No confirmed safe cells remain.", count => "Đã tự động mở " + count + " ô. Không còn ô nào chắc chắn an toàn."],
    positiveBruteLimit: ["暴力接管阈值必须是正整数。", "The brute-force takeover threshold must be a positive integer.", "Ngưỡng chuyển sang vét cạn phải là số nguyên dương."],
    reading: ["正在读取…", "Loading…", "Đang tải…"],
    editedBoard: ["盘面已修改。", "Board updated.", "Đã cập nhật bàn chơi."],
    ioeEnumerating: [
        p => "正在枚举候选摆法，已完成 " + numberText(p.enumerated) + " / " + numberText(p.candidateCount) + " 张",
        p => "Enumerating layouts: " + numberText(p.enumerated) + " / " + numberText(p.candidateCount),
        p => "Đang liệt kê cách bố trí: " + numberText(p.enumerated) + " / " + numberText(p.candidateCount)
    ],
    ioeRoot: [
        p => "正在筛选根部，已分析 " + numberText(p.rootAnalyzed) + " / " + numberText(p.candidateCount) + " 张。已找到 " + numberText(p.rootEligible) + " 张可达 200% 的摆法，DOM 调用 " + numberText(p.rootDomCalls) + " 次",
        p => "Checking root layouts: " + numberText(p.rootAnalyzed) + " / " + numberText(p.candidateCount) + ". " + numberText(p.rootEligible) + " can reach 200% IOE. DOM calls: " + numberText(p.rootDomCalls) + ".",
        p => "Đang lọc ở gốc: " + numberText(p.rootAnalyzed) + " / " + numberText(p.candidateCount) + ". Có " + numberText(p.rootEligible) + " cách bố trí có thể đạt 200% IOE. Số lần gọi DOM: " + numberText(p.rootDomCalls) + "."
    ],
    ioeSearching: [
        p => "正在后续搜索，已调用 DOM " + numberText(p.searchDomCalls) + " / " + numberText(p.domLimit) + " 次。根部找到 " + numberText(p.rootEligible) + " 张可达 200% 的摆法",
        p => "Searching: " + numberText(p.searchDomCalls) + " / " + numberText(p.domLimit) + " DOM calls. " + numberText(p.rootEligible) + " root layouts can reach 200% IOE.",
        p => "Đang tìm kiếm tiếp: " + numberText(p.searchDomCalls) + " / " + numberText(p.domLimit) + " lần gọi DOM. Có " + numberText(p.rootEligible) + " cách bố trí ở gốc có thể đạt 200% IOE."
    ],
    invalidIoeLimits: ["请在设置中填写有效的 IOE 上限。", "Enter valid IOE limits in Settings.", "Nhập các giới hạn IOE hợp lệ trong Cài đặt."],
    countingLayouts: ["正在计算候选摆法数…", "Counting possible layouts…", "Đang đếm số cách bố trí hợp lệ…"],
    tooManyLayouts: [count => "有 " + numberText(count) + " 张候选摆法，超过设置的上限。", count => numberText(count) + " possible layouts exceed the configured limit.", count => numberText(count) + " cách bố trí hợp lệ vượt giới hạn đã đặt."],
    ioeLimitReached: ["已到搜索上限。", "Search limit reached.", "Đã đạt giới hạn tìm kiếm."],
    ioeLimitNoMove: ["已到搜索上限，尚未完成任何首招。", "Search limit reached before any first move was fully evaluated.", "Đã đạt giới hạn tìm kiếm trước khi đánh giá xong bất kỳ nước đầu nào."],
    ioeBoardComplete: ["当前盘面已完成。", "This board is complete.", "Bàn chơi này đã hoàn thành."],
    ioeNoOpening: ["没有可达到 200% 的首招。", "No first move can reach 200% IOE.", "Không có nước đầu nào đạt 200% IOE."],
    ioeDomCalls: [
        (status, root, search) => (status ? tr(status) + " " : "") + "根部 DOM " + numberText(root) + " 次，搜索 DOM " + numberText(search) + " 次。",
        (status, root, search) => (status ? tr(status) + " " : "") + "DOM calls: " + numberText(root) + " at root, " + numberText(search) + " in search.",
        (status, root, search) => (status ? tr(status) + " " : "") + "Số lần gọi DOM ở gốc: " + numberText(root) + ", khi tìm kiếm: " + numberText(search) + "."
    ],
    noEffLimit: ["留空不限", "Leave blank for no minimum", "Để trống nếu không đặt mức tối thiểu"],
    invalidEffInput: ["请输入正数或留空。", "Enter a positive number or leave blank.", "Nhập số dương hoặc để trống."],
    effProgress: [
        (attempts, dom) => "已尝试 " + numberText(attempts) + " 局 · DOM " + numberText(dom) + " 次",
        (attempts, dom) => numberText(attempts) + " tried · " + numberText(dom) + " DOM calls",
        (attempts, dom) => "Đã thử " + numberText(attempts) + " ván · " + numberText(dom) + " lần gọi DOM"
    ],
    currentVisibleBoard: ["当前可见盘面", "Current visible board", "Bàn chơi đang hiển thị"],
    pasteableDraft: ["分析草稿（可粘贴 JSON 或 davidNhill 盘面）", "Analysis draft (paste JSON or a davidNhill board)", "Bản nháp phân tích (dán bàn chơi dạng JSON hoặc davidNhill)"],
    selectedJson: ["已选中 JSON，可直接复制。", "JSON selected. Ready to copy.", "Đã chọn JSON, sẵn sàng sao chép."],
    selectedDavid: ["已选中 davidNhill 文本，可直接复制。", "davidNhill text selected. Ready to copy.", "Đã chọn văn bản davidNhill, sẵn sàng sao chép."],
    unknownBoardFormat: ["无法识别盘面格式", "Unrecognized board format", "Không nhận dạng được định dạng bàn chơi"],
    invalidDavidSize: ["davidNhill 盘面行数或宽度无效", "Invalid davidNhill board dimensions", "Kích thước bàn chơi davidNhill không hợp lệ"],
    invalidBoardLength: ["盘面数据长度无效", "Invalid board data length", "Độ dài dữ liệu bàn chơi không hợp lệ"],
    invalidCellState: ["盘面状态无效", "Invalid cell state", "Trạng thái ô không hợp lệ"],
    importedBoard: ["盘面已导入到分析草稿。", "Board imported into the analysis draft.", "Đã nhập bàn chơi vào bản nháp phân tích."],
    importFailed: [error => "导入失败：" + tr(error.key, ...error.args), error => "Import failed: " + tr(error.key, ...error.args), error => "Nhập thất bại: " + tr(error.key, ...error.args)],

    // Session::Error
    [ApiError.TemplatePrefix]: ["模板前缀超出目录", "The template prefix is out of range.", "Số mẫu cần dùng vượt phạm vi danh mục."],
    [ApiError.LtrEventLimit]: ["LTR 模板匹配上限必须是正整数", "The LTR template match limit must be a positive integer.", "Giới hạn số lần khớp mẫu LTR phải là số nguyên dương."],
    [ApiError.BruteLimit]: ["暴力接管阈值必须是非负整数", "The brute-force takeover threshold must be a nonnegative integer.", "Ngưỡng chuyển sang vét cạn phải là số nguyên không âm."],
    [ApiError.ThreadLimit]: ["BruteForce 核心数必须是正整数", "The brute-force thread limit must be a positive integer.", "Số luồng vét cạn phải là số nguyên dương."],
    [ApiError.NodeLimit]: ["自动停止节点数请填正整数或 -1", "Enter a positive node limit or -1.", "Nhập giới hạn số nút là số nguyên dương hoặc -1."],
    [ApiError.SearchWidth]: ["搜索候选宽度必须是正整数", "Search widths must be positive integers.", "Giới hạn số ứng viên phải là số nguyên dương."],
    [ApiError.BoardParameters]: ["盘面参数无效", "Invalid board parameters.", "Tham số bàn chơi không hợp lệ."],
    [ApiError.BoardRange]: ["自定义盘面范围无效", "Custom board dimensions or mine count are out of range.", "Kích thước bàn chơi hoặc số mìn vượt phạm vi cho phép."],
    [ApiError.BoardCells]: ["盘面数据无效", "Invalid board data.", "Dữ liệu bàn chơi không hợp lệ."],
    [ApiError.SolverOrder]: ["ShapeSolver 算法无效", "Invalid ShapeSolver algorithm.", "Thuật toán ShapeSolver không hợp lệ."],
    [ApiError.AnalysisExpired]: ["计算上下文已失效", "The analysis context has expired.", "Phiên phân tích đã hết hiệu lực."],
    [ApiError.CellIndex]: ["格子编号无效", "Invalid cell index.", "Chỉ số ô không hợp lệ."],
    [ApiError.IoeBeginner]: ["IOE 分析只支持初级盘面", "IOE analysis is available for beginner boards only.", "Phân tích IOE chỉ hỗ trợ bàn độ khó Dễ."],
    [ApiError.IoeClicks]: ["累计点击数无效", "Invalid total click count.", "Tổng lượt bấm không hợp lệ."],
    [ApiError.IoeMissingClicks]: ["已揭开的盘面至少有一次点击", "A board with open cells needs at least one click.", "Bàn có ô đã mở phải có ít nhất một lượt bấm."],
    [ApiError.PlayerFlags]: ["玩家旗数据无效", "Invalid player flag data.", "Dữ liệu cờ của người chơi không hợp lệ."],
    [ApiError.IoeCandidateLimit]: ["IOE 候选摆法上限无效", "Invalid IOE candidate layout limit.", "Giới hạn số cách bố trí cho IOE không hợp lệ."],
    [ApiError.IoeDomLimit]: ["IOE DOM 调用上限无效", "Invalid IOE DOM call limit.", "Giới hạn số lần gọi DOM cho IOE không hợp lệ."],
    [ApiError.IoeFlagOnOpen]: ["已揭格上不能有玩家旗", "An open cell cannot have a player flag.", "Không thể cắm cờ trên ô đã mở."],
    [ApiError.IoeUnsolvable]: ["IOE 盘面无解", "This board has no solution for IOE analysis.", "Bàn chơi này không có lời giải cho phân tích IOE."],
    [ApiError.EffThreshold]: ["最低 ZNE 请填正数", "Enter a positive minimum ZNE.", "Nhập ZNE tối thiểu là một số dương."],
    [ApiError.Seed]: ["开局种子无效", "Invalid seed.", "Seed khởi tạo không hợp lệ."],
    [ApiError.Minefield]: ["真实雷盘数据无效", "Invalid mine layout data.", "Dữ liệu vị trí mìn không hợp lệ."],
    [ApiError.MineCount]: ["真实雷盘与总雷数不符", "The mine layout does not match the total mine count.", "Dữ liệu vị trí mìn không khớp với tổng số mìn."],
    [ApiError.BoardMismatch]: ["游玩观测与真实雷盘不符", "The visible board does not match the mine layout.", "Bàn chơi đang hiển thị không khớp với vị trí mìn thực tế."],
    [ApiError.SearchTargetMine]: ["Search 目标格已确定为雷", "The search target is a confirmed mine.", "Ô cần tìm kiếm chắc chắn có mìn."],
    [ApiError.SearchAction]: ["搜索操作无效", "Invalid search action.", "Thao tác tìm kiếm không hợp lệ."],
    [ApiError.UnknownEndpoint]: ["未知接口", "Unknown endpoint.", "Không tìm thấy điểm cuối API."],
    [ApiError.BoardLogicConflict]: ["基础逻辑冲突", "The clues contradict one another.", "Các số gợi ý mâu thuẫn nhau."],
    [ApiError.BoardMineCountConflict]: ["连通块都有解，却与雷数冲突", "The components have solutions, but their mine counts conflict with the total.", "Mỗi nhóm liên thông đều có lời giải, nhưng tổng số mìn không khớp."],
    [ApiError.BoardComponentConflict]: ["某连通块无解", "A component has no solution.", "Có một nhóm liên thông không có lời giải."],
    [ApiError.HttpMethod]: ["只接受 POST 请求", "This endpoint accepts POST requests only.", "Điểm cuối này chỉ chấp nhận yêu cầu POST."],
    [ApiError.RequestOrigin]: ["请求来源无效", "Invalid request origin.", "Nguồn yêu cầu không hợp lệ."],
    [ApiError.WorkerFailed]: ["求解器运行失败。", "The solver failed.", "Bộ giải gặp lỗi."],
};

const translatedText = new Map();
const translatedAttributes = ["aria-label", "title", "placeholder"];
const localeIndex = { "zh-CN": 0, en: 1, vi: 2 };
let locale = "zh-CN";

function tr(key, ...args) {
    const entry = messages[key][localeIndex[locale]];
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
