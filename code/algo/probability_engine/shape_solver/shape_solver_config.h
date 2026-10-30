#pragma once

namespace mss {

// ShapeSolver 的调用方参数；GraphSolver 直接用这份配置，不反向依赖 ShapeSolver。
struct ShapeSolverConfig {
    enum class Order {
        Adjacent, // 只按相邻关系贪心，开销最低。
        Window3,  // 比较局部三步窗口，开销略高。
        SA,       // 模拟退火，开销最高。
        Auto,     // 组合 Adjacent 与 Window3。
        AutoSA,   // Auto 仍不理想时追加 SA。
    };

    // Box 数达到此值改用 Graph；只影响后端选择，不影响结果。
    int graphThreshold;
    // Graph 的消元顺序；analyze 和 observe 共用。
    Order order;
    // Auto/AutoSA 中 frontier 峰值超过此值时尝试 Window3/SA。
    int window3Threshold;
    // SA 的轮数、每轮迭代次数和温度区间；迭代次数必须 >= 2，温度必须为正。
    struct SA {
        int rounds;
        int iterations;
        double startTemperature;
        double endTemperature;
    } sa;
};

} // 命名空间 mss
