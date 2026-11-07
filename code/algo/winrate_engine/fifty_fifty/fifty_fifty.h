#pragma once

#include "algo/winrate_engine/fifty_fifty/old_java/long_term_risk.h"
#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"
#include "algo/winrate_engine/fifty_fifty/templates/semi.h"

namespace mss {

// 50-50 的两条完整路线，各自包含 Semi 检测和长期风险。
struct FiftyFifty {
    enum class Algorithm { OldJava, Templates };
    struct TemplateConfig {
        int prefix;
        TemplateLongTermRisk::Config risk;
        TemplateConfig(int prefix, TemplateLongTermRisk::Config risk) : prefix(prefix), risk(risk) {}
    };
    struct Config {
        Algorithm algorithm;
        // 只借用所选路线的配置；另一指针可以为空。
        const LongTermRisk::Config *oldJava;
        const TemplateConfig *templates;
        Config(Algorithm algorithm, const LongTermRisk::Config *oldJava, const TemplateConfig *templates) : algorithm(algorithm), oldJava(oldJava), templates(templates) {}
    };
    struct Pool {
        // 模板池由调用方持有，须覆盖本池及借用模板表的结果的使用期。
        struct Templates {
            TemplateSemi::Pool &semi;
            TemplateLongTermRisk::Pool &risk;
            Templates(TemplateSemi::Pool &semi, TemplateLongTermRisk::Pool &risk) : semi(semi), risk(risk) {}
        } templates;

        Pool(TemplateSemi::Pool &semi, TemplateLongTermRisk::Pool &risk) : templates(semi, risk) {}
    };
    struct Result {
        LongTermRisk::Result oldJava;
        TemplateLongTermRisk::Result templates;
    };

    int templateCount();
    Semi::Kind detect(vector<ObservedBoard::CellId> &cells, const PositionState &state, const Probability::Pool &probabilities, const Config &config, Pool &pool);
    // 准备风险；OldJava 可同时写入补找候选，命中时返回分类。
    Semi::Kind prepareRisk(Result &result, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, Pool &pool, vector<ObservedBoard::CellId> *semiCells);
    // 两条路线均返回除以全盘布局数后的无条件风险量。
    long double influence(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, Result &result, const ShapeSolverConfig &shapeConfig, const Config &config);
    LongTermRisk::Details describeOldJava(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const LongTermRisk::Config &config);

  private:
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss
