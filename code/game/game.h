#pragma once

#include "algo/winrate_engine/move_policy.h"

namespace mss {

struct Game {
    enum class State { Running, Won, Lost };

    // 两种构造都借用 combinations，该池须覆盖 Game 的生命周期，可供多局串行共用。
    // rows 和 cols 为正，0 <= mines < rows * cols。首次点击时布雷并保证首格安全。
    Game(int rows, int cols, int mines, U128 seed, const Position::Config &config, Combination::Pool &combinations);
    // 雷位为 0 或 1，须与可见盘面的尺寸、总雷数、已揭数字及强制标记一致。
    Game(const Grid<char> &mines, const ObservedBoard::Result &board, const Position::Config &config, Combination::Pool &combinations);

    // 按原尺寸和雷数重开空盘，下次实际点击时布雷。
    void reset(U128 seed);
    State state() const;
    // 借用当前局面。盘面和 Basic 即时更新，Structure 在分析时同步。
    const Position::Result &position() const;

    // x 是列，y 是行，坐标须在盘面内。点击后返回真实数字，雷格返回 9。
    int play(int x, int y);
    // 格号互异且在盘面内。只揭开 Hidden 和 ForcedSafe，遇到胜负即停止。
    void play(const ObservedBoard::CellId *cells, int count);
    // 当前局须已布雷，坐标须在盘面内。
    int number(int x, int y) const;
    // 连续揭开必安格，直到需要猜测或对局结束。
    void playUntilNoSafe();
    // pool 可跨 Game 和 reset 复用。useCache 启用建议缓存，命中时只返回推荐格、引擎和原因。
    SuggestResult suggest(const MovePolicy::Config &config, Suggest::Pool &pool, bool useCache = true);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    void generate(int first);

    Grid<char> mines_;
    U128 seed_; // 仅在等待首次布雷时使用。
    bool generated_;
    int remainingSafe_;
    State state_;
    Position::Pool positions_;
    Position::Result position_;
};

} // 命名空间 mss
