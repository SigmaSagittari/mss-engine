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
        Semi::Result semi;
        LongTermRisk::Result oldJava;
        TemplateLongTermRisk::Result templates;
    };
    struct Workspace {
        struct OldJava {
            Semi::Workspace semi;
            LongTermRisk::Workspace risk;
            Semi::Result description;
            OldJava(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : risk(basic, structure, probability) {}
        } oldJava;
        struct Templates {
            TemplateSemi::Workspace semi;
            TemplateLongTermRisk::Workspace risk;
            Templates(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : risk(basic, structure, probability) {}
        } templates;
        Workspace(Basic::Workspace &basic, Structure::Workspace &structure, Probability::Workspace &probability) : oldJava(basic, structure, probability), templates(basic, structure, probability) {}
    };

    static int templateCount();
    static void detect(Semi::Result &result, const PositionState &state, const Probability::Pool &probabilities, const Config &config, Pool &pool, Workspace &workspace);
    // 同一根盘面先准备所选路线的风险；OldJava 可同时补找 Semi。
    static void prepareRisk(Result &result, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const Config &config, Pool &pool, Workspace &workspace, bool collectSemi);
    // 两条路线均返回除以全盘布局数后的无条件风险量。
    static long double influence(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, Result &result, const ShapeSolverConfig &shapeConfig, const Config &config, Workspace &workspace);
    static LongTermRisk::Details describeOldJava(ObservedBoard::CellId cell, PositionState &state, const Probability::Pool &probabilities, const ShapeSolverConfig &shapeConfig, const LongTermRisk::Config &config, Workspace::OldJava &workspace);
};

} // 命名空间 mss
