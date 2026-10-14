#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stop_token>
#include <thread>

#include "test/common.h"
#include "core/assert.h"
#include "ui/internal/session.h"

namespace test::correctness {

inline void anytimeEndgame() {
    const std::string cells = "0001?101110112?11?????????????00022213?201?3222?????????????0112?22??5222?12?5??3?211?????01?323?4???12234??32??21?113?30112?32223211?2???21222??212?201122?10000011334210012???322212?111100000001?321112?23??22??211011100001122??32?2123432?2121102?421013?213??21101??222223?214???211??412233210123?11???23?4?43?124?3?101??1000111113312?3111112?43210122101110000?10111000002?4?100012222?211003310001110012?211112??3?33?200??11222?100122102?23?54?22?3222211??211001?1002?22?3?21112??";
    const std::array<std::pair<mss::winrate::CellId, int>, 3> expected{std::pair{55, 20023}, std::pair{51, 19630}, std::pair{79, 19336}};
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    for (int cell = 0; cell < 480; ++cell) if (cells[cell] != '?') board.board[cell / 30][cell % 30] = (mss::ObservedBoard::CellState)(cells[cell] - '0');
    mss::GameControl::Position position(std::move(board), workspace);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(input.probability.candidates() == 26028.0L, "test::anytimeEndgame: 固定盘面应有 26028 种布局");
    for (mss::BruteForce::Config::Solver solver : {mss::BruteForce::Config::Solver::Common, mss::BruteForce::Config::Solver::Bitwise}) {
        const mss::BruteForce::Config cfg(true, solver, 1000, 1);
        const mss::BruteForce::Result oracle = mss::BruteForce::solve(input.board, input.basic, input.structure, input.shapes, cfg, workspace.bruteForce);
        for (const std::pair<mss::winrate::CellId, int> &move : expected) {
            const std::vector<mss::BruteForce::Result::Move>::const_iterator found = std::find_if(oracle.moves.begin(), oracle.moves.end(), [&](const mss::BruteForce::Result::Move &candidate) { return candidate.cell == move.first; });
            mss::assert_(found != oracle.moves.end() && found->wins == move.second, "test::anytimeEndgame: 暴力首招胜局数变化");
        }
    }
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.longTermRisk, workspace.longTermRisk);
    mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, risk, {}, engineConfig.javaEval, engineConfig.moveEval, engineConfig.longTermRisk, engineConfig.pseudo, workspace.javaEval, false);
    const mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, std::move(evaluated), engineConfig, workspace.anytimeSearch, 1000000, {});
    mss::assert_(result.finished && result.candidates.size() == 3, "test::anytimeEndgame: 搜索未在预算内完整结束");
    mss::assert_(result.recommended() == 55, "test::anytimeEndgame: 完整搜索应推荐右侧 (2,26)");
    for (const mss::AnytimeSearch::Candidate &candidate : result.candidates) {
        const std::array<std::pair<mss::winrate::CellId, int>, 3>::const_iterator oracle = std::find_if(expected.begin(), expected.end(), [&](const std::pair<mss::winrate::CellId, int> &move) { return move.first == candidate.cellId; });
        mss::assert_(oracle != expected.end() && candidate.value.back() <= oracle->second / input.probability.candidates() + 1e-12L, "test::anytimeEndgame: 已完成评分不能高于暴力最优胜率");
    }
    // 一层仍是短期估计；第二层起逐层核对，不能只验证最终推荐。
    for (int depth = 2; depth <= result.completedDepth; ++depth) {
        const mss::AnytimeSearch::Candidate &best = *std::max_element(result.candidates.begin(), result.candidates.end(), [&](const mss::AnytimeSearch::Candidate &lhs, const mss::AnytimeSearch::Candidate &rhs) { return lhs.value[depth] < rhs.value[depth]; });
        mss::assert_(best.cellId == 55, "test::anytimeEndgame: 深层搜索仍把左侧排在右侧之前");
    }
    std::cout << "test/anytime_endgame: configurations=26028 depth=" << result.completedDepth << " nodes=" << result.node << " recommended=(2,26) score=" << std::setprecision(12) << result.candidates[0].value.back() << '\n';
}

