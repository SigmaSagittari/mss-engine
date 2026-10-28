# mss1.5 外壳重构架构草案

> 本文其他章节保留旧外壳重构时的源码快照与未实施草图，不能当成当前 API。MC 部分已按后续用户指示修订：现行模块为 `MonteCarlo`，复用 Game 与共享策略枚举，比较首招并按实际终局统计；契约见 `code/algo/winrate_engine/monte_carlo/` 的头文件。旧 MCTS 建树、PUCT 和独立设计文档均已撤销。

这份文档只给出架构方案，不代表实现已经改完。本轮依据当前工作树读取了 main 规范、test/common、UI 的 C++ 与浏览器代码、Config、Game、winrate_engine 当前四个文件、probability_engine 的公开接口与代表性实现，以及 Visual Studio / build.bat 的编译和资源复制配置；没有运行程序、benchmark 或测试。

项目工作树里已有用户修改。本方案按当前文件内容描述；执行时必须保留这些修改。实际执行还需单独按 TODO 顺序审批跨文件实现，不从本文档自动授权实现。

## 1. 先钉住真实目录边界

当前 winrate_engine 目录只有以下四个文件：

- code/src/algo/winrate_engine/monte_carlo/monte_carlo.h
- code/src/algo/winrate_engine/java_transplant/java_evaluate.h
- code/src/algo/winrate_engine/java_transplant/long_term_risk_helper.h
- code/src/algo/winrate_engine/java_transplant/pseudo_helper.h

GameControl::Position::suggest 的定义在 code/src/game/game.cpp。它是游戏层的策略调度入口：先取确定安全格，再按 Config 调用 MCTS、BruteForce、JavaEvaluate 或 LowRisk。它调用 winrate_engine，但它不属于 winrate_engine。UI 的 Session、BruteForce 和 Game 也都不在 winrate_engine 目录。

这个边界在后续拆分中保持不变：游戏层负责“当前局面选哪个策略并调用它”；winrate_engine 负责风险启发、JavaEvaluate 和 MCTS；UI 负责把用户操作变成现有领域 API 调用。

## 2. 当前接口中可以落到代码位置的问题

### UI

- ui/ui.h 的 UI::run 是简短的进程入口，保留即可。
- ui/internal/session.h 的 Session::handle(path, body) -> string 不是领域 API。session.cpp 里它同时拆表单、解析 Config、导入/编辑棋盘、路由 endpoint、调用 Position::suggest、序列化 JSON。
- Session::startMcts(body, response) 再次接收原始表单和输出字符串；Session 保存 mctsUpdate_ JSON 字符串及后台线程。
- http_server.cpp 自己另有 MCTS SSE 线程，同时路由 start/stop，其余 /api 请求又转给 Session::handle。一个 job 的停止和输出因此分散在 Session 与 HttpServer。
- 浏览器 app.js 同时保存页面状态、发 HTTP 请求、管理 MCTS SSE、渲染、生成雷盘、计算数字、移动首步地雷、揭格、零区展开、旗标和 chord。GameControl::MineBoard 也做布雷和数字计算，test/common.h 又单独模拟首步与揭格。这是实际规则重复，不是命名问题。

### test

- test/harness.h 通过注释/取消注释决定本次运行哪个用例；bruteforce.h、graph_solver_frequency.h、performance.h、probability_case.h 都把非模板测试实现写成 inline 函数。
- test/common.h 定义计时、RunSummary、测试自己的 RunConfig，并实现 generateGame/runGames。当前 RunConfig 只选 LowRisk/JavaEval；行、列、雷数和秒数仍作为位置参数传给 runGames，也没有对局数上限。不能因此删除 RunConfig 并用 mss::Config 取代它：两者分别属于测试设置和生产引擎 API。当前 runGames 在内部构造 mss::Config，是调用 Position::suggest 所需的生产 API 参数，不是测试配置的定义。
- graph_solver_frequency.h、performance.h 重复保存行数、列数、雷数和目标秒数，并都使用 runGames；这是把共同随机对局参数收进测试配置的直接依据。probability_case.h 和 bruteforce.h 是固定盘面回归，不应被硬塞进随机对局配置。
- code/main.cpp 生产入口包含 test/harness.h，测试调用靠注释；vs/mss1.5.vcxproj 的 Test 配置定义 GRAPH_SOLVER_FREQUENCY_TEST，但 main.cpp 中没有对应宏分支。测试入口、生产入口和项目配置没有形成一个真实的编译边界。
- 共用的 runGames 是模板，保留在头文件有实际理由；每个普通测试用例不是模板，没有这个理由。

### winrate_engine

