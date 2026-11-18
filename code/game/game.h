#pragma once

#include "algo/winrate_engine/move_policy.h"

namespace mss {

struct Minefield {
    // count 为盘面格数，0 <= mines < count，first 是盘面内的格号。
    U128 safeSeed(int count, int mines, U128 seed, int first);
    // layout 尺寸为正且已清零，0 <= mines < 格数。布雷后 random 停在整盘洗牌结束处。
    void generate(Grid<char> &layout, int mines, Random &random);

  private:
    struct Workspace;
    Workspace &getWorkspace();
};

struct Game {
    enum class State { Running, Won, Lost };

    // 两种构造都借用 combinations，该池须覆盖 Game 的生命周期，可供多局串行共用。
    // rows 和 cols 为正，0 <= mines < rows * cols。布雷时从 seed 开始试盘，直到首格安全。
    Game(int rows, int cols, int mines, U128 seed, const Position::Config &config, Combination::Pool &combinations);
    // 雷位为 0 或 1，须与可见盘面的尺寸、总雷数、已揭数字及强制标记一致。
    Game(const Grid<char> &mines, const ObservedBoard::Result &board, const Position::Config &config, Combination::Pool &combinations);

    // 按原尺寸和雷数重开空盘。
    void reset(U128 seed);
    State state() const;
    // 借用当前局面。盘面和 Basic 即时更新，Structure 在分析时同步。
    const Position::Result &position() const;

    // 盘面尚未布雷时调用。first 是随后要首点的格号；布雷后返回实际采用的种子。
    U128 generate(int first);
    // x 是列，y 是行，坐标须在盘面内。未布雷时先保证首格安全。雷格返回 9，其余返回格上的数字。
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

    Grid<char> mines_;
    U128 firstCandidate_; // 首次布雷时先试的种子。
    bool generated_;
    int remainingSafe_;
    State state_;
    Position::Pool positions_;
    Position::Result position_;
};

} // 命名空间 mss
