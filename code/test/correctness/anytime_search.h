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
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(input.probability.candidates() == 26028.0L, "test::anytimeEndgame: 固定盘面应有 26028 种布局");
    for (mss::BruteForce::Config::Solver solver : {mss::BruteForce::Config::Solver::Common, mss::BruteForce::Config::Solver::Bitwise}) {
        const mss::BruteForce::Config cfg(true, solver, 1000, 1);
        const mss::BruteForce::Result oracle = mss::BruteForce::solve(input.board, input.basic, input.structure, input.shapes, cfg, workspace.game.bruteForce);
        for (const std::pair<mss::winrate::CellId, int> &move : expected) {
            const std::vector<mss::BruteForce::Result::Move>::const_iterator found = std::find_if(oracle.moves.begin(), oracle.moves.end(), [&](const mss::BruteForce::Result::Move &candidate) { return candidate.cell == move.first; });
            mss::assert_(found != oracle.moves.end() && found->wins == move.second, "test::anytimeEndgame: 暴力首招胜局数变化");
        }
    }
    const mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, engineConfig.anytimeSearch, workspace.anytimeSearch, 1000000, {});
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
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input root = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    for (int branch = 0; branch < 2; ++branch) {
        mss::ObservedBoard::Delta delta;
        delta.changes.emplace_back(branch == 0 ? 422 : 452, mss::ObservedBoard::CellState::Hidden, branch == 0 ? mss::ObservedBoard::CellState::Num6 : mss::ObservedBoard::CellState::Num3);
        mss::ObservedBoard::applyDelta(root.board, delta.changes);
        mss::Basic::Delta basicDelta; mss::Basic::update(root.basic, basicDelta, root.board, delta, workspace.game.basic);
        mss::Structure::Delta structureDelta; const mss::winrate::CellId changed = delta.changes.front().cell; mss::Structure::update(root.structure, structureDelta, root.board, root.basic, root.shapes, std::span{&changed, 1}, workspace.game.structure);
        const mss::Probability::Result probability = mss::Probability::analyze(root.board, root.basic, root.structure, root.shapes, root.distributions, workspace.game.probability);
        const mss::winrate::Input input(root.board, root.basic, root.structure, probability, root.shapes, root.distributions);
        const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
        std::vector<mss::winrate::CellId> offEdge, witnesses;
        for (int cell = 0; cell < 480; ++cell) {
            if (input.board.board[cell / 30][cell % 30] <= mss::winrate::CellState::Num8) witnesses.push_back(cell);
            else if (input.basic.marks[cell / 30][cell % 30] == mss::Basic::Mark::T) offEdge.push_back(cell);
        }
        mss::MoveEval::Selection::Workspace selection;
        const std::vector<mss::winrate::CellId> selected = mss::MoveEval::Selection::window(input, &risk, offEdge, witnesses, offEdge.empty() ? -1 : offEdge.front(), engineConfig.game.javaEval.selection, selection);
        mss::assert_(input.probability.mineProbability(391, input.board, input.basic, input.structure) == 1.0L, "test::anytimeEquivalent: 两个分支的 (14,2) 都必须为雷");
        mss::assert_((input.basic.marks[13][1] == mss::Basic::Mark::F) == (branch == 0), "test::anytimeEquivalent: 固定盘面应覆盖 Basic 标雷与仅概率确认雷两种路径");
        const std::array<mss::winrate::CellId, 5> expected{25, 26, 27, 28, 29};
        mss::assert_(selected.size() == expected.size() && std::equal(selected.begin(), selected.end(), expected.begin()), "test::anytimeEquivalent: 把必雷当通路会使七格区域跨过八格阈值");
        mss::Structure::applyDelta(root.structure, root.shapes, structureDelta.removed, structureDelta.added, true);
        mss::Basic::applyDelta(root.basic, basicDelta.changes, true);
        mss::ObservedBoard::applyDelta(root.board, delta.changes, true);
    }
    std::vector<long double> automaticValues;
    for (bool full : {false, true}) {
        mss::Config cfg = engineConfig; cfg.anytimeSearch.fullAnalysis = full;
        const std::array<mss::winrate::CellId, 2> expected{422, 452};
        mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(root, cfg.anytimeSearch, workspace.anytimeSearch, full ? 0 : 4000, {}, full ? mss::winrate::Cells(expected) : mss::winrate::Cells{});
        mss::assert_(result.candidates.size() == (full ? 2 : 1), "test::anytimeEquivalent: 自动搜索应归并对换格，完整根分析应保留两格");
        int compared = 0;
        do {
            const mss::AnytimeSearch::Candidate &upper = result.candidates[0];
            mss::assert_(upper.cellId == expected[0], "test::anytimeEquivalent: 根候选坐标变化");
            if (full) {
                const mss::AnytimeSearch::Candidate &lower = result.candidates[1];
                mss::assert_(lower.cellId == expected[1], "test::anytimeEquivalent: 完整分析丢失第二个根招法");
                for (int depth = compared; depth < (int)upper.value.size() && depth < (int)lower.value.size(); ++depth) {
                    mss::assert_(std::abs(upper.value[depth] - lower.value[depth]) < 1e-12L, "test::anytimeEquivalent: 两个对换首招的同层评分不同");
                    mss::assert_(std::abs(upper.value[depth] - automaticValues[depth]) < 1e-12L, "test::anytimeEquivalent: 对换剪枝改变同层评分");
                    ++compared;
                }
            } else compared = upper.value.size();
            if (result.finished) break;
            mss::assert_(result.node < 4000, "test::anytimeEquivalent: 固定盘面搜索未在预算内结束");
            mss::AnytimeSearch::update(result, cfg.anytimeSearch, workspace.anytimeSearch, 17, {});
        } while (true);
        mss::assert_(result.completedDepth >= 14 && compared == result.completedDepth + 1, "test::anytimeEquivalent: 未覆盖完整十四层搜索");
        if (!full) automaticValues = result.candidates.front().value;
        mss::UI::Session session(cfg);
        std::string requestCells = cells, response;
        if (full) requestCells[422] = requestCells[452] = 'G';
        mss::assert_(session.startSearch("rows=16&cols=30&mines=99&cells=" + requestCells + "&algo=auto&rootWidth=6&middleWidth=3&deepWidth=1&run=false", response), "test::anytimeEquivalent: UI 拒绝固定盘面");
        session.controlSearch("action=step&nodes=4000");
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        std::string snapshot;
        do {
            snapshot = session.searchUpdate();
            if (snapshot.find("\"finished\":true") != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        mss::assert_(snapshot.find("\"completedDepth\":14") != std::string::npos && snapshot.find("\"finished\":true") != std::string::npos, "test::anytimeEquivalent: UI 搜索没有完整结束");
        const std::size_t upperValues = snapshot.find("\"value\":["), lowerValues = snapshot.find("\"value\":[", upperValues + 1);
        if (full) mss::assert_(snapshot.substr(upperValues, snapshot.find(']', upperValues) - upperValues) == snapshot.substr(lowerValues, snapshot.find(']', lowerValues) - lowerValues), "test::anytimeEquivalent: UI 发布的逐层评分不同");
        else mss::assert_(lowerValues == std::string::npos, "test::anytimeEquivalent: UI 自动搜索仍发布两个对换招法");
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
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(!position.error().failed && input.probability.candidates() == 12.0L, "test::anytimeFiftyFifty: 固定盘面应有 12 种布局");
    mss::Pseudo::Result pseudo = mss::Pseudo::analyze(input, {}, engineConfig.game.javaEval.selection.pseudo, workspace.game.javaEval.selection.pseudo);
    std::sort(pseudo.begin(), pseudo.end());
    mss::assert_(pseudo.size() == expected.size() && std::equal(pseudo.begin(), pseudo.end(), expected.begin()), "test::anytimeFiftyFifty: 重复数字约束破坏了不可避免的四格链");
    mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, engineConfig.game.javaEval, workspace.game.javaEval);
    mss::assert_(!evaluated.empty(), "test::anytimeFiftyFifty: JavaEval 漏掉 50-50 候选");
    for (const mss::MoveEval::Analysis &move : evaluated)
        mss::assert_(std::find(expected.begin(), expected.end(), move.cellId) != expected.end(), "test::anytimeFiftyFifty: JavaEval 把链外格子当成候选");
    const mss::AnytimeSearch::Result searched = mss::AnytimeSearch::analyze(input, engineConfig.anytimeSearch, workspace.anytimeSearch, 0, {});
    mss::assert_(searched.candidates.size() == 2, "test::anytimeFiftyFifty: Search 未保留两种互斥雷位的代表格");
    for (const mss::AnytimeSearch::Candidate &move : searched.candidates)
        mss::assert_(std::find(expected.begin(), expected.end(), move.cellId) != expected.end(), "test::anytimeFiftyFifty: Search 的根候选离开 50-50 链");
    std::cout << "test/anytime_fifty_fifty: configurations=12 chain=4 search_roots=2\n";
}

// 四角为 1 的初级盘面，左上八分之一区域的 14 个手选根招法。
inline void anytimeBeginner() {
    mss::Config cfg = engineConfig; cfg.anytimeSearch.fullAnalysis = true;
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(9, 9, 10);
    board.board[0][0] = board.board[0][8] = board.board[8][0] = board.board[8][8] = mss::ObservedBoard::CellState::Num1;
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(input.probability.candidates() == 6690509280.0L, "test::anytimeBeginner: 固定盘面布局数变化");
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, cfg.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
    std::vector<mss::winrate::CellId> cells;
    for (int row = 1; row <= 4; ++row)
        for (int col = 0; col <= row; ++col) cells.push_back(input.board.id(row, col));
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), cfg.game.javaEval, workspace.game.javaEval);
    mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), cfg.anytimeSearch, workspace.anytimeSearch, 0, {});
    while (result.completedDepth < 6 && result.node < 300000 && !result.finished) mss::AnytimeSearch::update(result, cfg.anytimeSearch, workspace.anytimeSearch, 1000, {});
    mss::assert_(result.completedDepth >= 6 && result.candidates.size() == 14, "test::anytimeBeginner: 第六层未完成或 G 根候选丢失");
    // 固定根层 JavaEval，改变长期风险参数；树内候选、展开顺序和各层评分都不应变化。
    mss::Config otherConfig = cfg; otherConfig.anytimeSearch.javaEval.moveEval.longTermRisk = {0.5L, 1e-6L}; otherConfig.anytimeSearch.javaEval.moveEval.influenceScale = 0.0L;
    mss::AnytimeSearch::Result other = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), cfg.anytimeSearch, workspace.anytimeSearch, 0, {});
    while (other.completedDepth < 6 && other.node < 300000 && !other.finished) mss::AnytimeSearch::update(other, otherConfig.anytimeSearch, workspace.anytimeSearch, 1000, {});
    mss::assert_(other.node == result.node && other.completedDepth == result.completedDepth && other.finished == result.finished && other.candidates.size() == result.candidates.size(), "test::anytimeBeginner: 树内展开仍依赖长期风险参数");
    for (int i = 0; i < (int)result.candidates.size(); ++i) {
        mss::assert_(result.candidates[i].cellId == baseline[i].cellId && result.candidates[i].value.front() == baseline[i].value, "test::anytimeBeginner: 根层 JavaEval 被短期评分覆写");
        mss::assert_(other.candidates[i].cellId == result.candidates[i].cellId && other.candidates[i].value == result.candidates[i].value, "test::anytimeBeginner: 树内逐层评分仍依赖长期风险参数");
    }
    std::cout << "test/anytime_beginner_scores: roots=14 risk_independent=true\n";
    for (int depth : {5, 6}) {
        const mss::AnytimeSearch::Candidate &best = *std::max_element(result.candidates.begin(), result.candidates.end(), [&](const mss::AnytimeSearch::Candidate &lhs, const mss::AnytimeSearch::Candidate &rhs) { return lhs.value[depth] < rhs.value[depth]; });
        mss::assert_(best.cellId == 27, "test::anytimeBeginner: 深层搜索把 (1,5) 排到 (1,4) 前面");
        std::cout << std::setprecision(12) << "test/anytime_beginner: roots=14 depth=" << depth << " recommended=(1,4) score=" << best.value[depth] << " nodes=" << result.node << '\n';
    }
}

