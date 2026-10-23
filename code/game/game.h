#pragma once

#include "game/position.h"

namespace mss {

namespace GameControl {

struct Game {
    enum class State { Running, Won, Lost };

    // 观测只含数字、Hidden、ForcedMine，且与 mineBoard 一致。
    // suggestions 由调用方持有，必须覆盖 Game 生命周期，可跨 Game 串行复用。
    Game(const MineBoard &mineBoard, const ObservedBoard::Result &observedBoard, Workspace &workspace, const Config &config, SuggestPool &suggestions);
    // rows、cols 须为正且 0 <= mines < rows*cols；首次实际点击时布雷并保证该格无雷。
    Game(int rows, int cols, int mines, U128 seed, Workspace &workspace, const Config &config, SuggestPool &suggestions);

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    // 按原尺寸和雷数重开空盘，保存新种子，等待首次点击布雷。
    void reset(U128 seed);
    // 引用由 Game 持有，生命周期不超过 Game。
    const Position &position() const;
    // 查询会填缓存，需要非 const 引用。
    Position &position();
    void suggest(const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &pool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, vector<ObservedBoard::CellId> &result);
    // 只揭开 Hidden，其余格跳过；本局必须处于 Running；随机局按首个实际揭开格布雷。
    void play(span<const ObservedBoard::CellId> cells);
    // 当前局必须已布雷，坐标必须在盘面内。
    int number(int row, int col) const;
    State state() const;
    // 连续执行确定安全的建议；需要猜测、无可走格或游戏结束时返回。
    void playUntilNoSafe();

  private:
    static int countRemainingSafe(const ObservedBoard::Result &observedBoard);

    MineBoard mineBoard_;
    bool hasMineBoard_;
    // 有值表示当前局尚未布雷，首次实际点击后消耗。
    U128 pendingSeed_;
    bool hasPendingSeed_;
    int remainingSafe_;
    State state_;
    Position position_;
    SuggestPool &suggestions_;
};

} // 命名空间 GameControl

} // 命名空间 mss
