#pragma once

#include <string>

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/java_brute/java_brute.h"
#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "game/game_control.h"
#include "game/position_state.h"

namespace mss {

namespace GameControl {

// 人工观测入口会检查局部数字约束和全盘可满足性；失败写入 error()，这是预期错误。
// 合法 Position 的查询共用 Workspace，不得并发或重入。
struct Position {
    struct SuggestResult {
        // 安全时是所有确定安全格；否则最多一个猜测格。空列表表示没有建议。
        vector<ObservedBoard::CellId> suggest;
        // true 表示列表全部确定安全；false 表示只有一个待猜格。
        bool safeResult;

        SuggestResult() : safeResult(true) {}
    };

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
    // 返回全部确定安全格：Basic 推导、前沿零雷、T 格零雷。
    span<const ObservedBoard::CellId> safeMoves();
    // 无确定安全格时选择最低雷概率格；同概率优先上行、右列；没有可点击格返回 -1。
    ObservedBoard::CellId lowRiskMove();
    // 优先返回全部确定安全格，否则返回一个最低雷概率猜测格。
    void suggest(SuggestResult &result, SuggestPool &pool);
    // JavaBrute：安全格优先，否则按阈值在 BruteForce / JavaEval 中选择一个建议格。
    void suggestJavaBrute(const JavaBrute::Config &config, JavaBrute::Workspace &workspace, SuggestResult &result, SuggestPool &pool);
    // JavaEval：安全格优先，否则返回 JavaEval 评分最高的一个候选。
    void suggestJavaEval(const JavaEval::Config &config, JavaEval::Workspace &workspace, SuggestResult &result, SuggestPool &pool);

  private:
    friend struct Game;
    enum class Check { User, Trusted };

    Position(const ObservedBoard::Result &board, Workspace &workspace, const Config &config, Check check);
    // Game 专用：输入必须合法，冷路径用 assert_ 检查基础一致性。
    static Position assumeLegal(const ObservedBoard::Result &board, Workspace &workspace, const Config &config);
    static U128 suggestionKey(const ObservedBoard::Result &board, int algorithm);
    static U128 suggestionKey(const ObservedBoard::Result &board, int algorithm, const JavaEval::Config &config);
    static U128 suggestionKey(const ObservedBoard::Result &board, int algorithm, const JavaBrute::Config &config);

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
