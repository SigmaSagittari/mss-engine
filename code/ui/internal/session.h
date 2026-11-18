#pragma once

#include <functional>
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
    enum class Error {
        None = 0, // 盘面参数校验通过。
        TemplatePrefix = 1,
        LtrEventLimit = 2,
        BruteLimit = 3,
        ThreadLimit = 4,
        NodeLimit = 5,
        SearchWidth = 6,
        BoardParameters = 7,
        BoardRange = 8,
        BoardCells = 9,
        SolverOrder = 10,
        AnalysisExpired = 11,
        CellIndex = 12,
        IoeBeginner = 13,
        IoeClicks = 14,
        IoeMissingClicks = 15,
        PlayerFlags = 16,
        IoeCandidateLimit = 17,
        IoeDomLimit = 18,
        IoeFlagOnOpen = 19,
        IoeUnsolvable = 20,
        EffThreshold = 21,
        Seed = 22,
        Minefield = 23,
        MineCount = 24,
        BoardMismatch = 25,
        SearchTargetMine = 26,
        SearchAction = 27,
        UnknownEndpoint = 28,
        BoardLogicConflict = 29,
        BoardMineCountConflict = 30,
        BoardComponentConflict = 31,
        HttpMethod = 32,
        RequestOrigin = 33
    };

    Session();
    ~Session();
    std::string index(std::string content) const;
    const std::string &token() const;
    bool stopped() const;
    std::string handle(const std::string &path, const std::string &body);
    std::string analyzeIoe(const std::string &body, const std::function<void(const std::string &)> &publish = {});
    bool startSearch(const std::string &body, std::string &response);
    std::string searchUpdate() const;
    void stopAnalysis();
    std::string error(Error code) const;

  private:
    enum class SearchAction { Pause = 1, Resume = 2 };
    struct FormField;
    struct Analysis;
    struct SearchTask;
    struct Workspace;
    Workspace &getWorkspace();
#ifdef __EMSCRIPTEN__
    std::string advanceSearch();
#endif

    std::vector<FormField> parseForm(const std::string &body) const;
    std::string jsonString(const std::string &value) const;
    bool parseInteger(const std::string &value, int &result) const;
    bool parseSeed(const std::string &value, U128 &seed) const;
    std::string makeToken() const;
    std::string cellString(const ObservedBoard::Result &board) const;
    std::string candidateCount(long double layouts) const;
    void writeSeed(std::ostream &result, U128 seed) const;
    void writeMinefield(std::ostream &result, const Grid<char> &layout) const;
    void writeProbabilities(std::ostream &result, const Position::Result &position, const Probability::BoxResult &boxes, const Probability::Pool &probabilities) const;
    void writeHighlight(std::ostream &result, Position::Result &position, Position::Pool &positions, const std::string &highlight) const;
    std::string searchResult(const SearchTask &task) const;
    void writeMoveDetails(std::ostream &result, const SuggestResult::Details &details, ObservedBoard::CellId cell) const;
    std::string value(const std::vector<FormField> &fields, const std::string &key) const;
    bool parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolverConfig::Order &algo);
    bool parseFiftyFifty(const std::vector<FormField> &fields, Config &config, std::string &response);
    bool parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response);
    Error parseBoardParameters(const std::vector<FormField> &fields, int &rows, int &cols, int &mines) const;
    bool parseBoard(const std::vector<FormField> &fields, int &rows, int &cols, int &mines, std::string &cells, std::string &response);
    ObservedBoard::Result makeBoard(int rows, int cols, int mines, const std::string &cells) const;
    bool createAnalysis(const std::vector<FormField> &fields, std::unique_ptr<Analysis> &analysis, std::string &response);
    std::string analyze(const std::vector<FormField> &fields);
    std::string generateBoard(const std::vector<FormField> &fields);
    std::string generateEffBoard(const std::vector<FormField> &fields);
    std::string playUntilNoSafe(const std::vector<FormField> &fields);
    std::string controlSearch(const std::vector<FormField> &fields);

    // 普通计算由 HTTP 请求串行访问；仅保留最近一次计算，前端负责串行引用其 id。
    std::unique_ptr<Analysis> analysis_;
    int nextAnalysis_;
    bool stopped_;
    std::string token_;
    const Config config_;
    bool searchRunning_;
#ifdef __EMSCRIPTEN__
    std::unique_ptr<SearchTask> searchTask_;
#else
    // 后台任务独占自己的 Analysis；线程先销毁，发布缓存与互斥锁后销毁。
    mutable std::mutex searchMutex_;
    std::condition_variable_any searchWake_;
    std::string searchUpdate_;
    std::jthread searchWorker_;
#endif
};

} // 命名空间 mss::UI
