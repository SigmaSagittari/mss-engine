#pragma once

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string_view>

namespace mss {

// 所有诊断共用此锁，避免并发追加日志时交错。
inline std::mutex diagnosticMutex;

// 把一条诊断同时写进错误日志与本次运行日志；文件打不开时静默跳过。
inline void reportDiagnostic(const char *kind, std::string_view message, const std::source_location &location) {
    std::lock_guard<std::mutex> lock(diagnosticMutex);
    std::ofstream log("mss_error.log", std::ios::app);
    std::ofstream currentRunLog("mss_current_run.log", std::ios::app);
    std::ofstream *outputs[2] = {&log, &currentRunLog};
    for (int i = 0; i < 2; ++i) {
        std::ofstream &output = *outputs[i];
        if (!output) continue;
        output << '[' << kind << "]\n";
        output << "message: " << message << '\n';
        output << "file: " << location.file_name() << '\n';
        output << "line: " << location.line() << '\n';
        output << "function: " << location.function_name() << "\n\n";
        output.flush();
    }
}

// 条件为真时无操作；为假时记录调用位置并以状态码 1 退出进程，不抛异常。
inline void assert_(bool condition, std::string_view message, const std::source_location location = std::source_location::current()) {
    if (condition) return;
    reportDiagnostic("assert", message, location);
    std::exit(1);
}

// 记录警告后返回，不中断当前流程。
inline void warn_(std::string_view message, const std::source_location location = std::source_location::current()) {
    reportDiagnostic("warn", message, location);
}

} // 命名空间 mss