inline void anytimeEquivalent() {
    // 对应数字分支中同一格必雷，Basic 却分别标 F/H；区域计数必须读取完整概率。
    const std::string cells = "000001?11?211?1000111001??????0000023322?22221112?1012??????001223??223?22?22?21101?3?3221112??4?32?22?333?32112334220001?334?2122213?3?3?11?2??2?2110223?21101?103?4121111223333?102?21100012334?2000111012?2?210?211110112???210002?201?3311002201?222?3332111102?3123?31000?2122?3?4?11222?10112?12??210023?2213?4222??311000111245?100??3?2224?32?4?2001121212??2100??54?3?4??31211012?2?2?2232211????3?3?4?2001222?22122322?2?1???32232211002??2221112??22432???101?1000002?311?11?22211??1";
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    for (int cell = 0; cell < 480; ++cell) if (cells[cell] != '?') board.board[cell / 30][cell % 30] = (mss::ObservedBoard::CellState)(cells[cell] - '0');
    mss::GameControl::Position position(std::move(board), workspace);
    const mss::winrate::Input root = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    for (int branch = 0; branch < 2; ++branch) {
        mss::ObservedBoard::Delta delta;
        delta.changes.emplace_back(branch == 0 ? 422 : 452, mss::ObservedBoard::CellState::Hidden, branch == 0 ? mss::ObservedBoard::CellState::Num6 : mss::ObservedBoard::CellState::Num3);
        mss::ObservedBoard::applyDelta(root.board, delta);
        mss::Basic::Delta basicDelta; mss::Basic::update(root.basic, basicDelta, root.board, delta, workspace.basic);
        mss::Structure::Delta structureDelta; const mss::winrate::CellId changed = delta.changes.front().cell; mss::Structure::update(root.structure, structureDelta, root.board, root.basic, root.shapes, std::span{&changed, 1}, workspace.structure);
        const mss::Probability::Result probability = mss::Probability::analyze(root.board, root.basic, root.structure, root.shapes, root.distributions, workspace.probability);
        const mss::winrate::Input input(root.board, root.basic, root.structure, probability, root.shapes, root.distributions);
        const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.longTermRisk, workspace.longTermRisk);
        std::vector<mss::winrate::CellId> offEdge, witnesses;
        for (int cell = 0; cell < 480; ++cell) {
            if (input.board.board[cell / 30][cell % 30] <= mss::winrate::CellState::Num8) witnesses.push_back(cell);
            else if (input.basic.marks[cell / 30][cell % 30] == mss::Basic::Mark::T) offEdge.push_back(cell);
        }
        mss::MoveEval::Selection::Workspace selection;
        const mss::winrate::Cells selected = mss::MoveEval::Selection::select(input, risk, {}, offEdge, witnesses, offEdge.empty() ? -1 : offEdge.front(), engineConfig.javaEval, selection);
        mss::assert_(input.probability.mineProbability(391, input.board, input.basic, input.structure) == 1.0L, "test::anytimeEquivalent: 两个分支的 (14,2) 都必须为雷");
        mss::assert_((input.basic.marks[13][1] == mss::Basic::Mark::F) == (branch == 0), "test::anytimeEquivalent: 固定盘面应覆盖 Basic 标雷与仅概率确认雷两种路径");
        const std::array<mss::winrate::CellId, 5> expected{25, 26, 27, 28, 29};
        mss::assert_(selected.size() == expected.size() && std::equal(selected.begin(), selected.end(), expected.begin()), "test::anytimeEquivalent: 把必雷当通路会使七格区域跨过八格阈值");
        mss::Structure::applyDelta(root.structure, root.shapes, structureDelta, true);
        mss::Basic::applyDelta(root.basic, basicDelta, true);
        mss::ObservedBoard::applyDelta(root.board, delta, true);
    }
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(root, {}, engineConfig.longTermRisk, workspace.longTermRisk);
    for (bool full : {false, true}) {
        const mss::Config cfg(engineConfig.bruteForce, engineConfig.longTermRisk, engineConfig.pseudo, engineConfig.moveEval, engineConfig.javaEval, {{6, 3, 1}, full}, engineConfig.monteCarlo);
        const std::array<mss::winrate::CellId, 2> expected{422, 452};
        mss::JavaEval::Result evaluated = full ? mss::JavaEval::analyze(root, risk, expected, cfg.moveEval, cfg.longTermRisk, workspace.javaEval, false) : mss::JavaEval::analyze(root, risk, {}, cfg.javaEval, cfg.moveEval, cfg.longTermRisk, cfg.pseudo, workspace.javaEval, false);
        mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(root, std::move(evaluated), cfg, workspace.anytimeSearch, full ? 0 : 3400, {});
        mss::assert_(result.candidates.size() == 2, "test::anytimeEquivalent: 应分别保留上下两个互斥招法");
        int compared = 0;
        do {
            const mss::AnytimeSearch::Candidate &upper = result.candidates[0], &lower = result.candidates[1];
            mss::assert_(upper.cellId == expected[0] && lower.cellId == expected[1], "test::anytimeEquivalent: 根候选坐标变化");
            for (int depth = compared; depth < (int)upper.value.size() && depth < (int)lower.value.size(); ++depth) {
                mss::assert_(std::abs(upper.value[depth] - lower.value[depth]) < 1e-12L, "test::anytimeEquivalent: 两个等价首招的同层评分不同");
                ++compared;
            }
            if (result.finished) break;
            mss::assert_(result.node < 3400, "test::anytimeEquivalent: 固定盘面搜索未在预算内结束");
            mss::AnytimeSearch::update(result, cfg, workspace.anytimeSearch, 17, {});
        } while (true);
        mss::assert_(result.completedDepth >= 14 && compared == result.completedDepth + 1, "test::anytimeEquivalent: 未覆盖完整十四层搜索");
        mss::UI::Session session(cfg);
        std::string requestCells = cells, response;
        if (full) requestCells[422] = requestCells[452] = 'G';
        mss::assert_(session.startSearch("rows=16&cols=30&mines=99&cells=" + requestCells + "&algo=auto&rootWidth=6&middleWidth=3&deepWidth=1&run=false", response), "test::anytimeEquivalent: UI 拒绝固定盘面");
        session.controlSearch("action=step&nodes=3400");
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        std::string snapshot;
        do {
            snapshot = session.searchUpdate();
            if (snapshot.find("\"finished\":true") != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        mss::assert_(snapshot.find("\"completedDepth\":14") != std::string::npos && snapshot.find("\"finished\":true") != std::string::npos, "test::anytimeEquivalent: UI 搜索没有完整结束");
        const std::size_t upperValues = snapshot.find("\"value\":["), lowerValues = snapshot.find("\"value\":[", upperValues + 1);
        mss::assert_(snapshot.substr(upperValues, snapshot.find(']', upperValues) - upperValues) == snapshot.substr(lowerValues, snapshot.find(']', lowerValues) - lowerValues), "test::anytimeEquivalent: UI 发布的逐层评分不同");
        std::cout << "test/anytime_equivalent: full=" << full << " configurations=880 layers=" << compared << " nodes=" << result.node << " score=" << result.candidates[0].value.back() << '\n';
    }
}

inline void anytimeFiftyFifty() {
    // A+B=1、B+C=1、C+D=1；两端各被两个数字重复约束，旁格无法区分两种雷位。
    const std::array<const char *, 5> rows{"??1", "??1", "?31", "??1", "??1"};
    const std::array<mss::winrate::CellId, 4> expected{1, 4, 10, 13};
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(5, 3, 5);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 3; ++col)
            if (rows[row][col] != '?') board.board[row][col] = (mss::ObservedBoard::CellState)(rows[row][col] - '0');
    mss::GameControl::Position position(std::move(board), workspace);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(!position.error().failed && input.probability.candidates() == 12.0L, "test::anytimeFiftyFifty: 固定盘面应有 12 种布局");
    mss::Pseudo::Result pseudo = mss::Pseudo::analyze(input, {}, engineConfig.pseudo, workspace.pseudo);
    std::sort(pseudo.begin(), pseudo.end());
    mss::assert_(pseudo.size() == expected.size() && std::equal(pseudo.begin(), pseudo.end(), expected.begin()), "test::anytimeFiftyFifty: 重复数字约束破坏了不可避免的四格链");
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.longTermRisk, workspace.longTermRisk);
    mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, risk, {}, engineConfig.javaEval, engineConfig.moveEval, engineConfig.longTermRisk, engineConfig.pseudo, workspace.javaEval, false);
    mss::assert_(!evaluated.empty(), "test::anytimeFiftyFifty: JavaEval 漏掉 50-50 候选");
    for (const mss::MoveEval::Analysis &move : evaluated)
        mss::assert_(std::find(expected.begin(), expected.end(), move.cellId) != expected.end(), "test::anytimeFiftyFifty: JavaEval 把链外格子当成候选");
    const mss::AnytimeSearch::Result searched = mss::AnytimeSearch::analyze(input, std::move(evaluated), engineConfig, workspace.anytimeSearch, 0, {});
    mss::assert_(searched.candidates.size() == 2, "test::anytimeFiftyFifty: Search 未保留两种互斥雷位的代表格");
    for (const mss::AnytimeSearch::Candidate &move : searched.candidates)
        mss::assert_(std::find(expected.begin(), expected.end(), move.cellId) != expected.end(), "test::anytimeFiftyFifty: Search 的根候选离开 50-50 链");
    std::cout << "test/anytime_fifty_fifty: configurations=12 chain=4 search_roots=2\n";
}

