#include "test/correctness/harness.h"
#include "test/benchmark/harness.h"
#include "ui/ui.h"
#include <fstream>
#include <string>

int main(int argc, char **argv) {
    // 每次启动都清空本次运行日志；累计日志保留跨运行记录。
    {
        std::ofstream currentRunLog("mss_current_run.log", std::ios::trunc);
    }
    if (argc > 1 && std::string(argv[1]) == "--test")
        test::correctness::harness();
    else if (argc > 1 && std::string(argv[1]) == "--benchmark")
        test::benchmark::harness();
    else
        mss::UI::run();
    return 0;
}
