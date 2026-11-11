#include <fstream>
#include <string>

#include "core/assert.h"

int main(int argc, char **argv) {
    mss::assert_(argc == 3, "参数：patterns.csv、输出头文件");
    std::ifstream input(argv[1]);
    mss::assert_(input.good(), "无法打开模板 CSV");
    std::string line;
    std::getline(input, line);
    mss::assert_(line == "id,hit_probability_sum,pattern" || line == "id,hit_probability_sum,pattern\r", "模板 CSV 表头有误");
    std::string header = "// 由 code/tools/fifty_fifty_catalog/generate.cpp 从 patterns.csv 生成。\n#pragma once\n\n"
                         "namespace mss::FiftyFiftyCatalog {\ninline constexpr const char *patterns[] = {\n";
    while (std::getline(input, line)) {
        if (line.ends_with('\r')) line.pop_back();
        const std::size_t comma = line.rfind(',');
        mss::assert_(comma != std::string::npos && comma + 1 < line.size() && line.find_first_not_of("AB./", comma + 1) == std::string::npos, "模板 CSV 的 pattern 列有误");
        header += "    \"";
        header.append(line, comma + 1);
        header += "\",\n";
    }
    mss::assert_(input.eof(), "读取模板 CSV 失败");
    std::ofstream output(argv[2], std::ios::binary);
    output << header << "};\n} // 命名空间 mss::FiftyFiftyCatalog\n";
    output.close();
    mss::assert_(output.good(), "写入模板头文件失败");
    return 0;
}
