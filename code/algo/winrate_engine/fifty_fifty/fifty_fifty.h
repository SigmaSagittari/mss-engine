#pragma once

#include "algo/winrate_engine/fifty_fifty/old_java/long_term_risk.h"
#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"
#include "algo/winrate_engine/fifty_fifty/templates/semi.h"

namespace mss {

struct FiftyFifty {
    enum class Algorithm { OldJava, Templates };

    struct TemplateConfig {
        int prefix;
    };

    struct Config {
        Algorithm algorithm;
        // 借用所选后端的配置，另一指针可为空。
        const LongTermRisk::Config *oldJava;
        const TemplateConfig *templates;
        const int *eventLimit; // 启用模板 LTR 时借用事件上限。其余情况传 nullptr。

        void hash(U128Hasher &hasher, bool longTermRisk) const {
            hasher.mix((int) algorithm);
            if (algorithm == Algorithm::Templates) {
                hasher.mix(templates->prefix);
                if (longTermRisk) hasher.mix(*eventLimit);
            } else if (longTermRisk) oldJava->hash(hasher);
        }
    };

    struct Pool {
        FiftyFiftyData::Pool prefix100;
        FiftyFiftyData::Pool templates; // 最近使用的非 100 前缀表。
        int prefix; // 非 100 前缀。0 表示尚未编译。

        Pool() : prefix(0) {}
    };

    struct Result {
        LongTermRisk::Result oldJava;
        TemplateLongTermRisk::Result templates;
    };

    // 盘面和概率须匹配且有解。模板前缀为 [1, FiftyFiftyData::count()]。
    Semi::Kind detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position, const Position::ProbabilityResult &probability,
                      const Probability::Pool &probabilities, const Config &config, Pool &pool,
                      const vector<ObservedBoard::CellId> &dead);

    // 完成当前盘面所选路线的风险扫描。模板后端可同时输出前缀 100、事件上限 1 的粗 LTR。
    // 返回正式 LTR 借用的模板表。池销毁或该表重新编译前，可与本次 result 一起使用。旧 Java 返回空指针。
    const FiftyFiftyData::Pool *prepareRisk(Result &result, Position::Result &position, Position::Pool &positions,
                                           const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                           const Config &config, Pool &pool, Grid<long double> *coarse = nullptr);

    // 使用同盘面、同路线完整扫描的结果。模板路线使用 prepareRisk 返回的表。
    // 返回可直接加到根层评分的补偿概率。
    long double influence(ObservedBoard::CellId cell, Result &result, Position::Result &position, Position::Pool &positions,
                          const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                          Algorithm algorithm, const FiftyFiftyData::Pool *templates);

  private:
    static constexpr int coarsePrefix = 100;
    const FiftyFiftyData::Pool &prepareTemplates(Pool &pool, int prefix);
};

} // 命名空间 mss