// 四角为 1 的初级盘面，左上八分之一区域的 14 个手选根招法。
inline void anytimeBeginner() {
    const mss::Config cfg(engineConfig.bruteForce, engineConfig.longTermRisk, engineConfig.pseudo, engineConfig.moveEval, engineConfig.javaEval, {{6, 3, 1}, true}, engineConfig.monteCarlo);
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(9, 9, 10);
    board.board[0][0] = board.board[0][8] = board.board[8][0] = board.board[8][8] = mss::ObservedBoard::CellState::Num1;
    mss::GameControl::Position position(std::move(board), workspace);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(input.probability.candidates() == 6690509280.0L, "test::anytimeBeginner: 固定盘面布局数变化");
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, cfg.longTermRisk, workspace.longTermRisk);
    std::vector<mss::winrate::CellId> cells;
    for (int row = 1; row <= 4; ++row)
        for (int col = 0; col <= row; ++col) cells.push_back(input.board.id(row, col));
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, cells, cfg.moveEval, cfg.longTermRisk, workspace.javaEval, false);
    mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, baseline, cfg, workspace.anytimeSearch, 0, {});
    while (result.completedDepth < 6 && result.node < 300000 && !result.finished) mss::AnytimeSearch::update(result, cfg, workspace.anytimeSearch, 1000, {});
    mss::assert_(result.completedDepth >= 6 && result.candidates.size() == 14, "test::anytimeBeginner: 第六层未完成或 G 根候选丢失");
    // 覆盖全部候选的七层评分；路径只乘结局概率，候选按完整启发式评分取宽度。
    std::array<std::array<long double, 7>, 14> expected{
        std::array{0.657981504876447721394L, 0.650745885049992927099L, 0.649560092737888372368L, 0.648237852253656323407L, 0.647638352385196604255L, 0.646586725487754242724L, 0.646301207490232518379L},
        std::array{0.646189011317588389723L, 0.638355352641443591353L, 0.637291211057767068059L, 0.636655767875946975636L, 0.636277136243856946459L, 0.635783012494065895043L, 0.635238499745072201011L},
        std::array{0.876240560814474462518L, 0.870556255753386014676L, 0.867139417534803380583L, 0.865085336263613611889L, 0.862836954029777326977L, 0.862185483590237018345L, 0.861678426060261314133L},
        std::array{0.865723613879276960399L, 0.857613410887882052821L, 0.855622604119642980508L, 0.854241801877234007421L, 0.851916675253683497091L, 0.850811831242372296625L, 0.850319635021757353453L},
        std::array{0.856059830528734228396L, 0.842675851522605986273L, 0.840622680260774860904L, 0.838207717426323771525L, 0.836549766222766108276L, 0.836045789561395703761L, 0.835411273679033944482L},
        std::array{0.878586161829212212277L, 0.868108698557694083539L, 0.866480372402876541362L, 0.865575315176634387426L, 0.86504076206236576107L, 0.864396672083874148919L, 0.86421013069500772979L},
        std::array{0.865088528923938564165L, 0.850185914719137213202L, 0.848031210374054830866L, 0.847344671426672776504L, 0.8467108071198132402L, 0.845968865586547485265L, 0.845952041186422303769L},
        std::array{0.865052381602145583294L, 0.85418021760602601413L, 0.852068621288156900578L, 0.850372021255668980469L, 0.849420318827929474636L, 0.848794511472128801444L, 0.848548478487023763288L},
        std::array{0.865052381602145583294L, 0.8501859217729693889L, 0.847830094184558794338L, 0.846442391893081634341L, 0.845572809475890292122L, 0.844948058490863972841L, 0.844796809912372751296L},
        std::array{0.878586161829212212277L, 0.868001149599241213944L, 0.866480372402876541362L, 0.865575315176634387426L, 0.865027360557410697614L, 0.864346563679264898801L, 0.864198473183458191181L},
        std::array{0.865088528923938564165L, 0.850284869999868983949L, 0.848661219400580476646L, 0.84828797325398407693L, 0.847947075698757652036L, 0.847074789862802259846L, 0.846941622336126848222L},
        std::array{0.865052381602145583294L, 0.854129342509657122988L, 0.852068621288156900578L, 0.850384670317257669635L, 0.849496211311998589979L, 0.848942962534830125956L, 0.848669677551335820327L},
        std::array{0.865052381602145583294L, 0.850185789193280491358L, 0.84782996595505729059L, 0.846495287910699665212L, 0.84535480660886971779L, 0.843959318636671470106L, 0.843587583122528022409L},
        std::array{0.865052381602145583294L, 0.850094468831204983124L, 0.84782996595505729059L, 0.846486743203124602998L, 0.845513234303577060054L, 0.844036218273648953679L, 0.843736249088646666616L},
    };
    long double maxError = 0.0L;
    for (int i = 0; i < (int)result.candidates.size(); ++i)
        for (int depth = 0; depth <= 6; ++depth) {
            const long double error = std::abs(result.candidates[i].value[depth] - expected[i][depth]);
            mss::assert_(error < (depth <= 3 ? 1e-12L : 1e-7L), "test::anytimeBeginner: 候选的逐层评分变化");
            maxError = (std::max)(maxError, error);
        }
    std::cout << "test/anytime_beginner_scores: checked=98 max_error=" << maxError << '\n';
    for (int depth : {5, 6}) {
        const mss::AnytimeSearch::Candidate &best = *std::max_element(result.candidates.begin(), result.candidates.end(), [&](const mss::AnytimeSearch::Candidate &lhs, const mss::AnytimeSearch::Candidate &rhs) { return lhs.value[depth] < rhs.value[depth]; });
        mss::assert_(best.cellId == 27, "test::anytimeBeginner: 深层搜索把 (1,5) 排到 (1,4) 前面");
        std::cout << std::setprecision(12) << "test/anytime_beginner: roots=14 depth=" << depth << " recommended=(1,4) score=" << best.value[depth] << " nodes=" << result.node << '\n';
    }
}