- JavaEvaluate、LongTermRiskReference、PseudoReference 的主要非模板实现都放在 .h 中并标 inline；ProbabilityEngine 的同类公开模块则以同名 .h/.cpp 分开。
- JavaEvaluate::Candidate / Result 用 1-based x/y，Evaluation 与其余引擎使用 ObservedBoard::CellId。game.cpp 和 MCTS 因而重复做行列与 CellId 的转换。
- JavaEvaluate::Workspace 只有一个 LongTermRiskReference::Workspace 引用，没有自己的暂存；它是多余包装。
- LongTermRiskReference::countWithForces 声明接收 const board/basic/structure，但实现用 const_cast 原地应用 Delta，再逆序撤回。声明和真实写入行为相反。
- PseudoReference 内部固定定义 kEqEps=1e-9；LongTermRiskReference 的单格 findInfluence 重载也多处直接写 1e-9，而 Config 已有 LongTermRisk::eqEps。JavaEvaluate::eqEps 在当前代码中没有读取点。
- 现行 MonteCarlo 比较初始目标格，后续 rollout 必须复用 Game。目标格由外部传入或使用 Java 自动候选；后续策略复用 LowRisk / JavaEval / JavaBrute 枚举，有必安格时退化为整局胜率采样。
- 现行 MonteCarlo::Result 用 winrate 区分整局与首招模式；只公开实际样本和胜局统计，不保留没有树节点含义的 nodes。UI 预算与取消属于调用方，不用候选列表推断任务是否完成。

### Config

当前 Config 已经是跨层唯一配置类型，含 Position、BruteForce、LongTermRisk、JavaEvaluate、MCTS 五个引擎组和一个 UI 展示组。不要再拆出 UIConfig、TestConfig、MctsOptions 等镜像配置；收敛时移除 UI 展示组。

具体收敛点：

- 保留 position、bruteForce、longTermRisk、javaEvaluate、mcts 五个引擎分组，所有引擎仍接收同一个 const Config&。
- MonteCarlo::Config 保存 temperature 与 priorWeight，分别控制 Java 评分先验分布及伪计数；首招调度沿用归档的竞争机会与预计方差下降，不加入 PUCT exploration。
- 归档的 Java 先验权重在现行接口中命名为 priorWeight，保留原统计含义和默认值，不为接口迁移改动参数。
- JavaEvaluate::eqEps 当前未使用；精度容差归 LongTermRisk::eqEps 所有。PseudoReference 和 LongTermRisk 的单格入口都接收 Config，不再硬编码第二份容差。
- UI.includeProbabilities 是响应是否附带概率的展示选项，不是引擎参数；留在 API 当前请求的局部 bool，不进入 Config。
- mss::version 当前没有读取点，且版本号不是算法配置。移除；有真实消费者时再单独定义应用版本。
- core/config.h 是纯值类型聚合，无函数实现，不需要 config.cpp。

## 3. 项目里实际可复用的接口风格

以下归纳来自 probability_engine 的 Basic、ObservedBoard、Structure、Probability、Observe、ShapeSolver、BruteForce 与 Poly，不套用通用 C++ 风格猜测。

- 普通算法头文件公开该模块的 Result、Delta、Workspace、Pool 和窄 API；同名 .cpp 放非模板算法实现。basic、observed_board、structure、probability、observe、shape_solver 和 bruteforce 都有对应实现文件。
- 头文件在声明附近写可执行的契约：输入来自同一状态、CellId 如何编码、引用/Span/池 ID 的有效期、Delta 的撤回顺序、Workspace 是否可复用或并发。
- 增量状态用模块自己的 Delta，并提供 reverseDelta；例如 Basic 和 Structure 的多次状态写入必须按 LIFO 恢复。不要以副本包装一个本来已经有增量 API 的对象。
- 临时空间集中进 Workspace，调用显式传 Workspace&；不在模块内部偷偷创建第二套全局缓存。引用到上层 Workspace 子对象时，写清楚绑定关系和生命周期。
- Result 拥有跨调用要保留的数据；临时 Span/回调视图则写出覆盖或失效时机。Structure::Pool 与 DistributionPool 的 ID 生命周期在接口处明确。
- 头文件保留模板、确实需要的编译期定义、短值类型和模板回调；DfsSolver 的模板枚举留在 .cpp 实例化，Probability::frontierCells 作为真实模板留在 .h。
- 普通 .cpp 先包含自己的 .h，再列实现依赖；实现放模块命名空间。声明不是路由器，不解析文本协议，也不把 HTTP 字符串传进算法。

编码、测试与引擎接口约定集中在 docs/prompts/ 下，并由 AGENTS.md 按任务引导读取。code/main.cpp 和 code/test/common.h 是实现证据，不是 agent 规范入口；新增测试先完整阅读 CODE_REVIEW_PROMPT.md、implementation.md 和 test.md，再按需要查源码核对真实 API。

## 4. 目标调用图

    浏览器输入
      -> UI::Api（唯一表单/JSON 边界）
      -> Session（会话持有的 Game / analysis Position / MCTS job）
      -> GameControl::Game 或 GameControl::Position
      -> probability_engine / winrate_engine
      -> 原领域 Result
      -> UI::Api 编码响应
      -> 浏览器 render

    test_main -> test::run -> 用例 -> test::runGames -> GameControl::Game
                                                   -> Position::suggest
                                                   -> probability_engine / winrate_engine

GameControl::Position::suggest 仍在 game.cpp，是上述调用图的策略调度者；JavaEvaluate 和 MCTS 是它的被调用模块。浏览器不再有 MineBoard、numberCells、revealLocally、moveFirstMine 等第二份规则实现。

