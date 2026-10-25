#include <fstream>
#include <string>

#include "core/assert.h"

// 输入保留 P 顺序；运行时只加载本体，再生成旋转、封口和匹配前缀树。
int main(int argc, char **argv) {
    mss::assert_(argc == 3, "参数：patterns.csv、输出头文件");
    std::ifstream input(argv[1]);
    std::ofstream output(argv[2]);
    mss::assert_(input.good() && output.good(), "无法打开模板目录或输出头文件");
    output << "// 由 code/tools/fifty_fifty_catalog/generate.cpp 从 patterns.csv 生成。\n// / 分行，A/B 为可互换的两组本体格，. 为本体外位置；封口雷和边界条件由 Table 推导。\n#pragma once\n\nnamespace mss::FiftyFiftyData {\ninline constexpr const char *patterns[] = {\n";
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        output << "    \"" << line.substr(line.find_last_of(',') + 1) << "\",\n";
    }
    output << "};\n} // 命名空间 mss::FiftyFiftyData\n";
    return 0;
}
