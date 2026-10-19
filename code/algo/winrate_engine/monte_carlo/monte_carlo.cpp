#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/assert.h"

namespace mss {

MonteCarlo::Candidate::Candidate(ObservedBoard::CellId cell, long double prior, const Config &config) : cell(cell), samples(0), wins(0), prior(prior), chanceSamples(-1), chanceTarget(0.0L), chanceLog(0.0L) { estimate(*this, config); }

MonteCarlo::Result::Result(bool winrate, vector<ObservedBoard::CellId> &safeCells) : winrate(winrate), samples(0), wins(0), random(0xC0FFEE12345ULL, 0xD1B54A32D192ED03ULL) {
    this->safeCells.data = safeCells.data;
    this->safeCells.size = safeCells.size;
    this->safeCells.capacity = safeCells.capacity;
    safeCells.data = nullptr;
    safeCells.size = safeCells.capacity = 0;
}

int MonteCarlo::Result::best() const {
    int best = 0;
    for (int i = 1; i < (int)candidates.size; ++i)
        if (candidates.data[i].samples != 0 && (candidates.data[best].samples == 0 || candidates.data[i].mean > candidates.data[best].mean)) best = i;
    return best;
}

ObservedBoard::CellId MonteCarlo::Result::recommended() const { return winrate || candidates.size == 0 ? -1 : candidates.data[best()].cell; }

MonteCarlo::Workspace::Workspace() : javaEval(game.basic, game.structure, game.probability), javaBrute(game.basic, game.structure, game.probability, game.probability.shapeSolver) {}

void MonteCarlo::estimate(Candidate &candidate, const Config &config) {
    const long double count = candidate.samples + 2.0L + config.priorWeight;
    candidate.mean = (candidate.wins + 1.0L + config.priorWeight * candidate.prior) / count;
    candidate.variance = candidate.mean * (1.0L - candidate.mean) / (count + 1.0L);
}

long double MonteCarlo::logChance(long double won, long double lost, long double target) {
    if (won == lost && target == 0.5L) return -std::log(2.0L);
    const bool direct = 1.0L - target < (lost + 1.0L) / (won + lost + 2.0L);
    const long double a = direct ? lost : won, b = direct ? won : lost, x = direct ? 1.0L - target : target;
    long double c = 1.0L, d = 1.0L / (1.0L - (a + b) * x / (a + 1.0L)), fraction = d;
    for (int m = 1;; ++m) {
        if (m > 4096) assert_(false, "MonteCarlo: 尾概率连分式未收敛");
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
}

int MonteCarlo::select(Result &result, const Config &config) {
    const int reference = result.best();
    if (result.candidates.size == 1 || result.candidates.data[reference].samples == 0) return reference;
    int selected = reference;
    const long double zero = -std::numeric_limits<long double>::infinity();
    long double maximum = zero, pressure = zero;
    const Candidate &leader = result.candidates.data[reference];
    const long double leaderReduction = std::log(leader.variance / (leader.samples + 3.0L + config.priorWeight));
    for (int i = 0; i < (int)result.candidates.size; ++i) {
        if (i == reference) continue;
        Candidate &candidate = result.candidates.data[i];
        if (candidate.chanceSamples != candidate.samples || candidate.chanceTarget != leader.mean) {
            const long double won = candidate.wins + 1.0L + config.priorWeight * candidate.prior;
            const long double lost = candidate.samples - candidate.wins + 1.0L + config.priorWeight * (1.0L - candidate.prior);
            candidate.chanceLog = logChance(won, lost, leader.mean);
            candidate.chanceSamples = candidate.samples;
            candidate.chanceTarget = leader.mean;
        }
        const long double reduction = std::log(candidate.variance / (candidate.samples + 3.0L + config.priorWeight));
        const long double sumMaximum = (std::max)(leaderReduction, reduction);
        const long double totalReduction = sumMaximum + std::log1p(std::exp((std::min)(leaderReduction, reduction) - sumMaximum));
        const long double leaderPressure = candidate.chanceLog + leaderReduction - totalReduction;
        const long double pressureMaximum = (std::max)(pressure, leaderPressure);
        // 零竞争概率的对数为负无穷，不能相减后送入 log-sum-exp。
        pressure = pressure == zero ? leaderPressure : pressureMaximum + std::log1p(std::exp((std::min)(pressure, leaderPressure) - pressureMaximum));
        const long double value = candidate.chanceLog + reduction - totalReduction - std::log(-std::log1p(-result.random.nextUnit()));
        if (value > maximum) { maximum = value; selected = i; }
    }
    if (pressure - std::log(-std::log1p(-result.random.nextUnit())) > maximum) selected = reference;
    return selected;
}

MonteCarlo::Result MonteCarlo::analyze(PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace, const vector<ObservedBoard::CellId> *cells) {
    vector<ObservedBoard::CellId> safeCells;
    int remaining = state.board.cells.rows() * state.board.cells.cols() - state.board.totalMines;
    for (int row = 0; row < state.board.cells.rows(); ++row)
        for (int col = 0; col < state.board.cells.cols(); ++col) {
            if (state.board.cells[row][col] <= ObservedBoard::CellState::Num8) { --remaining; continue; }
            const ObservedBoard::CellId cell = state.board.id(row, col);
            if (Probability::mineProbability(state.probability, cell, state.board, state.basic, state.structure) == 0.0L) safeCells.push_back(cell);
        }
    Result result(safeCells.size != 0 || remaining == 0, safeCells);
    if (result.winrate) return result;
    JavaEval::Result evaluated = JavaEval::analyze(state, shapeConfig, config.javaEval, workspace.javaEval, nullptr, cells);
    assert_(evaluated.candidates.size != 0, "MonteCarlo: 非终局没有首招候选");
    std::sort(evaluated.candidates.data, evaluated.candidates.data + evaluated.candidates.size, [](const JavaEval::Candidate &left, const JavaEval::Candidate &right) { return left.value > right.value; });
    const long double maximum = evaluated.candidates.data[0].value;
    long double total = 0.0L;
    for (int i = 0; i < (int)evaluated.candidates.size; ++i) {
        evaluated.candidates.data[i].value = std::exp((evaluated.candidates.data[i].value - maximum) / config.temperature);
        total += evaluated.candidates.data[i].value;
    }
    for (int i = 0; i < (int)evaluated.candidates.size; ++i)
        result.candidates.push_back(Candidate(evaluated.candidates.data[i].cell, evaluated.candidates.data[i].value / total, config));
    return result;
}

bool MonteCarlo::simulate(Result &result, PositionState &state, long long sequence, ObservedBoard::CellId first, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace) {
    Grid<char> mines;
    Probability::sampleMineBoard(state.board, state.basic, state.structure, state.shapes, result.assignments, U128(splitmix64(0x123456789ABCDEFULL + sequence), splitmix64(0xF123456789ABCDE0ULL + sequence)), mines, workspace.game.probability);
    ObservedBoard::Result observed = state.board;
    for (int i = 0; i < (int)result.safeCells.size; ++i) {
        const std::pair<int, int> position = observed.pos(result.safeCells.data[i]);
        if (observed.cells[position.first][position.second] == ObservedBoard::CellState::ForcedSafe) observed.cells[position.first][position.second] = ObservedBoard::CellState::Hidden;
    }
    const GameControl::Config gameConfig{shapeConfig};
    GameControl::Game game(GameControl::MineBoard(mines), observed, workspace.game, gameConfig, result.suggestions);
    if (game.state() == GameControl::Game::State::Running)
        game.play(result.winrate ? span<const ObservedBoard::CellId>(result.safeCells.data, (int)result.safeCells.size) : span<const ObservedBoard::CellId>(&first, 1));
    GameControl::Game::SuggestResult suggestion;
    while (game.state() == GameControl::Game::State::Running) {
        if (config.playout == PlayoutAlgo::LowRisk) game.suggest(suggestion);
        else if (config.playout == PlayoutAlgo::JavaEval) game.suggestJavaEval(config.javaEval, workspace.javaEval, suggestion);
        else game.suggestJavaBrute(config.javaBrute, workspace.javaBrute, suggestion);
        if (suggestion.suggest.size == 0) assert_(false, "MonteCarlo: 对局未结束却没有建议");
        game.play(span<const ObservedBoard::CellId>(suggestion.suggest.data, (int)suggestion.suggest.size));
    }
    return game.state() == GameControl::Game::State::Won;
}

void MonteCarlo::update(Result &result, PositionState &state, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace) {
    const int selected = result.winrate ? -1 : select(result, config);
    const long long sequence = result.winrate ? result.samples : result.candidates.data[selected].samples;
    const bool won = simulate(result, state, sequence, result.winrate ? -1 : result.candidates.data[selected].cell, shapeConfig, config, workspace);
    ++result.samples;
    result.wins += won;
    if (selected >= 0) {
        Candidate &candidate = result.candidates.data[selected];
        ++candidate.samples;
        candidate.wins += won;
        estimate(candidate, config);
    }
}

} // 命名空间 mss
