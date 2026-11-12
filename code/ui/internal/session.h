#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>
#ifndef __EMSCRIPTEN__
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include "game/game.h"
#include "ui/ui.h"

namespace mss::UI {

struct Session {
    Session();
    ~Session();
    std::string index(std::string content) const;
    const std::string &token() const;
    bool stopped() const;
    std::string handle(const std::string &path, const std::string &body);
    bool startMcts(const std::string &body, std::string &response);
    std::string mctsUpdate() const;
    bool startSearch(const std::string &body, std::string &response);
    std::string searchUpdate() const;
    void stopAnalysis();

  private:
    struct FormField;
    struct Analysis;
    struct MctsTask;
    struct SearchTask;
#ifdef __EMSCRIPTEN__
    std::string advanceMcts();
    std::string advanceSearch();
#endif

    std::vector<FormField> parseForm(const std::string &body) const;
    std::string jsonString(const std::string &value) const;
    bool parseInteger(const std::string &value, int &result) const;
    std::string makeToken() const;
    std::string cellString(const ObservedBoard::Result &board) const;
    std::string candidateCount(long double layouts) const;
    void writeProbabilities(std::ostream &result, Position::Result &position, Position::Pool &positions) const;
    std::string mctsResult(const MonteCarlo::Result &result, bool complete, long double candidates) const;
    std::string searchResult(const SearchTask &task) const;
    void writeMoveDetails(std::ostream &result, const SuggestResult::Details &details, ObservedBoard::CellId cell) const;
    std::string value(const std::vector<FormField> &fields, const std::string &key) const;
    std::string error(const std::string &message) const;
    bool parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolverConfig::Order &algo);
    bool parseFiftyFifty(const std::vector<FormField> &fields, Config &config, std::string &response);
    bool parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response);
    bool parseBoard(const std::vector<FormField> &fields, int &rows, int &cols, int &mines, std::string &cells, std::string &response);
    ObservedBoard::Result makeBoard(int rows, int cols, int mines, const std::string &cells) const;
    bool createAnalysis(const std::vector<FormField> &fields, std::unique_ptr<Analysis> &analysis, std::string &response);
    std::string analyze(const std::vector<FormField> &fields);
    std::string playUntilNoSafe(const std::vector<FormField> &fields);
    std::string controlSearch(const std::vector<FormField> &fields);
    void writeSuggestion(std::ostream &result, Analysis &analysis, const Config &config, const vector<ObservedBoard::CellId> &targets);

    // 普通计算由 HTTP 请求串行访问；仅保留最近一次计算，前端负责串行引用其 id。
    std::unique_ptr<Analysis> analysis_;
    int nextAnalysis_;
    bool stopped_;
    std::string token_;
    const Config config_;
    long long searchBudget_;
    bool searchRunning_;
#ifdef __EMSCRIPTEN__
    std::unique_ptr<MctsTask> mctsTask_;
    std::unique_ptr<SearchTask> searchTask_;
#else
    // 后台任务独占自己的 Analysis；线程先销毁，发布缓存与互斥锁后销毁。
    mutable std::mutex mctsMutex_;
    std::string mctsUpdate_;
    mutable std::mutex searchMutex_;
    std::condition_variable_any searchWake_;
    std::string searchUpdate_;
    unsigned long long searchBudgetEpoch_; // 暂停或切换连续运行时，作废上一批的手动预算。
    std::jthread mctsWorker_;
    std::jthread searchWorker_;
#endif
};

} // 命名空间 mss::UI
