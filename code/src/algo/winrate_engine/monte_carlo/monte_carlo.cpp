#include "monte_carlo.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

#include "core/assert.h"
#include "core/config.h"
#include "core/workspace.h"
#include "core/utility/rng.h"
#include "game/game.h"

namespace mss {

MonteCarlo::Candidate::Candidate(winrate::CellId cellId, long double weight, long double prior, const Config &cfg) : cellId(cellId), samples(0), wins(0), weight(weight), prior(prior), mean((1.0L + cfg.priorWeight * prior) / (2.0L + cfg.priorWeight)), variance(mean * (1.0L - mean) / (3.0L + cfg.priorWeight)) {}

MonteCarlo::Result::Result(winrate::SuggestAlgo strategy, std::vector<winrate::CellId> safeCells, bool winrate, std::vector<Candidate> candidates, int initialRecommended, bool fullScores, int cellCount) : winrate(winrate), candidates(std::move(candidates)), strategy(strategy), safeCells(std::move(safeCells)), random(0xC0FFEE12345ULL, 0xD1B54A32D192ED03ULL), winrateSamples(0), winrateWins(0), initialRecommended(initialRecommended), fullScores(fullScores), nextMovesOf(winrate ? 0 : cellCount * 9, -1) {}

long long MonteCarlo::Result::samples() const {
    if (winrate)
        return winrateSamples;
    long long result = 0;
    for (const Candidate &candidate : candidates)
        result += candidate.samples;
    return result;
}

long long MonteCarlo::Result::wins() const {
    if (winrate)
        return winrateWins;
    long long result = 0;
    for (const Candidate &candidate : candidates)
        result += candidate.wins;
    return result;
}

int MonteCarlo::Result::best() const {
    int result = initialRecommended;
    for (int i = 0; i < (int)candidates.size(); ++i)
        if (candidates[i].samples != 0 && (candidates[result].samples == 0 || candidates[i].mean > candidates[result].mean))
            result = i;
    return result;
}

winrate::CellId MonteCarlo::Result::recommended() const {
    return winrate ? -1 : candidates[best()].cellId;
}

MonteCarlo::Workspace::Workspace(Probability::Workspace &probability, LongTermRisk::Workspace &risk, JavaEval::Workspace &javaEval, mss::Workspace &rollout) : probability(probability), risk(risk), javaEval(javaEval), rollout(rollout) {
    firstMove.reserve(1);
}

std::vector<winrate::CellId> MonteCarlo::collectSafe(const winrate::View &root) {
    std::vector<winrate::CellId> result;
    for (int row = 0; row < root.board.board.rows(); ++row)
        for (int col = 0; col < root.board.board.cols(); ++col) {
            const winrate::CellState state = root.board.board[row][col];
            if (state != winrate::CellState::Hidden && state != winrate::CellState::ForcedSafe)
                continue;
            const winrate::CellId cell = root.board.id(row, col);
            if (root.probability.mineProbability(cell, root.board, root.basic, root.structure) == 0.0L)
                result.push_back(cell);
        }
    return result;
}

int MonteCarlo::remainingSafe(const winrate::Board &board) {
    int remaining = board.board.rows() * board.board.cols() - board.totalMines;
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col)
            remaining -= board.board[row][col] <= winrate::CellState::Num8;
    return remaining;
}

JavaEval::Result MonteCarlo::evaluate(const winrate::Input &root, const mss::Config &cfg, Workspace &workspace) {
    const LongTermRisk::Result risk = LongTermRisk::analyze(root, {}, cfg.longTermRisk, workspace.risk);
    JavaEval::Result result = JavaEval::analyze(root, risk, MoveEval::Selection::prepare(root, &risk, cfg.javaEval, cfg.pseudo, workspace.probability, workspace.rollout.pseudo, workspace.javaEval.selection), cfg.moveEval, cfg.longTermRisk, workspace.javaEval);
    std::stable_sort(result.begin(), result.end(), [](const JavaEval::Analysis &lhs, const JavaEval::Analysis &rhs) { return lhs.value > rhs.value; });
    return result;
}