inline void anytimeSwapPruning() {
    const std::string cells = "1?????????????201?101?10000000????????????5?2122101221000011????????????4211?22222?100002??????????????20113??3?3222113??????????????30002?45?31??12?4?????????????4100112??223312????????????????1111012322?11343????312?3????311?10001?3221??1??3?2123?23?310111011224?43321??2111?2111110111001?22????100??3211121101111?211112?4?42211????1002?212?2223?1012221101?1???31002?21?4?11?2212?10000222??3201121124?421111?32100001?2???202?3102???100012?21112123????202??102?421000012?11?2?12?";
    mss::Config automatic = engineConfig; automatic.anytimeSearch.width = {1000, 3, 1};
    mss::Config complete = automatic; complete.anytimeSearch.fullAnalysis = true;
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    for (int cell = 0; cell < 480; ++cell) if (cells[cell] != '?') board.board[cell / 30][cell % 30] = (mss::ObservedBoard::CellState)(cells[cell] - '0');
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, automatic.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 2> targets{451, 450};
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, mss::MoveEval::Selection::specified(input, targets, workspace.game.probability), automatic.game.javaEval, workspace.game.javaEval);
    mss::assert_(baseline.size() == 2 && baseline[1].multiplier > baseline[0].multiplier && std::abs(baseline[0].value / baseline[0].multiplier - baseline[1].value / baseline[1].multiplier) < 1e-15L, "test::anytimeSwapPruning: 复现盘面的两格基础分或 influence 关系改变");
    for (const std::array<mss::winrate::CellId, 2> &order : {targets, std::array<mss::winrate::CellId, 2>{450, 451}}) {
        mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, order, workspace.game.probability), automatic.anytimeSearch, workspace.anytimeSearch, 0, {});
        mss::assert_(result.candidates.size() == 1 && result.candidates.front().cellId == 450 && result.candidates.front().multiplier == baseline[1].multiplier && result.candidates.front().value.front() == baseline[1].value, "test::anytimeSwapPruning: 对换代表没有保留较高 influence 或依赖候选顺序");
        const long long before = result.node;
        mss::AnytimeSearch::update(result, automatic.anytimeSearch, workspace.anytimeSearch, 1024, {});
        mss::assert_(result.node - before <= 1024 && result.candidates.size() == 1 && result.candidates.front().cellId == 450, "test::anytimeSwapPruning: 对换剪枝后续算超预算或根代表改变");
    }
    const mss::AnytimeSearch::Result full = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, targets, workspace.game.probability), complete.anytimeSearch, workspace.anytimeSearch, 0, {});
    mss::assert_(full.candidates.size() == 2 && full.candidates[0].value.front() == baseline[0].value && full.candidates[1].value.front() == baseline[1].value, "test::anytimeSwapPruning: 完整根分析剪掉指定对换格或改写 JavaEval 分数");
    // 1-2-1 约束的等价变形：Basic 无法推导 (2,2)，Probability 才确认它为雷。
    const std::array<const char *, 4> probabilityRows{"M321", "M???", "??MM", "??MM"};
    mss::Workspace probabilityWorkspace;
    mss::ObservedBoard::Result probabilityBoard(4, 4, 9);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            const char value = probabilityRows[row][col];
            if (value == 'M') probabilityBoard.board[row][col] = mss::ObservedBoard::CellState::ForcedMine;
            else if (value != '?') probabilityBoard.board[row][col] = (mss::ObservedBoard::CellState)(value - '0');
        }
    mss::GameControl::Position probabilityPosition(std::move(probabilityBoard), probabilityWorkspace.game);
    mss::assert_(!probabilityPosition.error().failed, "test::anytimeSwapPruning: 概率必雷盘面无效");
    const mss::winrate::Input probabilityInput = probabilityPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(probabilityInput.basic.marks[1][1] != mss::Basic::Mark::F && probabilityInput.probability.mineProbability(5, probabilityInput.board, probabilityInput.basic, probabilityInput.structure) == 1.0L && probabilityInput.basic.marks[1][0] == mss::Basic::Mark::F, "test::anytimeSwapPruning: 未同时覆盖概率必雷和 Basic 必雷");
    const mss::LongTermRisk::Result probabilityRisk = mss::LongTermRisk::analyze(probabilityInput, {}, automatic.game.javaEval.moveEval.longTermRisk, probabilityWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 3> probabilityTargets{12, 8, 13};
    const mss::AnytimeSearch::Result probabilityResult = mss::AnytimeSearch::analyze(probabilityInput, probabilityRisk, mss::MoveEval::Selection::specified(probabilityInput, probabilityTargets, probabilityWorkspace.game.probability), automatic.anytimeSearch, probabilityWorkspace.anytimeSearch, 0, {});
    mss::assert_(probabilityResult.candidates.size() == 1, "test::anytimeSwapPruning: 必雷没有排除或三个对换格没有归并");
    // 两格虽然同雷率、隐藏邻格相同，却分别受 A+C=1 与全局 B+D=1 约束，不能交换雷位。
    const std::array<const char *, 2> differentRows{"2??M", "M??M"};
    mss::Workspace differentWorkspace;
    mss::ObservedBoard::Result differentBoard(2, 4, 5);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 4; ++col) {
            const char value = differentRows[row][col];
            if (value == 'M') differentBoard.board[row][col] = mss::ObservedBoard::CellState::ForcedMine;
            else if (value != '?') differentBoard.board[row][col] = (mss::ObservedBoard::CellState)(value - '0');
        }
    mss::GameControl::Position differentPosition(std::move(differentBoard), differentWorkspace.game);
    const mss::winrate::Input differentInput = differentPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result differentRisk = mss::LongTermRisk::analyze(differentInput, {}, automatic.game.javaEval.moveEval.longTermRisk, differentWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 2> differentTargets{5, 6};
    mss::assert_(differentInput.probability.mineProbability(5, differentInput.board, differentInput.basic, differentInput.structure) == 0.5L && differentInput.probability.mineProbability(6, differentInput.board, differentInput.basic, differentInput.structure) == 0.5L, "test::anytimeSwapPruning: 不同约束反例不是同雷率");
    const mss::AnytimeSearch::Result different = mss::AnytimeSearch::analyze(differentInput, differentRisk, mss::MoveEval::Selection::specified(differentInput, differentTargets, differentWorkspace.game.probability), automatic.anytimeSearch, differentWorkspace.anytimeSearch, 0, {});
    mss::assert_(different.candidates.size() == 2, "test::anytimeSwapPruning: 错把不同数字约束的格子判成可对换");
    mss::Workspace informationWorkspace;
    mss::GameControl::Position informationPosition(mss::ObservedBoard::Result(3, 3, 2), informationWorkspace.game);
    const mss::winrate::Input informationInput = informationPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result informationRisk = mss::LongTermRisk::analyze(informationInput, {}, automatic.game.javaEval.moveEval.longTermRisk, informationWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 2> informationTargets{0, 1};
    const mss::AnytimeSearch::Result information = mss::AnytimeSearch::analyze(informationInput, informationRisk, mss::MoveEval::Selection::specified(informationInput, informationTargets, informationWorkspace.game.probability), automatic.anytimeSearch, informationWorkspace.anytimeSearch, 0, {});
    mss::assert_(information.candidates.size() == 2, "test::anytimeSwapPruning: 隐藏邻格不同的相邻 T 被误剪");
    std::cout << std::setprecision(15) << "test/anytime_swap_pruning: roots=2->1 kept=(16,1) multiplier=" << baseline[1].multiplier << " score=" << baseline[1].value << " PE_and_Basic_mines=true group=3->1 constraints_and_information_negatives=2 full_roots=2\n";
}


inline void anytimePruning() {
    mss::Config fullConfig = engineConfig; fullConfig.anytimeSearch.width = {1, 3, 1}; fullConfig.anytimeSearch.fullAnalysis = true;
    mss::Config autoConfig = engineConfig;
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    board.board[0][0] = mss::ObservedBoard::CellState::Num2;
    board.board[0][28] = board.board[1][28] = board.board[2][29] = mss::ObservedBoard::CellState::Num1;
    board.board[0][29] = board.board[1][29] = mss::ObservedBoard::CellState::Num0;
    board.board[2][27] = mss::ObservedBoard::CellState::Num3;
    board.board[2][28] = mss::ObservedBoard::CellState::Num2;
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 2> cells{27, 117};
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), engineConfig.game.javaEval, workspace.game.javaEval);
    mss::assert_(!baseline[0].equivalence && !baseline[1].equivalence && baseline[0].branches.size() == 3 && baseline[1].branches.size() == 5, "test::anytimePruning: 两个活等价格的状态识别或数字分支改变");
    mss::assert_(std::abs(baseline[0].value - 0.780792365410507672L) < 1e-12L && std::abs(baseline[1].value - 0.782020032431476109L) < 1e-12L, "test::anytimePruning: 等价元数据改写了原 JavaEval 评分");
    const mss::JavaEval::Result automatic = mss::JavaEval::analyze(input, engineConfig.game.javaEval, workspace.game.javaEval);
    for (mss::winrate::CellId cell : cells)
        mss::assert_(std::any_of(automatic.begin(), automatic.end(), [&](const mss::MoveEval::Analysis &move) { return move.cellId == cell && move.value >= 0.0L; }), "test::anytimePruning: JavaEval 错误启用了等价类剪枝");
    mss::AnytimeSearch::Result selected = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), autoConfig.anytimeSearch, workspace.anytimeSearch, 0, {});
    mss::assert_(selected.candidates.size() == 1 && selected.candidates.front().cellId == 117 && selected.node == baseline[0].node + baseline[1].node, "test::anytimePruning: Search 根去重失效或重复计算了强制安全");
    mss::AnytimeSearch::Result full = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), fullConfig.anytimeSearch, workspace.anytimeSearch, 0, {});
    mss::assert_(full.candidates.size() == 2 && full.candidates[0].value.front() == baseline[0].value && full.candidates[1].value.front() == baseline[1].value, "test::anytimePruning: 指定等价格被剪掉或复制评分");
    mss::AnytimeSearch::Result whole = mss::AnytimeSearch::analyze(input, fullConfig.anytimeSearch, workspace.anytimeSearch, 256, {}, cells);
    for (int step = 0; step < 256 && !full.finished; ++step) {
        mss::AnytimeSearch::update(full, fullConfig.anytimeSearch, workspace.anytimeSearch, 1, {});
        if (step % 17 == 0) mss::JavaEval::analyze(input, engineConfig.game.javaEval, workspace.game.javaEval, std::span{cells}.first(1));
    }
    mss::assert_(full.candidates.size() == 2 && full.node == whole.node && full.completedDepth == whole.completedDepth && full.finished == whole.finished, "test::anytimePruning: 分段续算或工作区历史改变搜索进度");
    for (int i = 0; i < 2; ++i) {
        mss::assert_(full.candidates[i].cellId == whole.candidates[i].cellId && full.candidates[i].value == whole.candidates[i].value && full.candidates[i].branches.size() == whole.candidates[i].branches.size(), "test::anytimePruning: 分段续算改变候选或逐层评分");
        for (int j = 0; j < (int)full.candidates[i].branches.size(); ++j) mss::assert_(full.candidates[i].branches[j].digit == whole.candidates[i].branches[j].digit && full.candidates[i].branches[j].value == whole.candidates[i].branches[j].value && full.candidates[i].branches[j].depth == whole.candidates[i].branches[j].depth, "test::anytimePruning: 分段续算改变数字分支");
    }
    // A+B=1、B+C=1；A(0) 是 dead，C(4) 是活格，二者同生共死。
    // 6、13 不属于 A/C 的同生共死类，但它们相邻且可以互换。
    const std::array<const char *, 2> mixedRows{"?4?4???", "MMMMMM?"};
    mss::ObservedBoard::Result mixedBoard(2, 7, 8);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 7; ++col) {
            const char cell = mixedRows[row][col];
            if (cell == 'M') mixedBoard.board[row][col] = mss::ObservedBoard::CellState::ForcedMine;
            else if (cell != '?') mixedBoard.board[row][col] = (mss::ObservedBoard::CellState)(cell - '0');
        }
    mss::Workspace mixedWorkspace;
    mss::GameControl::Position mixedPosition(std::move(mixedBoard), mixedWorkspace.game);
    const mss::winrate::Input mixedInput = mixedPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(!mixedPosition.error().failed && mixedInput.probability.candidates() == 4.0L, "test::anytimePruning: 混合等价类盘面不满足四种布局");
    const mss::LongTermRisk::Result mixedRisk = mss::LongTermRisk::analyze(mixedInput, {}, engineConfig.game.javaEval.moveEval.longTermRisk, mixedWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 4> mixedCells{0, 4, 6, 13};
    const mss::JavaEval::Result mixedBaseline = mss::JavaEval::analyze(mixedInput, mixedRisk, mss::MoveEval::Selection::specified(mixedInput, mixedCells, mixedWorkspace.game.probability), engineConfig.game.javaEval, mixedWorkspace.game.javaEval);
    mss::assert_(mixedBaseline[0].branches.size() == 1 && mixedBaseline[1].branches.size() > 1 && !mixedBaseline[0].equivalence && !mixedBaseline[1].equivalence && !mixedBaseline[2].equivalence && !mixedBaseline[3].equivalence, "test::anytimePruning: 混合类或同安全率的非等价格未正确区分");
    mss::AnytimeSearch::Result mixedSelected = mss::AnytimeSearch::analyze(mixedInput, mixedRisk, mss::MoveEval::Selection::specified(mixedInput, mixedCells, mixedWorkspace.game.probability), autoConfig.anytimeSearch, mixedWorkspace.anytimeSearch, 0, {});
    mss::assert_(mixedSelected.candidates.size() == 2 && std::none_of(mixedSelected.candidates.begin(), mixedSelected.candidates.end(), [](const mss::AnytimeSearch::Candidate &move) { return move.cellId == 0; }) && std::any_of(mixedSelected.candidates.begin(), mixedSelected.candidates.end(), [](const mss::AnytimeSearch::Candidate &move) { return move.cellId == 4; }), "test::anytimePruning: dead 抢占类代表或误删不同等价类");
    const mss::JavaEval::Result mixedAutomatic = mss::JavaEval::analyze(mixedInput, engineConfig.game.javaEval, mixedWorkspace.game.javaEval);
    for (const mss::MoveEval::Analysis &move : mixedAutomatic)
        mss::assert_(move.value >= 0.0L && move.branches.size() > 1, "test::anytimePruning: 有活格时 JavaEval 推荐 dead 格");
    // A+B=1、B+C=1，总共两雷；A/C 安全会让远端 T 必雷，T 安全则让 B 必安。
    mss::Workspace globalWorkspace;
    mss::ObservedBoard::Result globalBoard(1, 6, 2);
    globalBoard.board[0][1] = globalBoard.board[0][3] = mss::ObservedBoard::CellState::Num1;
    mss::GameControl::Position globalPosition(std::move(globalBoard), globalWorkspace.game);
    const mss::winrate::Input globalInput = globalPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    mss::assert_(globalInput.probability.candidates() == 2.0L && globalInput.basic.unknownSum == 1 && globalInput.probability.tCellProbability() == 0.5L, "test::anytimePruning: 全局确定性盘面不是两种布局");
    const mss::LongTermRisk::Result globalRisk = mss::LongTermRisk::analyze(globalInput, {}, engineConfig.game.javaEval.moveEval.longTermRisk, globalWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 3> globalCells{0, 4, 5};
    const mss::AnytimeSearch::Result globalSelected = mss::AnytimeSearch::analyze(globalInput, globalRisk, mss::MoveEval::Selection::specified(globalInput, globalCells, globalWorkspace.game.probability), autoConfig.anytimeSearch, globalWorkspace.anytimeSearch, 0, {});
    mss::assert_(globalSelected.candidates.size() == 2 && std::any_of(globalSelected.candidates.begin(), globalSelected.candidates.end(), [](const mss::AnytimeSearch::Candidate &move) { return move.cellId == 5; }), "test::anytimePruning: Search 混淆全局雷数确定的不同等价类");
    mss::assert_(globalInput.basic.unknownSum == 1 && globalPosition.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 2.0L && mss::Basic::checkValid(globalInput.board, globalInput.basic), "test::anytimePruning: 全局确定性分支没有完整回退");
    mss::Workspace deadWorkspace;
    mss::ObservedBoard::Result deadBoard(1, 3, 1);
    deadBoard.board[0][1] = mss::ObservedBoard::CellState::Num1;
    mss::GameControl::Position deadPosition(std::move(deadBoard), deadWorkspace.game);
    const mss::winrate::Input deadInput = deadPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::JavaEval::Result deadAutomatic = mss::JavaEval::analyze(deadInput, engineConfig.game.javaEval, deadWorkspace.game.javaEval);
    mss::assert_(deadAutomatic.size() == 2 && deadAutomatic[0].value >= 0.0L && deadAutomatic[1].value >= 0.0L, "test::anytimePruning: 强制 50/50 的两个 dead 候选不应被普通盲猜回退替换");
    mss::AnytimeSearch::Result deadSearch = mss::AnytimeSearch::analyze(deadInput, autoConfig.anytimeSearch, deadWorkspace.anytimeSearch, 100, {});
    mss::assert_(deadSearch.candidates.size() == 2 && deadSearch.finished, "test::anytimePruning: 全 dead 的 Search 不能完成必猜终局");
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
    std::cout << "test/anytime_pruning: equivalent_roots=2->1, mixed_classes=2, forced_dead_roots=2, G_roots=2, zero_layer_nodes=" << selected.node << '\n';
}

inline void anytimeReplay() {
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(9, 9, 10);
    board.board[0][0] = board.board[0][8] = board.board[8][0] = board.board[8][8] = mss::ObservedBoard::CellState::Num1;
    board.board[4][4] = mss::ObservedBoard::CellState::ForcedSafe;
    mss::Basic::Result basic = mss::Basic::analyze(board, workspace.game.basic);
    mss::Structure::Pool shapes;
    mss::Structure::Result structure = mss::Structure::analyze(board, basic, shapes, workspace.game.structure);
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
        mss::ObservedBoard::applyDelta(board, boards[i].changes);
        mss::Basic::update(basic, basics[i], board, boards[i], workspace.game.basic);
        mss::Structure::update(structure, structures[i], board, basic, shapes, std::span{&cells[i], 1}, workspace.game.structure);
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
            mss::Structure::applyDelta(structure, shapes, structures[i].removed, structures[i].added, true);
            mss::Basic::applyDelta(basic, basics[i].changes, true);
            mss::ObservedBoard::applyDelta(board, boards[i].changes, true);
            check(i);
        }
        for (int i = 0; i < 2; ++i) {
            mss::ObservedBoard::applyDelta(board, boards[i].changes);
            mss::Basic::applyDelta(basic, basics[i].changes);
            mss::Structure::applyDelta(structure, shapes, structures[i].removed, structures[i].added);
            check(i + 1);
        }
    }
    std::cout << "test/anytime_replay: steps=2 repeats=3 forced_safe=true\n";
}