## 5. 目标文件树

    docs/
      README.md
      architecture/REFACTOR_ARCHITECTURE.md
      prompts/CODE_REVIEW_PROMPT.md
    TODO.md
    build.bat
    code/main.cpp
    code/src/core/config.h
    code/src/core/workspace.h
    code/src/game/game.h
    code/src/game/game.cpp
    code/src/ui/ui.h
    code/src/ui/ui.cpp
    code/src/ui/internal/http_server.h
    code/src/ui/internal/http_server.cpp
    code/src/ui/internal/api.h
    code/src/ui/internal/api.cpp
    code/src/ui/internal/session.h
    code/src/ui/session.cpp
    code/src/ui/web/index.html
    code/src/ui/web/app.js
    code/src/ui/web/state.js
    code/src/ui/web/api.js
    code/src/ui/web/game_controller.js
    code/src/ui/web/analysis_controller.js
    code/src/ui/web/render.js
    code/src/ui/web/style.css
    code/src/algo/winrate_engine/monte_carlo/monte_carlo.h
    code/src/algo/winrate_engine/monte_carlo/monte_carlo.cpp
    code/src/algo/winrate_engine/java_transplant/java_evaluate.h
    code/src/algo/winrate_engine/java_transplant/java_evaluate.cpp
    code/src/algo/winrate_engine/java_transplant/long_term_risk_helper.h
    code/src/algo/winrate_engine/java_transplant/long_term_risk_helper.cpp
    code/src/algo/winrate_engine/java_transplant/pseudo_helper.h
    code/src/algo/winrate_engine/java_transplant/pseudo_helper.cpp
    code/test/test_main.cpp
    code/test/harness.h
    code/test/harness.cpp
    code/test/common.h
    code/test/common.cpp
    code/test/bruteforce.h
    code/test/bruteforce.cpp
    code/test/graph_solver_frequency.h
    code/test/graph_solver_frequency.cpp
    code/test/performance.h
    code/test/performance.cpp
    code/test/probability_case.h
    code/test/probability_case.cpp
    vs/mss1.5.vcxproj
    vs/mss1.5.vcxproj.filters

## 6. 文件内容、API 和调用者

下面的声明是目标接口草图；函数实现仍按源文件分工。省略号只代表算法体，不是回退或错误处理代码。

### core 配置与共享工作区

#### code/src/core/config.h

继续作为唯一的默认值聚合，不加 parse/validate/setter 层，也不拆成多个配置 DTO。API 在 HTTP 边界填充这一个 Config，Position 与各引擎只收 const Config&。

    struct Config {
        struct Position { enum class Algo { Java, LowRisk, MCTS }; Algo algo = Algo::Java; int bruteForceThreshold = 1000; } position;
        struct BruteForce { enum class Solver { Common, Bitwise, BitwiseRootParallel }; bool checkAllMoves = false; int minWins = 1; Solver solver = Solver::Bitwise; int maxThreads = 5; } bruteForce;
        struct LongTermRisk { long double influenceThreshold = 0.025L; long double hotspotEnablerSafety = 0.9L; long double eqEps = 1e-9L; } longTermRisk;
        struct JavaEvaluate { bool completeScores = false; long double progressContribution = 0.001L; long double selectionThreshold1 = 0.10L; long double selectionThreshold2 = 0.20L; long double influenceScale = 0.9L; long double equalityThreshold = 0.0001L; int weight1 = 4; int weight2 = 1; int spaceThreshold = 8; } javaEvaluate;
        MonteCarlo::Config monteCarlo;
    };

上述其余字段是旧草图；现行 JavaEval 通过调用参数 prune 控制裁剪，MonteCarlo::Config 与同级 Workspace 在模块内定义，主 Config / Workspace 用同名 monteCarlo 成员聚合。UI 展示选项不属于引擎配置。

#### code/src/core/workspace.h

现行主 Workspace 聚合 Basic、ShapeSolver、BruteForce、Structure、Probability、LongTermRisk、Pseudo、JavaEval 与 MonteCarlo 的模块工作区。JavaEval 拥有分支 Delta 与评分缓冲，MonteCarlo 拥有根抽样及首步缓存，并借用主 Workspace 串行运行 Game；各引用绑定和非并发契约以当前头文件为准。

    LongTermRiskReference::Workspace longTermRisk{basic, structure, probability};

#### code/src/game/game.h

这是公共游戏规则的领域 API；它本来就在 GameControl 外壳，不移入 winrate_engine。Game 保存用户旗标与局终态，Position 保存引擎所需的 ObservedBoard。旗标是玩家输入，不能伪装成 ForcedMine。

    struct Game {
        enum class State { Playing, Won, Lost };
        Game(MineBoard mineBoard, ObservedBoard::Result observedBoard, Workspace &workspace, ShapeSolver::OrderAlgo orderAlgo);
        Game(int rows, int cols, int mines, U128 seed, Workspace &workspace);
        State activate(ObservedBoard::CellId cell, ObservedBoard::Delta &opened);
        State open(ObservedBoard::CellId cell, ObservedBoard::Delta &opened);
        bool toggleFlag(ObservedBoard::CellId cell);
        bool flagged(ObservedBoard::CellId cell) const;
        State state() const;
        int number(int row, int col) const;
        void playUntilNoSafe();
    };

    struct Position { SuggestResult suggest(const Config &config); };