inline void anytimePruning() {
    const mss::Config fullConfig(engineConfig.bruteForce, engineConfig.longTermRisk, engineConfig.pseudo, engineConfig.moveEval, engineConfig.javaEval, {{1, 3, 1}, true}, engineConfig.monteCarlo);
    const mss::Config autoConfig(engineConfig.bruteForce, engineConfig.longTermRisk, engineConfig.pseudo, engineConfig.moveEval, engineConfig.javaEval, {{6, 3, 1}, false}, engineConfig.monteCarlo);
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    board.board[0][0] = mss::ObservedBoard::CellState::Num2;
    board.board[0][28] = board.board[1][28] = board.board[2][29] = mss::ObservedBoard::CellState::Num1;
    board.board[0][29] = board.board[1][29] = mss::ObservedBoard::CellState::Num0;
    board.board[2][27] = mss::ObservedBoard::CellState::Num3;
    board.board[2][28] = mss::ObservedBoard::CellState::Num2;
    mss::GameControl::Position position(std::move(board), workspace);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.longTermRisk, workspace.longTermRisk);
    const std::array<mss::winrate::CellId, 2> cells{27, 117};
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, cells, engineConfig.moveEval, engineConfig.longTermRisk, workspace.javaEval, false);
    mss::assert_(baseline[0].equivalence && baseline[0].equivalence == baseline[1].equivalence && baseline[0].branches.size() == 3 && baseline[1].branches.size() == 5, "test::anytimePruning: 两个活等价格的状态识别或数字分支改变");
    mss::assert_(std::abs(baseline[0].value - 0.780792365410507672L) < 1e-12L && std::abs(baseline[1].value - 0.782020032431476109L) < 1e-12L, "test::anytimePruning: 等价元数据改写了原 JavaEval 评分");
    const mss::JavaEval::Result automatic = mss::JavaEval::analyze(input, risk, {}, engineConfig.javaEval, engineConfig.moveEval, engineConfig.longTermRisk, engineConfig.pseudo, workspace.javaEval, false);
    for (mss::winrate::CellId cell : cells)
        mss::assert_(std::any_of(automatic.begin(), automatic.end(), [&](const mss::MoveEval::Analysis &move) { return move.cellId == cell && move.value >= 0.0L; }), "test::anytimePruning: JavaEval 错误启用了等价类剪枝");
    mss::AnytimeSearch::Result selected = mss::AnytimeSearch::analyze(input, baseline, autoConfig, workspace.anytimeSearch, 0, {});
    mss::assert_(selected.candidates.size() == 1 && selected.candidates.front().cellId == 117 && selected.node == baseline[0].node + baseline[1].node, "test::anytimePruning: Search 根去重失效或重复计算了强制安全");
    mss::AnytimeSearch::Result full = mss::AnytimeSearch::analyze(input, baseline, fullConfig, workspace.anytimeSearch, 0, {});
    mss::assert_(full.candidates.size() == 2 && full.candidates[0].value.front() == baseline[0].value && full.candidates[1].value.front() == baseline[1].value, "test::anytimePruning: 指定等价格被剪掉或复制评分");
    // A+B=1、B+C=1；A(0) 是 dead，C(4) 是活格，二者同生共死。
    // 6、13 与它们安全率相同，但不属于同一等价类。
    const std::array<const char *, 2> mixedRows{"?4?4???", "MMMMMM?"};
    mss::ObservedBoard::Result mixedBoard(2, 7, 8);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 7; ++col) {
            const char cell = mixedRows[row][col];
            if (cell == 'M') mixedBoard.board[row][col] = mss::ObservedBoard::CellState::ForcedMine;
            else if (cell != '?') mixedBoard.board[row][col] = (mss::ObservedBoard::CellState)(cell - '0');
        }
    mss::Workspace mixedWorkspace;
    mss::GameControl::Position mixedPosition(std::move(mixedBoard), mixedWorkspace);
    const mss::winrate::Input mixedInput = mixedPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(!mixedPosition.error().failed && mixedInput.probability.candidates() == 4.0L, "test::anytimePruning: 混合等价类盘面不满足四种布局");
    const mss::LongTermRisk::Result mixedRisk = mss::LongTermRisk::analyze(mixedInput, {}, engineConfig.longTermRisk, mixedWorkspace.longTermRisk);
    const std::array<mss::winrate::CellId, 4> mixedCells{0, 4, 6, 13};
    const mss::JavaEval::Result mixedBaseline = mss::JavaEval::analyze(mixedInput, mixedRisk, mixedCells, engineConfig.moveEval, engineConfig.longTermRisk, mixedWorkspace.javaEval, false);
    mss::assert_(mixedBaseline[0].branches.size() == 1 && mixedBaseline[1].branches.size() > 1 && mixedBaseline[0].equivalence == mixedBaseline[1].equivalence && mixedBaseline[1].equivalence != mixedBaseline[2].equivalence && mixedBaseline[2].equivalence != mixedBaseline[3].equivalence, "test::anytimePruning: 混合类或同安全率的非等价格未正确区分");
    mss::AnytimeSearch::Result mixedSelected = mss::AnytimeSearch::analyze(mixedInput, mixedBaseline, autoConfig, mixedWorkspace.anytimeSearch, 0, {});
    mss::assert_(mixedSelected.candidates.size() == 3 && std::none_of(mixedSelected.candidates.begin(), mixedSelected.candidates.end(), [](const mss::AnytimeSearch::Candidate &move) { return move.cellId == 0; }) && std::any_of(mixedSelected.candidates.begin(), mixedSelected.candidates.end(), [](const mss::AnytimeSearch::Candidate &move) { return move.cellId == 4; }), "test::anytimePruning: dead 抢占类代表或误删不同等价类");
    const mss::JavaEval::Result mixedAutomatic = mss::JavaEval::analyze(mixedInput, mixedRisk, {}, engineConfig.javaEval, engineConfig.moveEval, engineConfig.longTermRisk, engineConfig.pseudo, mixedWorkspace.javaEval, false);
    for (const mss::MoveEval::Analysis &move : mixedAutomatic)
        mss::assert_(move.value < 0.0L || move.branches.size() > 1, "test::anytimePruning: 有活格时 JavaEval 推荐 dead 格");
    // A+B=1、B+C=1，总共两雷；A/C 安全会让远端 T 必雷，T 安全则让 B 必安。
    mss::Workspace globalWorkspace;
    mss::ObservedBoard::Result globalBoard(1, 6, 2);
    globalBoard.board[0][1] = globalBoard.board[0][3] = mss::ObservedBoard::CellState::Num1;
    mss::GameControl::Position globalPosition(std::move(globalBoard), globalWorkspace);
    const mss::winrate::Input globalInput = globalPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(globalInput.probability.candidates() == 2.0L && globalInput.basic.unknownSum == 1 && globalInput.probability.tCellProbability() == 0.5L, "test::anytimePruning: 全局确定性盘面不是两种布局");
    const mss::LongTermRisk::Result globalRisk = mss::LongTermRisk::analyze(globalInput, {}, engineConfig.longTermRisk, globalWorkspace.longTermRisk);
    const std::array<mss::winrate::CellId, 3> globalCells{0, 4, 5};
    const mss::JavaEval::Result globalBaseline = mss::JavaEval::analyze(globalInput, globalRisk, globalCells, engineConfig.moveEval, engineConfig.longTermRisk, globalWorkspace.javaEval, false);
    mss::assert_(globalBaseline[0].equivalence == globalBaseline[1].equivalence && globalBaseline[0].equivalence != globalBaseline[2].equivalence, "test::anytimePruning: 候选必安集合混淆了不同等价类");
    mss::assert_(globalInput.basic.unknownSum == 1 && globalPosition.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 2.0L && mss::Basic::checkValid(globalInput.board, globalInput.basic), "test::anytimePruning: 全局确定性分支没有完整回退");
    mss::Workspace deadWorkspace;
    mss::ObservedBoard::Result deadBoard(1, 3, 1);
    deadBoard.board[0][1] = mss::ObservedBoard::CellState::Num1;
    mss::GameControl::Position deadPosition(std::move(deadBoard), deadWorkspace);
    const mss::winrate::Input deadInput = deadPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result deadRisk = mss::LongTermRisk::analyze(deadInput, {}, engineConfig.longTermRisk, deadWorkspace.longTermRisk);
    const mss::JavaEval::Result deadAutomatic = mss::JavaEval::analyze(deadInput, deadRisk, {}, engineConfig.javaEval, engineConfig.moveEval, engineConfig.longTermRisk, engineConfig.pseudo, deadWorkspace.javaEval, false);
    mss::assert_(std::count_if(deadAutomatic.begin(), deadAutomatic.end(), [](const mss::MoveEval::Analysis &move) { return move.value >= 0.0L; }) == 1, "test::anytimePruning: 全 dead 时 JavaEval 未只保留一个必猜格");
    mss::AnytimeSearch::Result deadSearch = mss::AnytimeSearch::analyze(deadInput, deadAutomatic, autoConfig, deadWorkspace.anytimeSearch, 100, {});
    mss::assert_(deadSearch.candidates.size() == 1 && deadSearch.finished, "test::anytimePruning: 全 dead 的 Search 不能完成必猜终局");
    mss::UI::Session session(fullConfig);
    std::string response;
    mss::assert_(session.startSearch("rows=2&cols=7&mines=8&cells=G4?4G??MMMMMM?&algo=auto&rootWidth=1&middleWidth=3&deepWidth=1&run=false", response), "test::anytimePruning: Session 拒绝混合等价类的 G 根招法");
    std::string snapshot;
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do {
        snapshot = session.searchUpdate();
        if (snapshot.find("\"cell\":4") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    mss::assert_(snapshot.find("\"cell\":0") != std::string::npos && snapshot.find("\"cell\":4") != std::string::npos, "test::anytimePruning: UI G 的 dead 或等价根招法被剪掉");
    std::cout << "test/anytime_pruning: equivalent_roots=2->1, mixed_classes=3, dead_fallback=1, G_roots=2, zero_layer_nodes=" << selected.node << '\n';
}

inline void anytimeReplay() {
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(9, 9, 10);
    board.board[0][0] = board.board[0][8] = board.board[8][0] = board.board[8][8] = mss::ObservedBoard::CellState::Num1;
    board.board[4][4] = mss::ObservedBoard::CellState::ForcedSafe;
    mss::Basic::Result basic = mss::Basic::analyze(board, workspace.basic);
    mss::Structure::Pool shapes;
    mss::Structure::Result structure = mss::Structure::analyze(board, basic, shapes, workspace.structure);
    const std::array<mss::ObservedBoard::CellId, 2> cells{40, 20};
    const std::array<int, 2> digits{0, 1};
    std::array<mss::ObservedBoard::Delta, 2> boards;
    std::array<mss::Basic::Delta, 2> basics;
    std::array<mss::Structure::Delta, 2> structures;
    std::vector<mss::ObservedBoard::Result> boardStates{board};
    std::vector<mss::Basic::Result> basicStates{basic};
    std::vector<mss::Structure::Result> structureStates{structure};
    for (int i = 0; i < 2; ++i) {
        const auto [row, col] = board.pos(cells[i]);
        boards[i].changes.emplace_back(cells[i], board.board[row][col], (mss::ObservedBoard::CellState)digits[i]);
        mss::ObservedBoard::applyDelta(board, boards[i]);
        mss::Basic::update(basic, basics[i], board, boards[i], workspace.basic);
        mss::Structure::update(structure, structures[i], board, basic, shapes, std::span{&cells[i], 1}, workspace.structure);
        boardStates.push_back(board); basicStates.push_back(basic); structureStates.push_back(structure);
    }
    mss::assert_(std::count_if(basics[0].changes.begin(), basics[0].changes.end(), [](const mss::Basic::Delta::Change &change) { return change.cell == 31; }) == 2, "test::anytimeReplay: 未覆盖同格 T→H→S 的两次变化");
    auto check = [&](int state) {
        mss::assert_(mss::Basic::checkValid(board, basic) && structure.components == structureStates[state].components, "test::anytimeReplay: 重放后邻域计数或组件顺序改变");
        for (int cell = 0; cell < 81; ++cell) {
            const auto [row, col] = board.pos(cell);
            mss::assert_(board.board[row][col] == boardStates[state].board[row][col] && basic.marks[row][col] == basicStates[state].marks[row][col] && structure.cellLoc[cell].component == structureStates[state].cellLoc[cell].component && structure.cellLoc[cell].box == structureStates[state].cellLoc[cell].box, "test::anytimeReplay: 重放没有恢复完整状态");
        }
    };
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int i = 1; i >= 0; --i) {
            mss::Structure::applyDelta(structure, shapes, structures[i], true);
            mss::Basic::applyDelta(basic, basics[i], true);
            mss::ObservedBoard::applyDelta(board, boards[i], true);
            check(i);
        }
        for (int i = 0; i < 2; ++i) {
            mss::ObservedBoard::applyDelta(board, boards[i]);
            mss::Basic::applyDelta(basic, basics[i]);
            mss::Structure::applyDelta(structure, shapes, structures[i]);
            check(i + 1);
        }
    }
    std::cout << "test/anytime_replay: steps=2 repeats=3 forced_safe=true\n";
}

