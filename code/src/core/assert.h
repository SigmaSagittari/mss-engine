#pragma once

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string_view>

namespace mss {

// 所有断言和警告共用此锁，避免并发追加日志时交错。
inline std::mutex errorLogMutex;

// 条件为真时无操作；为假时尝试记录调用位置并以状态码 1 退出进程，不抛异常。
inline void assert_(bool condition, std::string_view message, const std::source_location location = std::source_location::current()) {
    if (condition) return;
    std::lock_guard<std::mutex> lock(errorLogMutex);
    std::ofstream log("mss_error.log", std::ios::app);
    std::ofstream currentRunLog("mss_current_run.log", std::ios::app);
    auto write = [&](std::ofstream &output) {
        if (!output) return;
        output << "[assert]\n"
               << "message: " << message << '\n'
               << "file: " << location.file_name() << '\n'
               << "line: " << location.line() << '\n'
               << "function: " << location.function_name() << "\n\n";
        output.flush();
    };
    write(log);
    write(currentRunLog);
    log.close();
    currentRunLog.close();
    std::exit(1);
}

// 尝试记录警告后返回，不主动中断当前流程。
inline void warn_(std::string_view message, const std::source_location location = std::source_location::current()) {
    std::lock_guard<std::mutex> lock(errorLogMutex);
    std::ofstream log("mss_error.log", std::ios::app);
    std::ofstream currentRunLog("mss_current_run.log", std::ios::app);
    auto write = [&](std::ofstream &output) {
        if (!output) return;
        output << "[warn]\n"
               << "message: " << message << '\n'
               << "file: " << location.file_name() << '\n'
               << "line: " << location.line() << '\n'
               << "function: " << location.function_name() << "\n\n";
        output.flush();
    };
    write(log);
    write(currentRunLog);
}

} // namespace mss