activate 根据当前格状态选择 open 或 chord；对已开数字格，只有相邻旗数等于数字时才 chord 未开且未插旗的邻格。open 负责首步安全、揭格和零区展开；首步点中雷时由 Game 的局内随机流按现有随机选安全格并重算数字，保持固定 seed 可复现，不再让浏览器传入它看不到的真实安全格。toggleFlag 管旗标；playUntilNoSafe 复用 Position 的安全格。每个方法接收有效 CellId，前置条件写在头文件，不再由浏览器或 test 另写一份规则。

API 序列化只发送已揭开的 ObservedBoard 状态；Game::number 只用于本次开格结果和 Lost 后的终局展示，不把未开格数字写入普通响应。

#### code/src/game/game.cpp

存放 MineBoard、Game 动作和 Position::suggest 的非模板实现。Position::suggest 保持为游戏层调用者：它清空安全格、处理概率强制格并按 Config 调度各算法；其中调用 MCTS/JavaEvaluate 的几行仍在这里。

    GameControl::Game::State GameControl::Game::open(ObservedBoard::CellId cell, ObservedBoard::Delta &opened) {
        // 统一首步安全、真实雷盘揭示、零区展开和 Position 增量更新。
    }

### UI：协议边界、会话与服务器各有一个职责

#### code/src/ui/ui.h

仅保留生产启动入口，不暴露 Session 或 HTTP 细节。

    namespace mss::UI { void run(); }

#### code/src/ui/ui.cpp

唯一组合根，创建一个 Session 和一个 HttpServer 并运行。不要在这里写路由、JSON 或引擎选择。

    void UI::run() { Session session; HttpServer server(session); server.run(); }

#### code/src/ui/internal/session.h

Session 不再有 path/body/string JSON API，不持有 token，也不解析表单。它持有 Workspace、当前 Game、分析 Position 和一项 typed MCTS 任务；调用者通过领域对象进行操作。

    struct Session {
        void newGame(int rows, int cols, int mines, U128 seed);
        GameControl::Game &game();
        void setAnalysis(ObservedBoard::Result board, ShapeSolver::OrderAlgo algo);
        GameControl::Position &analysis();
        void startMcts(const Config &config, std::span<const ObservedBoard::CellId> rootCandidates);
        void stopMcts();
        MCTS::Result mctsResult() const;
    };

hasGame/hasAnalysis 只服务于协议层的“尚未建局/导入盘面”请求状态；内部拿到 Game 或 Position 后依照其契约运行，不再在每个引擎调用前堆防御式检查。新建局面、重导分析盘面前，Session 停止并 join 旧 MCTS job。

#### code/src/ui/session.cpp

实现会话生命周期，不包含 FormField、URL 解码、endpoint 字符串、JSON 拼接或 HTTP 状态码。启动 MCTS 时捕获独立 Search；工作线程只推进 Search，并在互斥锁保护下发布 MCTS::Result 副本，不发布 JSON 文本。

    void Session::startMcts(const Config &config, std::span<const CellId> rootCandidates) {
        stopMcts();
        // 从当前 analysis Position 取出引擎结果与 Pool 传给 Search，工作线程只拥有 Search 的副本状态。
    }

#### code/src/ui/internal/api.h

这是唯一的表单/JSON 接缝。它接收 HTTP 层已读出的 path/body，直接解码成现有 Config、ObservedBoard::Result、CellId、CellState，再调用 Session/Position/Game；它不定义平行 Board、Position、Suggestion DTO。

    struct ApiResponse { int status; std::string body; };
    ApiResponse dispatch(Session &session, const std::string &path, const std::string &body);

#### code/src/ui/internal/api.cpp

集中处理当前有限的 API 路由、表单值读取、Config 映射和 JSON 编码。Config 只构造一次；includeProbabilities 单独作为本次响应选项。API 对用户传入的尺寸、枚举和整数做边界解析；完成转换后内部函数遵循领域 API 的前置条件，不再添加回退状态。

保留已有功能路由并增加真实 Game 动作：

| 路由 | 直接调用 | 返回内容 |
|---|---|---|
| /api/game/new | Session::newGame | 实际 seed 与初始棋盘 |
| /api/game/activate | Game::activate | 新揭格、旗标、局状态 |
| /api/game/flag | Game::toggleFlag | 棋盘状态 |
| /api/game/play-safe | Game::playUntilNoSafe | 更新后棋盘状态 |
| /api/copy-analysis、/api/analysis/import | Session::setAnalysis | Analysis Position 当前状态 |
| /api/analysis/edit、/api/analysis/run、/api/suggest、/api/detail | GameControl::Position 与 Observe/Probability API | 建议、概率、结局分布 |
| /api/mcts/start、/api/mcts/poll、/api/mcts/stop | Session 的 typed MCTS job API | MCTS::Result |