void MonteCarlo::estimate(Candidate &candidate, const Config &cfg) {
    const long double count = candidate.samples + 2.0L + cfg.priorWeight;
    candidate.mean = (candidate.wins + 1.0L + cfg.priorWeight * candidate.prior) / count;
    candidate.variance = candidate.mean * (1.0L - candidate.mean) / (count + 1.0L);
}

MonteCarlo::Result MonteCarlo::analyze(const winrate::Input &root, winrate::SuggestAlgo strategy, const mss::Config &cfg, Workspace &workspace, winrate::Cells targets) {
    const int cellCount = root.board.board.rows() * root.board.board.cols();
    std::vector<winrate::CellId> safeCells = collectSafe(root);
    if (!safeCells.empty() || remainingSafe(root.board) == 0)
        return Result(strategy, std::move(safeCells), true, {}, -1, true, cellCount);
    std::vector<Candidate> candidates;
    if (targets.empty()) {
        const JavaEval::Result evaluated = evaluate(root, cfg, workspace);
        // 合法非终局且没有必安格时，Java 自动候选必须包含推荐项。
        assert_(!evaluated.empty(), "MonteCarlo::analyze: 非终局没有 Java 候选");
        candidates.push_back(Candidate(evaluated.front().cellId, evaluated.front().value, 1.0L, cfg.monteCarlo));
        return Result(strategy, {}, false, std::move(candidates), 0, false, cellCount);
    }
    const LongTermRisk::Result risk = LongTermRisk::analyze(root, {}, cfg.longTermRisk, workspace.risk);
    const JavaEval::Result evaluated = JavaEval::analyze(root, risk, MoveEval::Selection::specified(root, targets, workspace.probability, workspace.javaEval.selection), cfg.moveEval, cfg.longTermRisk, workspace.javaEval);
    long double maximum = evaluated.front().value;
    int recommended = 0;
    for (int i = 0; i < (int)evaluated.size(); ++i)
        if (evaluated[i].value > maximum) {
            maximum = evaluated[i].value;
            recommended = i;
        }
    long double total = 0.0L;
    for (const JavaEval::Analysis &candidate : evaluated)
        total += std::exp((candidate.value - maximum) / cfg.monteCarlo.temperature);
    candidates.reserve(evaluated.size());
    for (const JavaEval::Analysis &candidate : evaluated)
        candidates.push_back(Candidate(candidate.cellId, candidate.value, std::exp((candidate.value - maximum) / cfg.monteCarlo.temperature) / total, cfg.monteCarlo));
    return Result(strategy, {}, false, std::move(candidates), recommended, true, cellCount);
}

void MonteCarlo::expand(Result &result, const winrate::Input &root, const mss::Config &cfg, Workspace &workspace) {
    const JavaEval::Result evaluated = evaluate(root, cfg, workspace);
    // 首个推荐已完成一局；补齐评分只调整先验，保留同一 Candidate 中的实际胜负和雷盘序号。
    Candidate &first = result.candidates.front();
    for (const JavaEval::Analysis &candidate : evaluated)
        if (candidate.cellId == first.cellId)
            first.weight = candidate.value;
    long double maximum = first.weight;
    for (const JavaEval::Analysis &candidate : evaluated)
        maximum = (std::max)(maximum, candidate.value);
    long double total = std::exp((first.weight - maximum) / cfg.monteCarlo.temperature);
    for (const JavaEval::Analysis &candidate : evaluated)
        if (candidate.cellId != first.cellId)
            total += std::exp((candidate.value - maximum) / cfg.monteCarlo.temperature);
    first.prior = std::exp((first.weight - maximum) / cfg.monteCarlo.temperature) / total;
    estimate(first, cfg.monteCarlo);
    const winrate::CellId firstCell = first.cellId;
    for (const JavaEval::Analysis &candidate : evaluated) {
        int index;
        if (candidate.cellId == firstCell) {
            index = 0;
        } else {
            index = result.candidates.size();
            result.candidates.push_back(Candidate(candidate.cellId, candidate.value, std::exp((candidate.value - maximum) / cfg.monteCarlo.temperature) / total, cfg.monteCarlo));
        }
        if (candidate.cellId == evaluated.front().cellId)
            result.initialRecommended = index;
    }
    result.fullScores = true;
}

