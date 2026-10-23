#pragma once

#include <string>

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/move_policy.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "game/game_control.h"
#include "game/position_state.h"

namespace mss {

namespace GameControl {

// 人工观测入口会检查局部数字约束和全盘可满足性；失败写入 error()，这是预期错误。
// 合法 Position 的查询共用 Workspace，不得并发或重入。
struct Position {
    // 人工观测入口：检查局部数字约束和全盘可满足性，错误写入 error()。
    static Position fromUserInput(const ObservedBoard::Result &board, Workspace &workspace, const Config &config);

    Position(const Position &) = delete;
    Position &operator=(const Position &) = delete;

    const ObservedBoard::Result &observedBoard() const;
    // 空串表示合法；非空是预期错误文案，不应继续查询。
    const std::string &error() const;
    bool valid() const;

    // 以下查询要求局面合法；会按需合并结构、计算并缓存概率，所以不是 const。
    const Basic::Result &basic() const;
    const Structure::Result &structure();
    const Structure::Pool &shapePool();
    const Probability::Result &probability();
    // 状态引用；调用时确保结构已同步、概率已计算。winrate 可在此临时强制标记并恢复。
    PositionState &state();

    // 已知 S/F 直接返回 0/1，其余格按需查询全盘概率。
    long double mineProbability(ObservedBoard::CellId cell);
    // cell 必须是 Hidden 或 ForcedSafe；返回揭开后的数字与爆炸概率。
    void observe(ObservedBoard::CellId cell, Observe::Result &result);
    // 优先返回 Basic 推导的安全格；没有时统一检查前沿和 T 格的全盘零雷概率。
    span<const ObservedBoard::CellId> safeMoves();
    // 完整评分快照，不读写推荐缓存；fiftyPool 由调用方持有并跨查询复用。
    // cells 非空时至少一格，只比较指定首招；安全格始终优先。
    // search 非空时接收 Anytime 续搜句柄，借用 searchPool 和当前根状态。
    void suggest(const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &searchPool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, SuggestResult &result, const vector<ObservedBoard::CellId> *cells = nullptr, AnytimeSearch::Result *search = nullptr);
    // 只查询推荐格，使用盘面及有效配置缓存；安全时返回全部安全格。
    void suggest(const MovePolicy &policy, SuggestPool &pool, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &searchPool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, vector<ObservedBoard::CellId> &result);

  private:
    friend struct Game;
    enum class Check { User, Trusted };

    Position(const ObservedBoard::Result &board, Workspace &workspace, const Config &config, Check check);
    // Game 专用：输入必须合法，冷路径用 assert_ 检查基础一致性。
    static Position assumeLegal(const ObservedBoard::Result &board, Workspace &workspace, const Config &config);
    static U128 suggestionKey(const ObservedBoard::Result &board, const MovePolicy &policy);
    // 两个查询入口排除安全格后共用分派；缓存未命中时不再重复安全检查。
    void suggest(PositionState &current, const MovePolicy &policy, FiftyFifty::Pool &fiftyPool, AnytimeSearch::Pool &searchPool, AnytimeSearch::Workspace &workspace, BruteForce::Workspace &bruteForce, SuggestResult &result, const vector<ObservedBoard::CellId> *cells, AnytimeSearch::Result *search);

    void initialize();
    void reset(const ObservedBoard::Result &board);
    void update(ObservedBoard::Delta &delta);
    void updateStructure();

    PositionState state_;
    std::string error_;
    vector<ObservedBoard::CellId> safeCells_;
    // Basic 推导出的安全格增量集合；safeMoves 只复查这些格，不再全盘扫描。
    vector<ObservedBoard::CellId> basicSafeCells_;
    vector<unsigned char> basicSafeSeen_;
    Workspace &workspace_;
    Config config_;
};

} // 命名空间 GameControl

} // 命名空间 mss