HTTP 的 /api/exit 是服务器生命周期命令，留给 HttpServer；不塞进算法 Session。

    ApiResponse dispatch(Session &session, const std::string &path, const std::string &body) {
        const Config config = readConfig(body);
        return writeResult(session.analysis().suggest(config));
    }

上面最后一段只示意路由的调用方向。真实 dispatch 按 path 分支，不让未知路由进入某个默认 handler。

#### code/src/ui/internal/http_server.h

只公开服务器构造和运行入口。token 归服务器，因为它用于 HTTP 来源校验，不是游戏会话的领域状态。

    struct HttpServer { explicit HttpServer(Session &session); void run(); };

#### code/src/ui/internal/http_server.cpp

只做 Winsock 生命周期、HTTP request/response、Host/Origin/token 检查、固定资源 allowlist、启动系统浏览器。认证后的 /api 请求统一交给 Api::dispatch；它不按 MCTS 再开第二个 SSE 线程。Session 继续拥有唯一的 MCTS worker，浏览器通过 poll 取 typed 结果的 JSON 编码。

静态资源仍使用明确 allowlist，不能把 URL 任意拼接到文件系统路径。拆分 JS 后，模块路径必须进入 allowlist；VS PostBuild 与 build.bat 已经用 web/* 复制资源，但只复制不代表 HTTP 路由自动允许。

    const ApiResponse result = dispatch(session_, request.path, request.body);
    sendResponse(client, result.status, "application/json; charset=utf-8", result.body);

### UI 浏览器文件

#### code/src/ui/web/index.html

仅保留可访问的静态 DOM、id 契约、token meta 和 Config 默认值 marker；脚本改为 ES module 入口。每个模块仍由服务器固定 allowlist 提供。

    <meta name="mss-token" content="__MSS_TOKEN__">
    <script type="module" src="/app.js"></script>

#### code/src/ui/web/app.js

只负责初始化模块、绑定共享 state 与启动游戏/分析视图，不保留引擎、规则或 HTTP 序列化实现。

    import { state } from "./state.js";
    import { render } from "./render.js";
    import { bindGame } from "./game_controller.js";
    import { bindAnalysis } from "./analysis_controller.js";

#### code/src/ui/web/state.js

持有一份 UI 快照：当前 mode、服务器返回的 cells/flags/status、分析结果、选中格和页面消息。这里不存 minefield，不实现邻接、number、first move 或 reveal。

    export const state = { mode: "play", game: null, analysis: null, selectedCell: -1 };

#### code/src/ui/web/api.js

唯一浏览器网络接口，设置 X-Mss-Token 与 urlencoded body；MCTS 以短轮询替代 SSE，不再维护第二个连接线程/取消协议。

    export async function post(path, fields = {}) {
        return await fetch(path, { method: "POST", headers: headers(), body: new URLSearchParams(fields) }).then(readResponse);
    }

#### code/src/ui/web/game_controller.js

只把用户输入映射到服务器动作，再用响应替换 UI 快照。鼠标反馈用 CSS :active；不再在 JS 计算邻格、高亮 chord 目标或改棋盘状态。

    export async function activateCell(cell) {
        state.game = await post("/api/game/activate", { cell });
        render(state);
    }

#### code/src/ui/web/analysis_controller.js

负责分析盘导入/编辑、设置字段组装、suggest/detail 和 MCTS 开停/轮询。G 只在 API 编码边界转换成 rootCandidates CellId，不作为 ObservedBoard 的新 CellState。

    export async function requestSuggestion() {
        const result = await post("/api/suggest", { ...boardFields(state), ...engineFields() });
        state.analysisResult = result;
        render(state);
    }

#### code/src/ui/web/render.js

从服务器快照更新棋盘、概率和侧栏；不推导任何状态。用共享 rows/cols/cells 编码计算 DOM 位置，不做游戏动作。

    export function render(state) { renderBoard(state.game || state.analysis); renderDetails(state); }

#### code/src/ui/web/style.css

纯视觉样式，不承担 game state 或接口行为；保留现有 class/id 的样式并按拆分后的 renderer 输出调整。

    .cell:active { filter: brightness(0.9); }

### winrate_engine：策略 API 留在领域层，定义移入 .cpp

#### code/src/algo/winrate_engine/java_transplant/java_evaluate.h

保留 JavaEvaluate 的公开评分和候选结果类型；所有坐标统一为 ObservedBoard::CellId。删除仅转发 LongTermRisk Workspace 的 JavaEvaluate::Workspace。solve 和 evaluate 接收同一个 const Config& 及真实 LongTermRisk Workspace&，并在声明处说明会临时修改哪些 Result、怎样逆序恢复，以及完整评分与剪枝评分的含义。

    struct JavaEvaluate {
        struct Candidate { ObservedBoard::CellId cell; long double safety, influence, expectedClears, maxValueProgress, weight; bool deferGuessing; };
        struct Evaluation { ObservedBoard::CellId cell, dominatingLocation; long double safety, influence, weight, expectedClears, maxValueProgress; bool deferGuessing, pruned; std::vector<ObservedBoard::CellId> commonClears; };
        struct Result { ObservedBoard::CellId bestCell; long double weight; bool pseudo5050; std::vector<Candidate> candidates; std::vector<ObservedBoard::CellId> deadCells; };
        static Evaluation evaluate(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure, const Probability::Result &probability, Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, const LongTermRiskReference::Influence &risk, ObservedBoard::CellId cell, const Config &config, LongTermRiskReference::Workspace &workspace, const Evaluation *best = nullptr);
        static Result solve(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure, const Probability::Result &probability, Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, const LongTermRiskReference::Influence &risk, std::span<const ObservedBoard::CellId> dead, const Config &config, LongTermRiskReference::Workspace &workspace);
    };

MCTS 需要完整评分时传 config.javaEvaluate.completeScores；公开 Result 不再返回 1-based x/y，也不将 UI 展示结构带进引擎。

#### code/src/algo/winrate_engine/java_transplant/java_evaluate.cpp

放 evaluate、solve、候选选择、死格观察、SpaceCounter、dominance 和结局临时揭示等非模板实现。JavaEvaluateInternal 作为具名实现命名空间留在此文件；每个更新产生的 Delta 在当前调用内逆序撤回。这里调用 PseudoReference 和 LongTermRiskReference 的真实算法 API，不调用 Position::suggest，因此策略实现不会递归回到游戏调度层。

    JavaEvaluate::Result JavaEvaluate::solve(..., const Config &config, LongTermRiskReference::Workspace &workspace) {
        // 生成完整候选评分并选择 Java 策略的推荐格。
    }

#### code/src/algo/winrate_engine/java_transplant/long_term_risk_helper.h

只声明 LongTermRiskReference::Influence、RiskHotspot、Possible5050、Workspace 和 findInfluence 两个领域入口。计数缓存 Force/TallyCache 是实现工作区细节，移入 .cpp，不暴露指针身份缓存键。移除未被其他模块调用的 countWithForces 公共 API；若后续测试确实要锁定该原语，再由有对应断言的测试需求决定是否公开。

    struct LongTermRiskReference {
        struct RiskHotspot { ObservedBoard::CellId cell; long double safety; std::vector<ObservedBoard::CellId> exemptions; };
        struct Possible5050 { long double probability; std::vector<ObservedBoard::CellId> exemptions; };
        struct Influence {
            std::vector<long double> tiles, enablers;
            std::vector<ObservedBoard::CellId> pseudos;
            std::vector<RiskHotspot> hotspots;
            std::vector<Possible5050> possible;
            std::vector<ObservedBoard::CellId> influencedTiles(long double threshold, const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability, std::span<const ObservedBoard::CellId> dead) const;
        };
        struct Workspace { Workspace(Basic::Workspace&, Structure::Workspace&, Probability::Workspace&); /* 私有复用缓存 */ };
        static Influence findInfluence(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure, const Probability::Result &probability, Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, std::span<const ObservedBoard::CellId> dead, const Config &config, Workspace &workspace);
        static long double findInfluence(ObservedBoard::CellId cell, ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure, const Probability::Result &probability, Structure::Pool &shapes, ShapeSolver::DistributionPool &distributions, const Influence &full, const Config &config, Workspace &workspace);
    };