// 沿用归档的 Beta 尾概率连分式；后验只用于竞争调度，不将共享雷盘下的各候选当成独立事件相乘。
long double MonteCarlo::logChance(long double won, long double lost, long double target) {
    if (won == lost && target == 0.5L)
        return -std::log(2.0L);
    const bool direct = 1.0L - target < (lost + 1.0L) / (won + lost + 2.0L);
    const long double a = direct ? lost : won, b = direct ? won : lost, x = direct ? 1.0L - target : target;
    long double c = 1.0L, d = 1.0L / (1.0L - (a + b) * x / (a + 1.0L)), fraction = d;
    for (int m = 1; m <= 4096; ++m) {
        const long double even = m * (b - m) * x / ((a + 2.0L * m - 1.0L) * (a + 2.0L * m));
        d = 1.0L / (1.0L + even * d);
        c = 1.0L + even / c;
        fraction *= d * c;
        const long double odd = -(a + m) * (a + b + m) * x / ((a + 2.0L * m) * (a + 2.0L * m + 1.0L));
        d = 1.0L / (1.0L + odd * d);
        c = 1.0L + odd / c;
        const long double change = d * c;
        fraction *= change;
        if (std::abs(change - 1.0L) <= 8.0L * std::numeric_limits<long double>::epsilon()) {
            const long double tail = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) + b * std::log1p(-x) + std::log(fraction / a);
            return direct ? tail : std::log1p(-std::exp(tail));
        }
    }
    assert_(false, "MonteCarlo::logChance: 连分式未收敛");
    std::exit(1);
}

int MonteCarlo::select(Result &result, const Config &cfg) {
    if (!result.fullScores || result.candidates.size() == 1)
        return result.initialRecommended;
    const int reference = result.best();
    // best 优先返回已有样本的候选；仍为零说明所有首招尚未采样，首次保持 Java 原推荐。
    if (result.candidates[reference].samples == 0)
        return result.initialRecommended;
    int selected = -1;
    long double maximum = -std::numeric_limits<long double>::infinity();
    long double pressure = maximum;
    const Candidate &leader = result.candidates[reference];
    const long double leaderReduction = std::log(leader.variance / (leader.samples + 3.0L + cfg.priorWeight));
    // 超越机会给出竞争份额；双方下一样本的预计方差下降决定份额分配，领先者汇总竞争压力。
    for (int i = 0; i < (int)result.candidates.size(); ++i) {
        if (i == reference)
            continue;
        Candidate &candidate = result.candidates[i];
        const long double won = candidate.wins + 1.0L + cfg.priorWeight * candidate.prior;
        const long double lost = candidate.samples - candidate.wins + 1.0L + cfg.priorWeight * (1.0L - candidate.prior);
        if (!candidate.chance || candidate.chance->samples != candidate.samples || candidate.chance->target != leader.mean)
            candidate.chance.emplace(candidate.samples, leader.mean, logChance(won, lost, leader.mean));
        const long double tail = candidate.chance->logarithm;
        const long double reduction = std::log(candidate.variance / (candidate.samples + 3.0L + cfg.priorWeight));
        const long double sumMaximum = (std::max)(leaderReduction, reduction);
        const long double totalReduction = sumMaximum + std::log1p(std::exp((std::min)(leaderReduction, reduction) - sumMaximum));
        const long double leaderPressure = tail + leaderReduction - totalReduction;
        const long double pressureMaximum = (std::max)(pressure, leaderPressure);
        pressure = pressureMaximum + std::log1p(std::exp((std::min)(pressure, leaderPressure) - pressureMaximum));
        // Random::nextUnit() 返回 [0, 1)，逆变换生成与原调度相同的单位指数分布。
        const long double value = tail + reduction - totalReduction - std::log(-std::log1p(-result.random.nextUnit()));
        if (value > maximum) {
            maximum = value;
            selected = i;
        }
    }
    if (pressure - std::log(-std::log1p(-result.random.nextUnit())) > maximum)
        selected = reference;
    return selected;
}

