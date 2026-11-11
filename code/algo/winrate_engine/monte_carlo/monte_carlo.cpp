#include "algo/winrate_engine/monte_carlo/monte_carlo.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/assert.h"
#include "game/game.h"

namespace mss {

struct MonteCarlo::Workspace {
    Grid<char> mines;
};

__declspec(noinline) MonteCarlo::Workspace &MonteCarlo::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

MonteCarlo::Candidate::Candidate(ObservedBoard::CellId cell)
    : cell(cell), samples(0), wins(0), chanceSamples(-1), chanceTarget(0.0L), chanceLog(0.0L) {
    MonteCarlo().estimate(*this);
}

MonteCarlo::Result::Result()
    : winrate(false), samples(0), wins(0), random(0xC0FFEE12345ULL, 0xD1B54A32D192ED03ULL) {}

MonteCarlo::Result::Result(SuggestResult &root) : Result() {
    winrate = root.engine == SuggestResult::Engine::Safe || root.reason == SuggestResult::Reason::Finished;
    if (root.engine == SuggestResult::Engine::BruteForce) exact.swap(root);
    else if (winrate) safeCells.swap(root.suggest);
}

int MonteCarlo::Result::best() const {
    int best = 0;
    for (int i = 1; i < (int)candidates.size; ++i)
        if (candidates[i].samples != 0 && (candidates[best].samples == 0 || candidates[i].mean > candidates[best].mean)) best = i;
    return best;
}

ObservedBoard::CellId MonteCarlo::Result::recommended() const {
    if (exact.engine == SuggestResult::Engine::BruteForce) return exact.suggest.size == 0 ? -1 : exact.suggest[0];
    return winrate || candidates.size == 0 ? -1 : candidates[best()].cell;
}

void MonteCarlo::estimate(Candidate &candidate) {
    const long double count = candidate.samples + 2.0L;
    candidate.mean = (candidate.wins + 1.0L) / count;
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

int MonteCarlo::select(Result &result) {
    // 每个候选先采一次，再按采样结果分配后续对局。
    if (result.samples < (long long)result.candidates.size) return (int)result.samples;
    const int reference = result.best();
    const Candidate &leader = result.candidates[reference];
    if (result.candidates.size == 1) return reference;
    int selected = reference;
    const long double zero = -std::numeric_limits<long double>::infinity();
    long double maximum = zero, pressure = zero;
    const long double leaderReduction = std::log(leader.variance / (leader.samples + 3.0L));
    for (int i = 0; i < (int)result.candidates.size; ++i) {
        if (i == reference) continue;
        Candidate &candidate = result.candidates[i];
        if (candidate.chanceSamples != candidate.samples || candidate.chanceTarget != leader.mean) {
            const long double won = candidate.wins + 1.0L;
            const long double lost = candidate.samples - candidate.wins + 1.0L;
            candidate.chanceLog = logChance(won, lost, leader.mean);
            candidate.chanceSamples = candidate.samples;
            candidate.chanceTarget = leader.mean;
        }
        const long double reduction = std::log(candidate.variance / (candidate.samples + 3.0L));
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

MonteCarlo::Result MonteCarlo::analyze(const Selection::Result &selected) {
    assert_(selected.candidates.size != 0, "MonteCarlo: 非终局没有首招候选");
    Result result;
    result.candidates.resize(selected.candidates.size);
    for (int i = 0; i < (int)selected.candidates.size; ++i)
        result.candidates[i] = Candidate(selected.candidates[i].cell);
    return result;
}

void MonteCarlo::update(Result &result, const Position::Result &position, const MovePolicy::Config &policy, Pool &pool) {
    if (result.exact.engine == SuggestResult::Engine::BruteForce) return;
    Candidate *candidate = result.winrate ? nullptr : &result.candidates[select(result)];
    const long long sequence = candidate ? candidate->samples : result.samples;
    // 不同首招的同序号样本使用同一张抽样雷盘。
    const U128 seed{splitmix64(0x123456789ABCDEFULL + sequence), splitmix64(0xF123456789ABCDE0ULL + sequence)};
    Grid<char> &mines = getWorkspace().mines;
    Combination::Pool &combinations = pool.suggestions.combinations;
    Probability().sampleMineBoard(position.board, position.basic, position.structure, pool.assignments, combinations, seed, mines);
    Game game(mines, position.board, position.config(), combinations);
    if (candidate) game.play(&candidate->cell, 1);
    else game.play(result.safeCells.data, (int)result.safeCells.size);
    while (game.state() == Game::State::Running) {
        const SuggestResult suggestion = game.suggest(policy, pool.suggestions);
        game.play(suggestion.suggest.data, (int)suggestion.suggest.size);
    }
    const bool won = game.state() == Game::State::Won;
    ++result.samples;
    result.wins += won;
    if (candidate) {
        ++candidate->samples;
        candidate->wins += won;
        estimate(*candidate);
    }
}

} // 命名空间 mss
