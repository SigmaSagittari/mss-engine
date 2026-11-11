#pragma once

#include "algo/probability_engine/probability/observe.h"
#include "core/datastructure/grid.h"
#include "core/datastructure/span.h"
#include "game/position.h"

namespace mss {

struct Selection {
  private:
    friend struct AnytimeSearch;
    enum class Range : unsigned char { First, Second, Fallback };

    struct Cell {
        ObservedBoard::CellId cell;
        long double mine;
        bool preferred;
        Range range;
        bool removed;
    };

    struct Frontier {
        struct Cell {
            ObservedBoard::CellId id;
            long double mine;
        };
        vector<Cell> cells;
        int knownSafe;

        Frontier() : knownSafe(-1) {}
    };

  public:
    struct Config {
        // 允许粗 LTR 让原本筛掉的格进入候选。给首招打分时也计入 LTR 补偿。
        bool longTermRisk;
        // 比较相邻的两个候选。看这两个候选之外、只挨着其中一个的格子。
        // 如果这些格子都确定是雷，就只留先处理的候选。
        bool interchangeable;
        // 两个候选雷率一样，且每种合法摆法里两格要么都是雷、要么都安全，就只留先处理的格。
        bool equivalence;
        // A 的雷率比 B 低。如果 A 有雷的每种摆法里 B 也有雷，就剪掉 B。
        bool dominance;
        // 用 LogicComponent 分块。如果有候选来自至少有 spaceThreshold 个可点格的块，
        // 就跳过较小块里的猜测格。候选都来自较小块时照常选。设为 1 或更小可关闭。
        int spaceThreshold;
        long double selectionThreshold; // 与最低雷率的允许差值，非负。
        long double selectionThreshold2; // 第二档允许比全盘最低雷率高多少，非负。

        void hash(U128Hasher &hasher) const {
            hasher.mix(longTermRisk, interchangeable, equivalence, dominance, spaceThreshold, selectionThreshold, selectionThreshold2);
        }
    };

    struct Candidate {
      private:
        friend struct Selection;
        friend struct AnytimeSearch;
        int linkedClears; // 已知本格安全时确定的格数，-1 表示剪枝未计算。

      public:
        ObservedBoard::CellId cell;
        Observe::Result observation;

        Candidate(ObservedBoard::CellId cell, const Observe::Result &observation)
            : linkedClears(-1), cell(cell), observation(observation) {}
    };

    struct Result {
        vector<Candidate> candidates;
        int nodes; // 本次实际执行的 Observe 和条件概率分析次数。

        Result() : nodes(0) {}
    };

    // 写出筛完的候选及每格点开后出现各个数字或踩雷的概率。
    // probability 对应当前盘面，切片来自 probabilities。positions 用于临时改盘，返回时盘面已恢复。
    // requested 为空时先筛常规候选，不足两格才筛第二档。
    // 第二档筛完仍不足两格时，再从更外面按雷率补一个可用的前沿格。
    // T 格安全率须高于全盘最高安全率的 95%，有必安时跳过这批 T 格。
    // longTermRisk 开启时，借用与盘面同尺寸的 influences。旧 Java 传布局数，模板传补偿概率。
    // requested 非空时只在这组互异的 Hidden 或 ForcedSafe 格内按三个开关筛选，启用支配剪枝时先按雷率排序。
    // result.nodes 记录计算数字概率和重算条件概率的次数。
    void select(Result &result, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                Probability::Pool &probabilities, const Config &config,
                span<const ObservedBoard::CellId> requested = {}, const Grid<long double> *influences = nullptr, bool oldJava = false);

  private:
    struct Workspace;
    Workspace &getWorkspace();
    // 用当前盘面的雷率筛选并排序，覆盖 cells。每格保留常规范围、第二范围或兜底的归属，供调用方逐格筛选。
    void collect(vector<Cell> &cells, Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                 const Probability::Pool &probabilities, const Config &config, span<const ObservedBoard::CellId> requested,
                 const Grid<long double> *influences, bool oldJava);
    bool offEdgeCandidate(const ObservedBoard::Result &board, ObservedBoard::CellId cell);
    bool isDead(const Observe::Result &observation);
    bool interchangeable(const Position::Result &position, const Position::ProbabilityResult &probability,
                         const Probability::Pool &probabilities, ObservedBoard::CellId lhs, ObservedBoard::CellId rhs);
    // 假设 candidate 是雷，按等价和支配规则标记 following 中要删的格。只检查 lastRange 及之前的范围。
    // 返回重算概率的次数，并保存 candidate 为安时带出的必安格数。调用结束时恢复盘面。
    int removeExchange(Candidate &candidate, span<Cell> following, Range lastRange, Frontier &frontier,
                       Position::Result &position, Position::Pool &positions, const Position::ProbabilityResult &probability,
                       Probability::Pool &probabilities, const Config &config);
};

} // 命名空间 mss