MonteCarlo::Outcome MonteCarlo::simulate(Result &result, const winrate::View &root, long long sequence, winrate::CellId first, const mss::Config &cfg, Workspace &workspace, std::stop_token stop) {
    RawGrid<char> mines = Probability::sampleMineBoard(root.board, root.basic, root.structure, root.shapes, result.assignments, workspace.probability, {splitmix64(0x123456789ABCDEFULL + sequence), splitmix64(0xF123456789ABCDE0ULL + sequence)});
    winrate::Board observed = root.board;
    // FS 属于整局采样；只在模拟副本中恢复成 Hidden 后立即揭开，抽样仍使用根约束，FM 保留。
    for (winrate::CellId cell : result.safeCells) {
        const auto [row, col] = observed.pos(cell);
        if (observed.board[row][col] == winrate::CellState::ForcedSafe)
            observed.board[row][col] = winrate::CellState::Hidden;
    }
    // 雷盘和根评分均已计算完成，Game 只持有自己的 Position 和池；主工作区此时可串行复用。
    GameControl::Game game(GameControl::MineBoard(std::move(mines)), std::move(observed), workspace.rollout, result.suggestions);
    if (stop.stop_requested())
        return Outcome::Cancelled;
    if (game.state() == GameControl::Game::State::Running) {
        if (result.winrate) {
            game.play(result.safeCells);
        } else {
            workspace.firstMove.clear();
            workspace.firstMove.push_back(first);
            game.play(workspace.firstMove);
        }
    }
    if (!result.winrate && game.state() == GameControl::Game::State::Running) {
        if (stop.stop_requested())
            return Outcome::Cancelled;
        const auto [row, col] = root.board.pos(first);
        const int number = (int)game.position().observedBoard().board[row][col];
        int &cached = result.nextMovesOf[first * 9 + number];
        if (cached == -1) {
            cached = result.nextMoves.size();
            result.nextMoves.push_back(game.suggest(ShapeSolver::OrderAlgo::Auto, result.strategy, cfg).suggest);
        }
        // 建议只依赖公开观测，与本次雷盘无关；交给 Game 执行，后续所有建议仍由 Game 计算。
        game.play(result.nextMoves[cached]);
    }
    while (game.state() == GameControl::Game::State::Running) {
        if (stop.stop_requested())
            return Outcome::Cancelled;
        game.play(game.suggest(ShapeSolver::OrderAlgo::Auto, result.strategy, cfg).suggest);
    }
    return game.state() == GameControl::Game::State::Won ? Outcome::Won : Outcome::Lost;
}

bool MonteCarlo::update(Result &result, const winrate::Input &root, const mss::Config &cfg, Workspace &workspace, std::stop_token stop) {
    if (stop.stop_requested())
        return false;
    if (result.winrate) {
        const Outcome outcome = simulate(result, root, result.winrateSamples, -1, cfg, workspace, stop);
        if (outcome == Outcome::Cancelled)
            return false;
        ++result.winrateSamples;
        result.winrateWins += outcome == Outcome::Won;
        return true;
    }
    if (!result.fullScores && result.candidates.front().samples != 0)
        expand(result, root, cfg, workspace);
    Candidate &candidate = result.candidates[select(result, cfg.monteCarlo)];
    // 所有首招的第 k 个样本使用同一雷盘种子；取消不改变该候选的完整样本序号。
    const Outcome outcome = simulate(result, root, candidate.samples, candidate.cellId, cfg, workspace, stop);
    if (outcome == Outcome::Cancelled)
        return false;
    ++candidate.samples;
    candidate.wins += outcome == Outcome::Won;
    estimate(candidate, cfg.monteCarlo);
    return true;
}

} // 命名空间 mss
