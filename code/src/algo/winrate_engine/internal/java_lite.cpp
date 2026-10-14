#include "java_lite.h"

namespace mss {

JavaLite::Summary JavaLite::evaluate(const winrate::View &input, int remainingSafe) {
    const auto &[board, basic, structure, probability, shapes] = input;
    // 未揭示的 S = 总格数 - 已揭示数 - F - T - H；H 按 Box 大小扣除。
    int known = board.totalMines + remainingSafe - basic.mineSum - basic.unknownSum;
    int clears = 0;
    long double best = basic.unknownSum != 0 ? 1.0L - probability.tCellProbability() : 0.0L;
    long double second = best;
    auto add = [&](long double safety) {
        if (safety > best) { second = best; best = safety; }
        else if (safety > second) second = safety;
    };
    for (int component = 0; component < (int)structure.components.size(); ++component) {
        const Structure::Instance &instance = shapes.getInstance(structure.components[component]);
        for (int box = 0; box < (int)instance.boxes.count(); ++box) {
            const int count = instance.boxes.cellCount(shapes, box);
            const long double safety = 1.0L - probability.boxProbability(component, box);
            known -= count;
            if (safety == 1.0L) clears += count;
            add(safety);
            if (count > 1) add(safety);
        }
    }
    if (known != 0) add(1.0L);
    if (known > 1) add(1.0L);
    clears += known;
    // T 格并不在 frontierCells 中；零雷时它们同样能支持安全步截断。
    if (basic.unknownSum != 0 && probability.tCellProbability() == 0.0L) clears += basic.unknownSum;
    // 安全格已全部揭开就是获胜；空后续候选的价值为 1。
    return Summary(remainingSafe == 0 ? 1.0L : (4.0L * best + second) / 5.0L, clears);
}

} // 命名空间 mss
