#pragma once

#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"

namespace mss {

// Forced：封闭的二选一死猜。Semi：该格为雷时形成死猜。
enum class FiftyFiftyKind { None, Forced, Semi };

struct FiftyFifty {
    struct Config {
        int prefix; // Semi 和正式 LTR 使用的模板前缀。
        const int *eventLimit; // 启用模板 LTR 时借用事件上限。其余情况传 nullptr。

        void hash(U128Hasher &hasher, bool longTermRisk) const {
            hasher.mix(prefix);
            if (longTermRisk) hasher.mix(*eventLimit);
        }
    };

    struct Pool {
        FiftyFiftyData::Pool prefix100;
        FiftyFiftyData::Pool templates; // 最近使用的非 100 前缀表。
        int prefix; // 非 100 前缀。0 表示尚未编译。

        Pool() : prefix(0) {}
    };

    // 盘面和概率须匹配且有解。模板前缀为 [1, FiftyFiftyData::count()]。
    FiftyFiftyKind detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position, const Position::ProbabilityResult &probability,
                          const Probability::Pool &probabilities, const Config &config, Pool &pool);

    // 扫描当前盘面的正式 LTR。传入 coarse 时同时计算前缀 100、事件上限 1 的粗 LTR。
    // 返回正式 LTR 使用的模板表。表重新编译或池销毁后，该引用失效。
    const FiftyFiftyData::Pool &prepareRisk(TemplateLongTermRisk::Result &result, Position::Result &position, Position::Pool &positions,
                                          const Position::ProbabilityResult &probability, Probability::Pool &probabilities,
                                          const Config &config, Pool &pool, Grid<long double> *coarse = nullptr);

  private:
    static constexpr int coarsePrefix = 100;
    const FiftyFiftyData::Pool &prepareTemplates(Pool &pool, int prefix);
};

} // 命名空间 mss