inline void anytimeSearch() {
    anytimeReplay();
    anytimeEndgame();
    anytimeEquivalent();
    anytimeFiftyFifty();
    anytimeBeginner();
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const mss::Config cfg(mss::BruteForce::Config{false, mss::BruteForce::Config::Solver::Bitwise, 1000, 1}, engineConfig.longTermRisk, engineConfig.pseudo, engineConfig.moveEval, engineConfig.javaEval, {{1, 2, 1}, true}, engineConfig.monteCarlo);
    // 中心数字约束八格中恰有两雷；无安全格，八个根招法均应保留。
    mss::ObservedBoard::Result board(3, 3, 2);
    board.board[1][1] = mss::ObservedBoard::CellState::Num2;
    mss::Workspace workspace;
    mss::GameControl::Position position(std::move(board), workspace);
    mss::assert_(!position.error().failed && position.safeMoves(mss::ShapeSolver::OrderAlgo::Auto).empty(), "test::anytimeSearch: 固定盘面不是有效猜测局面");
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    std::vector<mss::winrate::CellId> cells{0, 1, 2, 3, 5, 6, 7, 8};
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, cfg.longTermRisk, workspace.longTermRisk);
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, cells, cfg.moveEval, cfg.longTermRisk, workspace.javaEval, false);
    mss::AnytimeSearch::Result split = mss::AnytimeSearch::analyze(input, baseline, cfg, workspace.anytimeSearch, 0, {});
    mss::assert_(split.candidates.size() == cells.size() && split.completedDepth == 0 && split.node > 0, "test::anytimeSearch: 零预算丢失必算的第零层或完整根候选");
    for (int i = 0; i < (int)cells.size(); ++i)
        mss::assert_(split.candidates[i].value.size() == 1 && split.candidates[i].value[0] == baseline[i].value, "test::anytimeSearch: JavaEval 交接重复计算或改变了第零层");
    for (int i = 0; i < (int)cells.size(); ++i) {
        mss::assert_(split.candidates[i].branches.size() == baseline[i].branches.size(), "test::anytimeSearch: 第零层丢失数字分支");
        for (int branch = 0; branch < (int)baseline[i].branches.size(); ++branch)
            mss::assert_(split.candidates[i].branches[branch].digit == baseline[i].branches[branch].digit && split.candidates[i].branches[branch].value == baseline[i].branches[branch].value, "test::anytimeSearch: 第零层子评分没有复用已计算的基础评分");
    }
    const long long initialNodes = split.node;
    std::stop_source stopped;
    stopped.request_stop();
    mss::AnytimeSearch::update(split, cfg, workspace.anytimeSearch, 10, stopped.get_token());
    mss::assert_(split.node == initialNodes && split.completedDepth == 0, "test::anytimeSearch: 停止请求未阻止后续搜索");
    mss::Workspace resumedWorkspace;
    bool partial = false;
    int updates = 0;
    while (!split.finished && updates < 10000) {
        const long long before = split.node;
        mss::AnytimeSearch::update(split, cfg, updates % 2 == 0 ? workspace.anytimeSearch : resumedWorkspace.anytimeSearch, 1, {});
        mss::assert_(split.node - before <= 1, "test::anytimeSearch: 一节点预算发生超支");
        std::size_t deepest = 0;
        long double best = -1.0L;
        mss::winrate::CellId recommended = -1;
        for (const mss::AnytimeSearch::Candidate &candidate : split.candidates) {
            mss::assert_((int)candidate.value.size() >= split.completedDepth + 1 && (int)candidate.value.size() <= split.completedDepth + 2, "test::anytimeSearch: 逐层历史有空洞或重复发布");
            if ((int)candidate.value.size() > split.completedDepth + 1) partial = true;
            if (candidate.value.size() > deepest || (candidate.value.size() == deepest && candidate.value.back() > best)) { deepest = candidate.value.size(); best = candidate.value.back(); recommended = candidate.cellId; }
        }
        mss::assert_(split.recommended() == recommended, "test::anytimeSearch: 推荐不符合优先最深再取最高评分");
        // 在续算之间复用原工作区，不能破坏 Result 内的游标和分支资料。
        if (updates == 7) mss::JavaEval::analyze(input, risk, std::span{cells.data(), 1}, cfg.moveEval, cfg.longTermRisk, workspace.javaEval, false);
        ++updates;
    }
    mss::assert_(updates == split.node - initialNodes, "test::anytimeSearch: 最后一个概率节点算完后还需额外空步才发布结果");
    mss::assert_(split.finished && partial, "test::anytimeSearch: 未自然结束或没有发布部分层的完整候选");
    mss::AnytimeSearch::Result batch = mss::AnytimeSearch::analyze(input, baseline, cfg, workspace.anytimeSearch, 100000, {});
    mss::assert_(batch.finished && batch.node == split.node && batch.completedDepth == split.completedDepth && batch.recommended() == split.recommended(), "test::anytimeSearch: 单节点续算与整批搜索结果不同");
    for (int i = 0; i < (int)cells.size(); ++i) {
        mss::assert_(batch.candidates[i].value == split.candidates[i].value, "test::anytimeSearch: 预算切分改变了逐层评分");
        for (int branch = 0; branch < (int)batch.candidates[i].branches.size(); ++branch)
            mss::assert_(batch.candidates[i].branches[branch].value == split.candidates[i].branches[branch].value && batch.candidates[i].branches[branch].depth == split.candidates[i].branches[branch].depth, "test::anytimeSearch: 单节点续算改变了数字分支的子评分");
    }
    mss::assert_(mss::Basic::checkValid(input.board, input.basic) && input.board.board[1][1] == mss::ObservedBoard::CellState::Num2 && position.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 28.0L, "test::anytimeSearch: 搜索修改了调用方的根输入");
    const mss::Config automatic(mss::BruteForce::Config{false, mss::BruteForce::Config::Solver::Bitwise, 1000, 1}, cfg.longTermRisk, cfg.pseudo, cfg.moveEval, cfg.javaEval, {{2, 2, 1}, false}, cfg.monteCarlo);
    mss::AnytimeSearch::Result selected = mss::AnytimeSearch::analyze(input, baseline, automatic, workspace.anytimeSearch, 0, {});
    mss::assert_(selected.candidates.size() == 2, "test::anytimeSearch: 自动根宽度未生效");
    mss::Workspace terminalWorkspace;
    mss::GameControl::Position terminalPosition(mss::ObservedBoard::Result(1, 1, 0), terminalWorkspace);
    const mss::winrate::Input terminalInput = terminalPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result terminalRisk = mss::LongTermRisk::analyze(terminalInput, {}, cfg.longTermRisk, terminalWorkspace.longTermRisk);
    const mss::winrate::CellId lastSafe = 0;
    const mss::JavaEval::Result terminal = mss::JavaEval::analyze(terminalInput, terminalRisk, std::span{&lastSafe, 1}, cfg.moveEval, cfg.longTermRisk, terminalWorkspace.javaEval, false);
    mss::assert_(terminal.front().value == 1.0L && terminal.front().branches.size() == 1 && terminal.front().branches.front().probability == 1.0L, "test::anytimeSearch: 揭开最后安全格的终局价值不是 1");
    // 两组约束为 A+D=1、D+B+C=1；A 安全必推出 B/C 安全，B 比 A 更安全。
    const std::array<const char *, 3> dominanceRows{"?34M??", "?MMM??", "?3?M??"};
    mss::ObservedBoard::Result dominanceBoard(3, 6, 8);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 6; ++col) {
            const char cell = dominanceRows[row][col];
            if (cell == 'M') dominanceBoard.board[row][col] = mss::ObservedBoard::CellState::ForcedMine;
            else if (cell != '?') dominanceBoard.board[row][col] = (mss::ObservedBoard::CellState)(cell - '0');
        }
    mss::Workspace dominanceWorkspace;
    mss::GameControl::Position dominancePosition(std::move(dominanceBoard), dominanceWorkspace);
    mss::assert_(!dominancePosition.error().failed && dominancePosition.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 27.0L, "test::anytimeSearch: 支配格固定盘面不满足 27 种布局");
    const mss::winrate::Input dominanceInput = dominancePosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result dominanceRisk = mss::LongTermRisk::analyze(dominanceInput, {}, cfg.longTermRisk, dominanceWorkspace.longTermRisk);
    const std::array<mss::winrate::CellId, 2> dominanceCells{0, 12};
    const mss::JavaEval::Result pruned = mss::JavaEval::analyze(dominanceInput, dominanceRisk, dominanceCells, cfg.moveEval, cfg.longTermRisk, dominanceWorkspace.javaEval, true);
    const mss::JavaEval::Result full = mss::JavaEval::analyze(dominanceInput, dominanceRisk, dominanceCells, cfg.moveEval, cfg.longTermRisk, dominanceWorkspace.javaEval, false);
    mss::assert_(pruned[0].value == -1.0L && pruned[1].value >= 0.0L && full[0].value >= 0.0L && full[1].value >= 0.0L, "test::anytimeSearch: 支配准备剪枝或完整指定格语义错误");
    mss::AnytimeSearch::Result fullDominance = mss::AnytimeSearch::analyze(dominanceInput, full, cfg, dominanceWorkspace.anytimeSearch, 0, {});
    mss::assert_(fullDominance.candidates.size() == 2, "test::anytimeSearch: 完整根分析杀掉了指定的支配招法");
    mss::Workspace cancelWorkspace;
    mss::GameControl::Position cancelPosition(mss::ObservedBoard::Result(5, 5, 5), cancelWorkspace);
    const mss::winrate::Input cancelInput = cancelPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result cancelRisk = mss::LongTermRisk::analyze(cancelInput, {}, cfg.longTermRisk, cancelWorkspace.longTermRisk);
    const mss::winrate::CellId corner = 0;
    mss::JavaEval::Result cancelBaseline = mss::JavaEval::analyze(cancelInput, cancelRisk, std::span{&corner, 1}, cfg.moveEval, cfg.longTermRisk, cancelWorkspace.javaEval, false);
    mss::AnytimeSearch::Result cancelled = mss::AnytimeSearch::analyze(cancelInput, std::move(cancelBaseline), cfg, cancelWorkspace.anytimeSearch, 0, {});
    std::stop_source cancel;
    std::jthread requestStop([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); cancel.request_stop(); });
    mss::AnytimeSearch::update(cancelled, cfg, cancelWorkspace.anytimeSearch, 10000000, cancel.get_token());
    requestStop.join();
    mss::assert_(cancel.stop_requested() && !cancelled.finished, "test::anytimeSearch: 运行中的停止请求未中断搜索");
    const long long cancelledNodes = cancelled.node;
    mss::AnytimeSearch::update(cancelled, cfg, cancelWorkspace.anytimeSearch, 1, {});
    mss::assert_(cancelled.node - cancelledNodes <= 1, "test::anytimeSearch: 停止后不能以新 token 继续单节点计算");
    mss::UI::Session session(cfg);
    std::string response;
    auto waitUpdate = [&](const std::string &expected) {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        std::string snapshot;
        do {
            snapshot = session.searchUpdate();
            if (snapshot.find(expected) != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        mss::assert_(snapshot.find(expected) != std::string::npos, "test::anytimeSearch: Session 未发布预期结果");
        return snapshot;
    };
    mss::assert_(session.startSearch("rows=3&cols=3&mines=2&cells=GGGG2GGGG&algo=auto&rootWidth=1&middleWidth=2&deepWidth=1&run=false", response), "test::anytimeSearch: Session 拒绝有效的指定格分析");
    const std::string sessionBaseline = waitUpdate("\"cell\":8");
    mss::assert_(sessionBaseline.find("\"paused\":true") != std::string::npos, "test::anytimeSearch: Session 的第零层未暂停等待预算");
    session.controlSearch("action=step&nodes=1");
    waitUpdate("\"nodes\":" + std::to_string(initialNodes + 1));
    // 请求 stop 后重新启动，会 join 正在 stop_token 条件等待中的旧任务。
    mss::assert_(session.startSearch("rows=1&cols=5&mines=1&cells=SS???&algo=auto&rootWidth=1&middleWidth=2&deepWidth=1&run=false", response), "test::anytimeSearch: Session 的暂停任务不能停止和重启");
    const std::string safeUpdate = waitUpdate("\"safe\":true");
    mss::assert_(safeUpdate.find("\"cell\":0") != std::string::npos && safeUpdate.find("\"cell\":1") != std::string::npos && safeUpdate.find("\"cell\":2") == std::string::npos, "test::anytimeSearch: Session 未完整返回全部安全格");
    mss::assert_(session.startSearch("rows=1&cols=2&mines=1&cells=1M&algo=auto&rootWidth=1&middleWidth=2&deepWidth=1&run=false", response), "test::anytimeSearch: Session 拒绝已完成盘面");
    const std::string completeUpdate = waitUpdate("\"finished\":true");
    mss::assert_(completeUpdate.find("\"moves\":[]") != std::string::npos, "test::anytimeSearch: 已完成盘面仍返回候选");
    anytimePruning();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "test/anytime_search: board=3x3/2, layouts=28, roots=" << split.candidates.size() << ", nodes=" << split.node << ", depth=" << split.completedDepth << ", updates=" << updates << ", partial=" << partial << ", seconds=" << seconds << '\n';
}

} // 命名空间 test