inline void anytimeWeights() {
    mss::Config cfg = engineConfig; cfg.anytimeSearch.fullAnalysis = true;
    const std::string cells = "011101???101?22???????????????01?22344310113???3????????????0123??3?100002?32?????????????001?4?322100022223????????????00112122?21112?22?????????????1100001?3?11?22??2?????????????11221223234321???????????????112??11?12???223??????????????123?422223?422?????????????????3?33?12?311023?????????????????3??323?31112?????????????????????3?32??1??2?????????????????????????322???????????????????????????????????????????????????????????????????????????????????????????????????????????";
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    mss::assert_(cells.size() == 480, "test::anytimeWeights: 复现盘面长度错误");
    for (int cell = 0; cell < 480; ++cell) if (cells[cell] != '?') board.board[cell / 30][cell % 30] = (mss::ObservedBoard::CellState)(cells[cell] - '0');
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, cfg.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
    const mss::winrate::CellId root = 15;
    const mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, risk, mss::MoveEval::Selection::specified(input, std::span{&root, 1}, workspace.game.probability), cfg.game.javaEval, workspace.game.javaEval);
    long double weighted = 0.0L, raw = 0.0L;
    for (const mss::MoveEval::Branch &branch : evaluated.front().branches) {
        weighted += branch.probability * evaluated.front().multiplier * branch.value;
        raw += branch.probability * branch.value;
    }
    mss::assert_(std::abs(weighted - evaluated.front().value) < 1e-12L && std::abs(weighted - raw) > 0.01L, "test::anytimeWeights: 根权重未还原 JavaEval 或未覆盖显著风险修正");
    mss::AnytimeSearch::Result result = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, std::span{&root, 1}, workspace.game.probability), cfg.anytimeSearch, workspace.anytimeSearch, 0, {});
    long double expected = 0.0L;
    for (int depth = 1; depth <= 2; ++depth) {
        while ((int)result.candidates.front().value.size() <= depth && result.node < 10000) mss::AnytimeSearch::update(result, cfg.anytimeSearch, workspace.anytimeSearch, 1, {});
        mss::assert_((int)result.candidates.front().value.size() == depth + 1, "test::anytimeWeights: 续搜层未在预算内发布");
        mss::assert_(result.candidates.front().multiplier == evaluated.front().multiplier, "test::anytimeWeights: 续搜覆写根乘数");
        expected = 0.0L;
        for (const mss::AnytimeSearch::Candidate::Branch &branch : result.candidates.front().branches) {
            mss::assert_(branch.depth == depth, "test::anytimeWeights: 混用了不同深度的子评分");
            const mss::MoveEval::Branch &original = *std::find_if(evaluated.front().branches.begin(), evaluated.front().branches.end(), [&](const mss::MoveEval::Branch &candidate) { return candidate.digit == branch.digit; });
            expected += original.probability * result.candidates.front().multiplier * branch.value;
        }
        mss::assert_(std::abs(result.candidates.front().value.back() - expected) < 1e-12L, "test::anytimeWeights: 续搜丢失根风险乘数");
    }
    std::cout << "test/anytime_weights: board=16x30/99 cell=" << root << " initial_correction=" << weighted - raw << " nodes=" << result.node << " score=" << expected << '\n';
    // 六种雷位的完整搜索均应为 1/2，不能保留初始风险和进度奖励。
    mss::Workspace completeWorkspace;
    mss::GameControl::Position completePosition(mss::ObservedBoard::Result(2, 3, 1), completeWorkspace.game);
    const mss::winrate::Input completeInput = completePosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result completeRisk = mss::LongTermRisk::analyze(completeInput, {}, cfg.game.javaEval.moveEval.longTermRisk, completeWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 6> roots{0, 1, 2, 3, 4, 5};
    const mss::AnytimeSearch::Result complete = mss::AnytimeSearch::analyze(completeInput, completeRisk, mss::MoveEval::Selection::specified(completeInput, roots, completeWorkspace.game.probability), cfg.anytimeSearch, completeWorkspace.anytimeSearch, 10000, {});
    mss::assert_(complete.finished, "test::anytimeWeights: 小盘完整搜索未结束");
    for (const mss::AnytimeSearch::Candidate &candidate : complete.candidates) {
        mss::assert_(std::abs(candidate.value.back() - 0.5L) < 1e-12L, "test::anytimeWeights: 已算完的结局仍被启发式放大");
        mss::assert_(candidate.multiplier == 1.0L, "test::anytimeWeights: 已算完招法的修正乘数未归一");
    }
    std::cout << "test/anytime_weights_complete: board=2x3/1 roots=6 nodes=" << complete.node << " score=0.5\n";
}