Influence 的数组索引契约为同一 board 的 CellId；findInfluence 单格与全盘两种重载共用 Config 中的 eqEps，不准再写第二个 1e-9 常量。单格扫描复用本次全盘 Influence 和 Workspace 缓存。

#### code/src/algo/winrate_engine/java_transplant/long_term_risk_helper.cpp

放全盘 2-tile/2x2 扫描、强制事实下重新计数、缓存命中和单格 tally 聚合。findInfluence 对其实际会临时更新的 board/basic/structure 明确接收可变引用；countWithForces 用 Delta 施加强制事实，按 Structure、Basic、ObservedBoard 的逆序撤回。它是模块内部原语，不出现在公开头文件；不复制完整派生状态，也不以 const_cast 隐藏写入。

    LongTermRiskReference::Influence LongTermRiskReference::findInfluence(ObservedBoard::Result &board, Basic::Result &basic, Structure::Result &structure, ..., const Config &config, Workspace &workspace) {
        // 用当前 Probability 方案数统计风险结构，并保留本局可复用 tally。
    }

#### code/src/algo/winrate_engine/java_transplant/pseudo_helper.h

这是一个由 JavaEvaluate 调用的独立猜链算法模块，不是另一份 JavaEvaluate facade。头文件只暴露 findPseudo5050；Link、Chain、assess、排序及邻接扫描全部留在同名 .cpp。将 const Config& 传入，使“确定雷”使用同一 LongTermRisk::eqEps。

    struct PseudoReference {
        static std::vector<ObservedBoard::CellId> findPseudo5050(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Probability::Result &probability, std::span<const ObservedBoard::CellId> dead, const Config &config);
    };

返回的 CellId 属于传入 ObservedBoard；空向量表示未找到链。函数不改输入 Result，且不将 Link/Chain 的 Java 移植中间态暴露给 JavaEvaluate 或 UI。

