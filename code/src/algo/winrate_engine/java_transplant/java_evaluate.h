#pragma once

#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "algo/probability_engine/probability/observe.h"
#include "algo/winrate_engine/types.h"
#include "long_term_risk_helper.h"
#include "pseudo_helper.h"

namespace mss {

struct JavaEval {
    struct Config {
        long double progressContribution;
        long double selectionThreshold1;
        long double selectionThreshold2;
        long double influenceScale;
        long double equalityThreshold;
        int weight1;
        int weight2;
        int spaceThreshold;
        Config(long double progressContribution, long double selectionThreshold1, long double selectionThreshold2, long double influenceScale, long double equalityThreshold, int weight1, int weight2, int spaceThreshold) : progressContribution(progressContribution), selectionThreshold1(selectionThreshold1), selectionThreshold2(selectionThreshold2), influenceScale(influenceScale), equalityThreshold(equalityThreshold), weight1(weight1), weight2(weight2), spaceThreshold(spaceThreshold) {}
    };

    struct Evaluation {
        winrate::CellId cellId;
        // -1 表示被裁剪；其余为原算法权重。
        long double value;
        Evaluation(winrate::CellId cellId, long double value) : cellId(cellId), value(value) {}
    };
    using Result = std::vector<Evaluation>;

  private:
    struct Candidate {
        winrate::CellId cell;
        long double safety;
        // 完成时为评分，被裁剪项不参与推荐。
        long double weight;
        long double expectedClears;
        bool deferGuessing;
        bool pruned;
        // -1 表示没有支配格。
        winrate::CellId dominatingLocation;
        std::vector<winrate::CellId> commonClears;
        Candidate(winrate::CellId cell, long double safety, long double weight, long double expectedClears, bool deferGuessing, bool pruned, winrate::CellId dominatingLocation, std::vector<winrate::CellId> commonClears) : cell(cell), safety(safety), weight(weight), expectedClears(expectedClears), deferGuessing(deferGuessing), pruned(pruned), dominatingLocation(dominatingLocation), commonClears(std::move(commonClears)) {}
    };

    using Scores = std::vector<Candidate>;

    struct EmptyBoxes {
        std::vector<winrate::CellId> cells;
        std::vector<int> offsets;
    };

    struct ForcedInfo {
        long double livingClears;
        bool dominated;
        ForcedInfo(long double livingClears, bool dominated) : livingClears(livingClears), dominated(dominated) {}
    };

    struct BoardSummary {
        long double blendedSafety;
        long double clears;
        // -1 表示没有唯一最安全格。
        winrate::CellId singleSafestTile;
        BoardSummary(long double blendedSafety, long double clears, winrate::CellId singleSafestTile) : blendedSafety(blendedSafety), clears(clears), singleSafestTile(singleSafestTile) {}
    };

    struct FrontierBox {
        long double safety;
        Structure::InstanceId instance;
        Structure::BoxId box;
        FrontierBox(long double safety, Structure::InstanceId instance, Structure::BoxId box) : safety(safety), instance(instance), box(box) {}
    };

  public:
    struct Workspace {
      public:
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability, LongTermRisk::Workspace &risk, Pseudo::Workspace &pseudo) : basic(basic), structure(structure), probability(probability), risk(risk), pseudo(pseudo) {}

      private:
        friend struct JavaEval;
        Basic::Workspace &basic;
        Structure::Workspace &structure;
        Probability::Workspace &probability;
        LongTermRisk::Workspace &risk;
        Pseudo::Workspace &pseudo;
        ObservedBoard::Delta boardDelta;
        Basic::Delta basicDelta;
        Structure::Delta structureDelta;
        // winrate::Probability 不允许空构造；首次以真实分支构造，以后复用输出容量。
        std::optional<winrate::Probability> nextProbability;
        Scores scores;
        // observed 置位前先写对应观测槽；每次 update 重置占用标记。
        std::vector<Observe::Result> observations;
        std::vector<char> observed;
        RawGrid<char> visited;
        std::vector<std::pair<int, int>> stack;
        Pseudo::Result pseudoResult;
        EmptyBoxes forcedBoxes;
        EmptyBoxes summaryBoxes;
        EmptyBoxes commonBoxes;
        std::vector<FrontierBox> boxes;
        std::vector<winrate::CellId> onEdge;
        std::vector<winrate::CellId> offEdge;
        std::vector<winrate::CellId> offEdgeCandidates;
        std::vector<winrate::CellId> notDeferred;
        std::vector<winrate::CellId> deferred;
    };

    // 输入与 risk 来自同一有效盘面，候选数 > 0；risk 须由构造工作区时传入的 LongTermRisk 工作区按 riskConfig 扫描产生。
    // 临时揭示在返回前恢复；池保留分支缓存，池视图不得跨调用持有；工作区不可并发或重入。
    // 自动入口沿用原候选筛选、排序与支配替换，推荐项放在首位；保留被裁剪项。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune = true);
    static void update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune = true);
    // 自定义入口按输入顺序返回，只评估给定的 Hidden 非确定雷格；空列表返回空列表。
    // prune=false 完整计算所有数字分支，不使用支配快路径；不做自动候选筛选或推荐替换。
    static Result analyze(const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune);
    static void update(Result &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells cells, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune);

  private:
    static void evaluateAutomatic(Scores &result, const winrate::Input &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, const LongTermRisk::Config &riskConfig, const Pseudo::Config &pseudoConfig, Workspace &workspace, bool prune);
    static void exportScores(Result &result, const Scores &scores);
    static Candidate evaluate(const winrate::Input &input, LongTermRisk::Analysis &risk, winrate::CellId cell, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune, bool allowDominated, const Candidate *best, const Observe::Result &observation);

    static winrate::Cells selectCandidates(const winrate::View &input, const LongTermRisk::Result &risk, winrate::Cells dead, const Config &cfg, Workspace &workspace);
    static void evaluateCandidates(Scores &result, winrate::Cells cells, const winrate::Input &input, LongTermRisk::Analysis &risk, const Config &cfg, const LongTermRisk::Config &riskConfig, Workspace &workspace, bool prune, bool allowDominated);
    static void selectRecommendation(Scores &result, const Config &cfg);
    static bool isNumberState(winrate::CellState state);
    static bool evaluateContains(winrate::Cells cells, winrate::CellId cell);
    static long double evaluateBoxTally(winrate::CellId cell, const winrate::View &input);
    static long double hotspotSafety(const LongTermRisk::Analysis &risk, winrate::CellId candidate, const winrate::View &input);
    static bool hotspotExempt(const LongTermRisk::Analysis &risk, winrate::CellId cell);
    static bool evalLess(const Candidate &lhs, const Candidate &rhs);
    static ForcedInfo inspectForced(const winrate::Structure &structure, const winrate::Probability &probability, winrate::CellId exclude, const winrate::Shapes &shapes, Workspace &workspace);
    static BoardSummary summarizeBoard(const winrate::View &input, const Config &cfg, Workspace &workspace);
    static void collectEmptyBoxes(EmptyBoxes &out, const winrate::Structure &structure, const winrate::Probability &probability, const winrate::Shapes &shapes);
    static void intersectBoxes(EmptyBoxes &out, const EmptyBoxes &next);
    static bool meetsSpaceThreshold(int startX, int startY, const winrate::Board &board, const winrate::Basic &basic, int threshold, Workspace &workspace);
    static const Candidate *findAlternativeMove(const Candidate &move, const std::vector<Candidate> &evaluated, const Config &cfg);
    static ForcedInfo analyzeForced(const winrate::Input &input, winrate::CellId cell, Workspace &workspace);
};

} // 命名空间 mss