inline void candidateSelection() {
    const std::string cells = "1??2223?11?12?21???3??????????123?2??422123?213434??????????112124?4?212?3323?12??????????2?1113?312?22?2??2124??????3???321?321133223432102???32212?22?124?201??12??10002?5310012212211??2013334?5200023?21012?10?201221001?2???21001?5?202?310?21110111112233?10012??202?310221?112?1111001110012433133?21?22122?222?111100002?3?3?2?22?2?101?321?211?221102?32?221111111012?1111012?4?4221112210122000012210011112????10001?211??00112?10001?11234?3112222?1122001?2110001111?11?101??1111000";
    mss::Workspace workspace;
    mss::ObservedBoard::Result board(16, 30, 99);
    for (int cell = 0; cell < 480; ++cell) if (cells[cell] != '?') board.board[cell / 30][cell % 30] = (mss::ObservedBoard::CellState)(cells[cell] - '0');
    mss::GameControl::Position position(std::move(board), workspace.game);
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, engineConfig.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
    mss::MoveEval::Selection::Result selected = mss::MoveEval::Selection::find(input, &risk, engineConfig.game.javaEval.selection, workspace.game.javaEval.selection);
    mss::MoveEval::Selection::removeDead(selected, input, workspace.game.probability);
    mss::assert_(selected.reason == mss::MoveEval::Selection::Reason::ForcedFiftyFifty && selected.cells.size() == 2 && selected.cells[0] == 437 && selected.cells[1] == 467, "test::candidateSelection: 强制 50/50 被普通 dead 回退替换");
    std::vector<mss::winrate::CellId> order = selected.cells;
    for (int reverse = 0; reverse < 2; ++reverse) {
        for (int size = 0; size <= (int)order.size(); ++size) {
            const mss::MoveEval::Selection::Result slice = mss::MoveEval::Selection::specified(input, std::span{order}.first(size), workspace.game.probability);
            mss::assert_((int)slice.cells.size() == size && slice.observations.size() == slice.cells.size() && std::equal(slice.cells.begin(), slice.cells.end(), order.begin()), "test::candidateSelection: 切片长度或顺序错误");
            for (int i = 0; i < size; ++i) mss::assert_(slice.observations[i].probability == position.observe(order[i], mss::ShapeSolver::OrderAlgo::Auto).probability, "test::candidateSelection: 切片观测错位");
        }
        std::reverse(order.begin(), order.end());
    }
    mss::MoveEval::Selection::Result repeated = mss::MoveEval::Selection::find(input, &risk, engineConfig.game.javaEval.selection, workspace.game.javaEval.selection);
    mss::MoveEval::Selection::removeDead(repeated, input, workspace.game.probability);
    mss::assert_(selected.cells == repeated.cells && selected.reason == repeated.reason && selected.observations.size() == repeated.observations.size(), "test::candidateSelection: 复用工作区改变候选");
    for (int i = 0; i < (int)selected.cells.size(); ++i) mss::assert_(selected.observations[i].probability == repeated.observations[i].probability, "test::candidateSelection: 复用工作区改变观测");
    const mss::JavaEval::Result evaluated = mss::JavaEval::analyze(input, risk, selected, engineConfig.game.javaEval, workspace.game.javaEval);
    mss::assert_(evaluated.size() == 2 && evaluated[0].cellId == 437 && evaluated[1].cellId == 467 && evaluated[0].value > 0.0L && evaluated[1].value > 0.0L && !evaluated[0].equivalence && !evaluated[1].equivalence, "test::candidateSelection: JavaEval 改写了候选或执行了搜索剪枝");
    mss::UI::Session session(engineConfig);
    const std::string response = session.handle("/api/analyze", "rows=16&cols=30&mines=99&cells=" + cells + "&algo=auto&suggest=true&strategy=java&limit=1000&maxThreads=1");
    mss::assert_(response.find("\"reason\":\"forced5050\"") != std::string::npos && response.find("\"candidates\":[437,467]") != std::string::npos && response.find("\"recommended\":[437]") != std::string::npos, "test::candidateSelection: UI 理由或候选与引擎不一致");
    mss::Workspace blindWorkspace;
    mss::ObservedBoard::Result blindBoard(3, 3, 6);
    for (int cell : {1, 3, 5, 6, 7}) blindBoard.board[cell / 3][cell % 3] = mss::ObservedBoard::CellState::ForcedMine;
    blindBoard.board[1][1] = mss::ObservedBoard::CellState::Num6;
    mss::GameControl::Position blindPosition(std::move(blindBoard), blindWorkspace.game);
    const mss::winrate::Input blindInput = blindPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result blindRisk = mss::LongTermRisk::analyze(blindInput, {}, engineConfig.game.javaEval.moveEval.longTermRisk, blindWorkspace.game.javaEval.moveEval.longTermRisk);
    mss::MoveEval::Selection::Result blind = mss::MoveEval::Selection::find(blindInput, &blindRisk, engineConfig.game.javaEval.selection, blindWorkspace.game.javaEval.selection);
    mss::MoveEval::Selection::removeDead(blind, blindInput, blindWorkspace.game.probability);
    mss::assert_(blind.reason == mss::MoveEval::Selection::Reason::BlindGuess && blind.cells.size() == 1 && blindInput.probability.mineProbability(blind.cells.front(), blindInput.board, blindInput.basic, blindInput.structure) != 1.0L, "test::candidateSelection: 普通候选全 dead 时没有直接进入单格盲猜");
    const std::string weakResponse = session.handle("/api/analyze", "rows=2&cols=2&mines=1&cells=1???&algo=auto&suggest=true&strategy=java&limit=1000&maxThreads=1");
    mss::assert_(weakResponse.find("\"reason\":\"5050OrSafe\"") != std::string::npos, "test::candidateSelection: 弱 50/50 原因丢失");
    const std::string blindResponse = session.handle("/api/analyze", "rows=3&cols=3&mines=6&cells=?M?M6MMM?&algo=auto&suggest=true&strategy=java&limit=1000&maxThreads=1");
    mss::assert_(blindResponse.find("\"reason\":\"blindGuess\"") != std::string::npos && blindResponse.find("\"candidates\":[0]") != std::string::npos, "test::candidateSelection: 盲猜原因或单格集合丢失");
    // 由审查失败盘面 ??S? 派生转置与镜像；C(3,2)=3，任何结构所需的第三格雷都与 S 冲突。
    for (int vertical = 0; vertical < 2; ++vertical) for (int safe : {1, 2}) {
        mss::ObservedBoard::Result constrained(vertical ? 4 : 1, vertical ? 1 : 4, 2);
        constrained.board[vertical ? safe : 0][vertical ? 0 : safe] = mss::ObservedBoard::CellState::ForcedSafe;
        mss::GameControl::Position constrainedPosition(std::move(constrained), workspace.game);
        const mss::winrate::Input constrainedInput = constrainedPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
        const mss::LongTermRisk::Result constrainedRisk = mss::LongTermRisk::analyze(constrainedInput, {}, engineConfig.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
        mss::assert_(constrainedInput.probability.candidates() == 3.0L && constrainedRisk.pseudos.empty(), "test::candidateSelection: S 约束或布局数丢失");
        for (int cell = 0; cell < 4; ++cell) if (cell != safe) {
            const auto [row, col] = constrainedInput.board.pos(cell);
            mss::assert_(constrainedRisk.influences[row][col] == 0.0L && mss::LongTermRisk::influence(cell, constrainedInput, constrainedRisk, engineConfig.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk) == 0.0L, "test::candidateSelection: 与 S 冲突的几何假设被计为合法布局");
        }
        mss::assert_(mss::Basic::checkValid(constrainedInput.board, constrainedInput.basic) && constrainedInput.board.board[vertical ? safe : 0][vertical ? 0 : safe] == mss::ObservedBoard::CellState::ForcedSafe, "test::candidateSelection: 风险查询未恢复盘面");
    }
    mss::GameControl::Workspace fresh;
    const mss::JavaEval::Result replay = mss::JavaEval::analyze(input, risk, selected, engineConfig.game.javaEval, workspace.game.javaEval);
    const mss::JavaEval::Result rebuilt = mss::JavaEval::analyze(input, engineConfig.game.javaEval, fresh.javaEval, selected.cells);
    mss::assert_(replay.size() == 2 && rebuilt.size() == 2, "test::candidateSelection: 切换盘面或工作区丢失候选");
    for (int i = 0; i < 2; ++i) mss::assert_(replay[i].cellId == evaluated[i].cellId && rebuilt[i].cellId == evaluated[i].cellId && replay[i].value == evaluated[i].value && rebuilt[i].value == evaluated[i].value && replay[i].node == evaluated[i].node && rebuilt[i].node == evaluated[i].node, "test::candidateSelection: 风险缓存或工作区历史改变评分");
    std::cout << "test/candidate_selection: forced=(15,18),(16,18) reason=forced5050 scored=2\n";
}

inline void anytimeSearch() {
    candidateSelection();
    anytimeWeights();
    anytimeReplay();
    anytimeEndgame();
    anytimeEquivalent();
    anytimeFiftyFifty();
    anytimeBeginner();
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    mss::Config cfg = engineConfig; cfg.anytimeSearch.width = {1, 2, 1}; cfg.anytimeSearch.fullAnalysis = true; cfg.game.bruteForce = mss::BruteForce::Config{false, mss::BruteForce::Config::Solver::Bitwise, 1000, 1};
    // 中心数字约束八格中恰有两雷；无安全格，八个根招法均应保留。
    mss::ObservedBoard::Result board(3, 3, 2);
    board.board[1][1] = mss::ObservedBoard::CellState::Num2;
    mss::Workspace workspace;
    mss::GameControl::Position position(std::move(board), workspace.game);
    mss::assert_(!position.error().failed && position.safeMoves(mss::ShapeSolver::OrderAlgo::Auto).empty(), "test::anytimeSearch: 固定盘面不是有效猜测局面");
    const mss::winrate::Input input = position.input(mss::ShapeSolver::OrderAlgo::Auto);
    std::vector<mss::winrate::CellId> cells{0, 1, 2, 3, 5, 6, 7, 8};
    const mss::LongTermRisk::Result risk = mss::LongTermRisk::analyze(input, {}, cfg.game.javaEval.moveEval.longTermRisk, workspace.game.javaEval.moveEval.longTermRisk);
    const mss::JavaEval::Result baseline = mss::JavaEval::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), cfg.game.javaEval, workspace.game.javaEval);
    mss::AnytimeSearch::Result split = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), cfg.anytimeSearch, workspace.anytimeSearch, 0, {});
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
    mss::AnytimeSearch::update(split, cfg.anytimeSearch, workspace.anytimeSearch, 10, stopped.get_token());
    mss::assert_(split.node == initialNodes && split.completedDepth == 0, "test::anytimeSearch: 停止请求未阻止后续搜索");
    mss::Workspace resumedWorkspace;
    bool partial = false;
    int updates = 0;
    while (!split.finished && updates < 10000) {
        const long long before = split.node;
        mss::AnytimeSearch::update(split, cfg.anytimeSearch, updates % 2 == 0 ? workspace.anytimeSearch : resumedWorkspace.anytimeSearch, 1, {});
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
        if (updates == 7) mss::JavaEval::analyze(input, risk, mss::MoveEval::Selection::specified(input, std::span{cells.data(), 1}, workspace.game.probability), cfg.game.javaEval, workspace.game.javaEval);
        ++updates;
    }
    mss::assert_(updates == split.node - initialNodes, "test::anytimeSearch: 最后一个概率节点算完后还需额外空步才发布结果");
    mss::assert_(split.finished && partial, "test::anytimeSearch: 未自然结束或没有发布部分层的完整候选");
    mss::AnytimeSearch::Result batch = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), cfg.anytimeSearch, workspace.anytimeSearch, 100000, {});
    mss::assert_(batch.finished && batch.node == split.node && batch.completedDepth == split.completedDepth && batch.recommended() == split.recommended(), "test::anytimeSearch: 单节点续算与整批搜索结果不同");
    for (int i = 0; i < (int)cells.size(); ++i) {
        mss::assert_(batch.candidates[i].value == split.candidates[i].value, "test::anytimeSearch: 预算切分改变了逐层评分");
        for (int branch = 0; branch < (int)batch.candidates[i].branches.size(); ++branch)
            mss::assert_(batch.candidates[i].branches[branch].value == split.candidates[i].branches[branch].value && batch.candidates[i].branches[branch].depth == split.candidates[i].branches[branch].depth, "test::anytimeSearch: 单节点续算改变了数字分支的子评分");
    }
    mss::assert_(mss::Basic::checkValid(input.board, input.basic) && input.board.board[1][1] == mss::ObservedBoard::CellState::Num2 && position.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 28.0L, "test::anytimeSearch: 搜索修改了调用方的根输入");
    mss::Config automatic = cfg; automatic.anytimeSearch.width = {2, 2, 1}; automatic.anytimeSearch.fullAnalysis = false;
    mss::AnytimeSearch::Result selected = mss::AnytimeSearch::analyze(input, risk, mss::MoveEval::Selection::specified(input, cells, workspace.game.probability), automatic.anytimeSearch, workspace.anytimeSearch, 0, {});
    mss::assert_(selected.candidates.size() == 2, "test::anytimeSearch: 自动根宽度未生效");
    mss::Workspace terminalWorkspace;
    mss::GameControl::Position terminalPosition(mss::ObservedBoard::Result(1, 1, 0), terminalWorkspace.game);
    const mss::winrate::Input terminalInput = terminalPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result terminalRisk = mss::LongTermRisk::analyze(terminalInput, {}, cfg.game.javaEval.moveEval.longTermRisk, terminalWorkspace.game.javaEval.moveEval.longTermRisk);
    const mss::winrate::CellId lastSafe = 0;
    const mss::JavaEval::Result terminal = mss::JavaEval::analyze(terminalInput, terminalRisk, mss::MoveEval::Selection::specified(terminalInput, std::span{&lastSafe, 1}, terminalWorkspace.game.probability), cfg.game.javaEval, terminalWorkspace.game.javaEval);
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
    mss::GameControl::Position dominancePosition(std::move(dominanceBoard), dominanceWorkspace.game);
    mss::assert_(!dominancePosition.error().failed && dominancePosition.probability(mss::ShapeSolver::OrderAlgo::Auto).candidates() == 27.0L, "test::anytimeSearch: 支配格固定盘面不满足 27 种布局");
    const mss::winrate::Input dominanceInput = dominancePosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result dominanceRisk = mss::LongTermRisk::analyze(dominanceInput, {}, cfg.game.javaEval.moveEval.longTermRisk, dominanceWorkspace.game.javaEval.moveEval.longTermRisk);
    const std::array<mss::winrate::CellId, 2> dominanceCells{0, 12};
    const mss::JavaEval::Result full = mss::JavaEval::analyze(dominanceInput, dominanceRisk, mss::MoveEval::Selection::specified(dominanceInput, dominanceCells, dominanceWorkspace.game.probability), cfg.game.javaEval, dominanceWorkspace.game.javaEval);
    mss::assert_(full.size() == 2 && full[0].value >= 0.0L && full[1].value >= 0.0L, "test::anytimeSearch: JavaEval 不应执行对换格剪枝");
    const mss::AnytimeSearch::Result prunedDominance = mss::AnytimeSearch::analyze(dominanceInput, dominanceRisk, mss::MoveEval::Selection::specified(dominanceInput, dominanceCells, dominanceWorkspace.game.probability), automatic.anytimeSearch, dominanceWorkspace.anytimeSearch, 0, {});
    mss::assert_(prunedDominance.candidates.size() == 1 && prunedDominance.candidates.front().cellId == 12, "test::anytimeSearch: 自动 Search 未执行已证明的对换格剪枝");
    mss::AnytimeSearch::Result fullDominance = mss::AnytimeSearch::analyze(dominanceInput, dominanceRisk, mss::MoveEval::Selection::specified(dominanceInput, dominanceCells, dominanceWorkspace.game.probability), cfg.anytimeSearch, dominanceWorkspace.anytimeSearch, 0, {});
    mss::assert_(fullDominance.candidates.size() == 2, "test::anytimeSearch: 完整根分析杀掉了指定的支配招法");
    mss::Workspace cancelWorkspace;
    mss::GameControl::Position cancelPosition(mss::ObservedBoard::Result(5, 5, 5), cancelWorkspace.game);
    const mss::winrate::Input cancelInput = cancelPosition.input(mss::ShapeSolver::OrderAlgo::Auto);
    const mss::LongTermRisk::Result cancelRisk = mss::LongTermRisk::analyze(cancelInput, {}, cfg.game.javaEval.moveEval.longTermRisk, cancelWorkspace.game.javaEval.moveEval.longTermRisk);
    const mss::winrate::CellId corner = 0;
    mss::AnytimeSearch::Result cancelled = mss::AnytimeSearch::analyze(cancelInput, cancelRisk, mss::MoveEval::Selection::specified(cancelInput, std::span{&corner, 1}, cancelWorkspace.game.probability), cfg.anytimeSearch, cancelWorkspace.anytimeSearch, 0, {});
    std::stop_source cancel;
    std::jthread requestStop([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); cancel.request_stop(); });
    mss::AnytimeSearch::update(cancelled, cfg.anytimeSearch, cancelWorkspace.anytimeSearch, 10000000, cancel.get_token());
    requestStop.join();
    mss::assert_(cancel.stop_requested() && !cancelled.finished, "test::anytimeSearch: 运行中的停止请求未中断搜索");
    const long long cancelledNodes = cancelled.node;
    mss::AnytimeSearch::update(cancelled, cfg.anytimeSearch, cancelWorkspace.anytimeSearch, 1, {});
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
    anytimeSwapPruning();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "test/anytime_search: board=3x3/2, layouts=28, roots=" << split.candidates.size() << ", nodes=" << split.node << ", depth=" << split.completedDepth << ", updates=" << updates << ", partial=" << partial << ", seconds=" << seconds << '\n';
}

} // 命名空间 test