#### code/src/algo/winrate_engine/java_transplant/pseudo_helper.cpp

放 Link/Chain 建链、封闭性判定、死格豁免、选择 living chain 和排序实现；实现命名空间为 PseudoReferenceInternal。删掉固定 kEqEps，让所有概率相等判断读取传入 Config。JavaEvaluate 调用这个算法一次；由它直接返回可参与候选筛选的 CellId，不再额外包装为 API DTO。

    std::vector<ObservedBoard::CellId> PseudoReference::findPseudo5050(..., const Config &config) {
        // 建立并排序候选链，返回可选择的格。
    }

#### code/src/algo/winrate_engine/monte_carlo/monte_carlo.h

模块使用同级 Config、Result、Workspace 与 analyze / update。Candidate 只公开 cellId、samples、wins；Result::winrate 区分整局与首招模式。初始目标可由 UI 传入，也可使用 Java 自动候选；后续策略复用 winrate::SuggestAlgo。引用有效期、取消与缓存契约见当前头文件。

#### code/src/algo/winrate_engine/monte_carlo/monte_carlo.cpp

根评分和合法雷盘抽样直接调用 probability_engine；完整模拟必须复用 Game::suggest / Game::play。首招调度沿用归档的竞争机会与预计方差下降，各首招共用雷盘种子序列，只累计完整终局的 0/1 统计。有必安格时退化为整局胜率采样，首招模式保证没有必安格。固定根下首招位置与数字确定的第一次后续建议缓存复用，后续回合由 Game 计算。

### test：用例作为独立编译单元，共享层只留真正共用的驱动

#### code/test/test_main.cpp

独立测试可执行入口，只把命令行交给 harness；生产 code/main.cpp 不再包含测试头文件或通过注释切换运行模式。

    int main(int argc, char **argv) { return test::run(argc, argv); }

#### code/test/harness.h

只声明命令行测试选择入口，不 include 每个测试的实现头文件。

    namespace test { int run(int argc, char **argv); }

#### code/test/harness.cpp

包含窄的具名测试表，将稳定的命令行名字映射到非模板 run 函数。一个名字运行一个用例，避免默认启动全部 benchmark；未知名字属于错误输入，直接在命令边界报告失败，不落到默认用例。表只作测试入口，不建通用 fixture、插件或注册框架。

    struct TestCase { const char *name; void (*run)(); };
    const TestCase cases[] = { {"bruteforce", bruteforce}, {"graph-solver-frequency", graphSolverFrequency}, {"performance", performance}, {"probability-case", probabilityCase} };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i)
        if (strcmp(argv[1], cases[i].name) == 0) { cases[i].run(); return 0; }
    assert_(false, "test::run: 未知测试名");

#### code/test/common.h

保留测试自己的 RunConfig，不删除或替换成 mss::Config。RunConfig 汇总随机多局测试共同使用的 rows、cols、mines、seconds、maxGames 和测试模式；当前 RunConfig 只有测试算法选择，其他字段仍散在各用例和 runGames 位置参数中。runGames 接收一个测试配置、固定 seed 和必要回调，按 seconds 与 maxGames 的先到限制结束。调用 Position::suggest 时，若其公开签名仍要求 mss::Config，就在适配该生产 API 的窄边界提供生产参数；不要把生产 Config 变成通用测试配置或 runGames 的配置类型。Game 当前公开 number、makeFirstMoveSafe、update、won、playUntilNoSafe；没有单格点击、旗标或 chord API，不能在文档中声称这些操作已经由 Game API 提供。

    struct TimeBox { std::chrono::steady_clock::time_point start, deadline; explicit TimeBox(double seconds); bool expired() const; double elapsedSeconds() const; };
    struct RunSummary { long long games; long long wins; double elapsedSeconds; long long javaEvalCalls; double javaEvalSeconds; };
    struct RunConfig { int rows; int cols; int mines; double seconds; long long maxGames; enum class Algorithm { LowRisk, JavaEval }; Algorithm algorithm; };
    template <typename DecisionFn, typename GameFn>
    RunSummary runGames(const RunConfig &config, mss::U128 seed, DecisionFn &&perDecision, GameFn &&perGame);

runGames 因 perDecision/perGame 是模板回调而保留在头文件；不为统一 API 再包一层 callback base class。新增测试前必须先完整阅读 CODE_REVIEW_PROMPT.md、implementation.md 和 test.md。需要核实当前 runGames 或 Game 行为时，再读对应源码定义和调用点；不能把源码注释当成替代 Prompt。

#### code/test/common.cpp

实现 TimeBox 的非模板计时方法，声明先行于实现；对局模板留在 common.h。它不实现一份 test 专属 MineBoard、数字计算或开格规则。

    bool TimeBox::expired() const { return std::chrono::steady_clock::now() >= deadline; }

#### code/test/bruteforce.h / code/test/bruteforce.cpp

头文件只声明 bruteforce；固定残局、布局计数和后端一致性断言留在 cpp。调用 BruteForce 与 Probability 的实际 API，测试数据与 benchmark 输出不进 common。

    namespace test { void bruteforce(); }

#### code/test/graph_solver_frequency.h / code/test/graph_solver_frequency.cpp

头文件只声明 graphSolverFrequency；组件采样统计和首个 GraphSolver 盘面输出留在 cpp。测试继续通过 runGames 观察 Game/Position，不能重写组件解析或玩法规则。

    namespace test { void graphSolverFrequency(); }

#### code/test/performance.h / code/test/performance.cpp

头文件只声明 performance；板型、策略 Config、运行秒数、吞吐量汇总输出留在 cpp。TimeBox 是协作式期限；运行 benchmark 时由执行工具另设外部硬超时，按目标时长加启动/清理余量估算，不在 common 里再做一个通用超时线程。性能结果报告目标时长和实际时长。

    namespace test { void performance(); }

#### code/test/probability_case.h / code/test/probability_case.cpp

头文件只声明 probabilityCase；固定雷盘、逐步更新、超时和概率不变量断言留在 cpp。它可直接测概率引擎增量接口，但棋盘动作统一走 Game API；只有确实需要隔离验证底层 Delta 时才直接构造 ObservedBoard。

    namespace test { void probabilityCase(); }

### 构建入口和跨文件数据流

#### code/main.cpp

仅生产启动与 main 规范；不 include test/harness.h。保留清空本次运行日志的现有行为；`.h/.cpp` 分工规范留在文件开头，不把测试宏塞回生产入口。

    int main() {
        { std::ofstream currentRunLog("mss_current_run.log", std::ios::trunc); }
        mss::UI::run();
        return 0;
    }

#### vs/mss1.5.vcxproj 与 vs/mss1.5.vcxproj.filters

两份项目文件显式列出每个 ClCompile/ClInclude，因此为每个新增 .cpp 同时登记源码项和过滤器显示项；filters 的虚拟目录与磁盘目录同构。Test 配置的入口改为 test_main.cpp，不再用 code/main.cpp + 宏猜测测试模式。PostBuild 保留 ui/web/* 资源复制，并把新的 JS 模块列入 HTTP 允许服务的资源集合。

    <ClCompile Include="..\code\src\algo\winrate_engine\monte_carlo\monte_carlo.cpp" />
    <ClCompile Include="..\code\test\test_main.cpp" Condition="'$(Configuration)'=='Test'" />

#### build.bat

递归收集 code/src 下的产品 .cpp，并单独以 code/main.cpp 链接生产程序；test 目标另选 code/test/*.cpp 与 test_main.cpp，绝不把两个 main 链在一起。复制 ui/web 资源后，HttpServer allowlist 仍需显式覆盖拆出的模块文件。

#### 请求、领域状态与引擎结果

一次游戏点击从浏览器 game_controller 发到 /api/game/activate；Api 解析 CellId 后调用 Session::game().activate；Game 更新 Position 和自己的旗标/终局；Api 编码实际的 ObservedBoard 与 changed cells。浏览器只替换服务器快照。

分析请求由 Api 解析出当前 ObservedBoard、CellId 与单个 Config，调用 Session::analysis().suggest；Position::suggest（定义仍在 game.cpp）决定 Safe/LowRisk/BruteForce/JavaEvaluate/MCTS 的策略入口。MonteCarlo 的根评分直接调用 JavaEval，模拟中的后续策略由 Game::suggest 执行。UI 请求的 includeProbabilities 在 Api 本次响应编码时处理，不传入引擎 Config。

winrate_engine 返回的候选、推荐和当前 MCTS 命名的搜索统计全部用 CellId；只有 Api 在表单与 JSON 编码的边界做一次行列转换。Workspace 沿 core/workspace.h 组合并由 Position/Session 生命周期持有；异步搜索 job 自己拥有搜索 Workspace。test::runGames 使用测试自己的 RunConfig 与 seed，调用 Game 和 Position API；测试配置不得与生产 mss::Config 合并。Position::suggest 的当前签名若要求 mss::Config，只在调用该生产 API 时转换测试模式，不让 mss::Config 成为测试层的公共运行配置。

#### 分阶段落地与本草案的边界

1. 先按同一接口把三份 winrate 引擎实现搬到同名 cpp，并锁定 CellId/Config/Workspace 契约；MonteCarlo 按当前头文件保留首招采样与归档调度，完整对局复用 Game，接口迁移不能替代实测验证。
2. 再让 Game 接管浏览器与测试重复实现的真实开格规则；这一步需要跨 game、UI、test 多文件联动，拆成单独提交审阅。
3. 按协议职责拆 Session、Api、HttpServer，并对照每条现有 endpoint 保留行为；删除重复 SSE 生命周期后只保留一个 MCTS job owner。
4. 拆 JS 视图文件并显式更新服务器 allowlist；同步修改 VS 两份工程文件和 build.bat 编译入口。
5. 收敛 Config 的字段后再按接口运行点测与有硬时限的 performance；执行前先给运行时长估计和硬超时值。

这份草案和 prompt 是设计材料，不是新增 glue 或跨文件实现的批准。每一步代码实施仍按 TODO 和用户明确指令执行；执行审查只针对该步 diff，不顺手清理无关现状。
