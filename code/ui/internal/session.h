#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "game/game.h"
#include "ui/ui.h"

namespace mss::UI {

struct Session {
    explicit Session(const Config &config);
    const std::string &token() const;
    bool stopped() const;
    std::string handle(const std::string &path, const std::string &body);
    bool startMcts(const std::string &body, std::string &response);
    void stopMcts();
    std::string mctsUpdate() const;
    bool startSearch(const std::string &body, std::string &response);
    std::string controlSearch(const std::string &body);
    void stopSearch();
    std::string searchUpdate() const;

  private:
    struct FormField {
        std::string key;
        std::string value;
    };
    // 一次计算拥有固定盘面及其结果、缓存；后续查询只引用 id，不接受盘面修改。
    struct Analysis {
        int id;
        GameControl::Config config;
        GameControl::Workspace game;
        BruteForce::Workspace bruteForce;
        AnytimeSearch::Workspace anytimeSearch;
        AnytimeSearch::Pool searchPool;
        MonteCarlo::Workspace monteCarlo;
        GameControl::Position position;
        Analysis(int id, const GameControl::Config &config, const ObservedBoard::Result &board) : id(id), config(config), bruteForce(game.probability.shapeSolver), anytimeSearch(game.basic, game.structure, game.probability), position(GameControl::Position::fromUserInput(board, game, config)) {}
    };

    static std::vector<FormField> parseForm(const std::string &body);
    static std::string jsonString(const std::string &value);
    static bool parseInteger(const std::string &value, int &result);
    static std::string makeToken();
    static std::string cellString(const ObservedBoard::Result &board);
    static void writeProbabilities(std::ostream &result, GameControl::Position &position);
    static void writeCells(std::ostream &result, span<const ObservedBoard::CellId> cells);
    static std::string mctsResult(const MonteCarlo::Result &result, bool complete, long double candidates);
    static void writeMoveDetails(std::ostream &result, const SuggestResult::Details &details, ObservedBoard::CellId cell);
    std::string value(const std::vector<FormField> &fields, const std::string &key) const;
    std::string error(const std::string &message) const;
    bool parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolverConfig::Order &algo);
    bool parseTemplateRisk(const std::vector<FormField> &fields, Config &config, std::string &response);
    bool parseFiftyFifty(const std::vector<FormField> &fields, Config &config, std::string &response);
    bool parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response);
    bool parseBoard(const std::vector<FormField> &fields, int &rows, int &cols, int &mines, std::string &cells, std::string &response);
    static ObservedBoard::Result makeBoard(int rows, int cols, int mines, const std::string &cells);
    bool createAnalysis(const std::vector<FormField> &fields, std::unique_ptr<Analysis> &analysis, std::string &response);
    std::string analyze(const std::vector<FormField> &fields);
    std::string playUntilNoSafe(const std::vector<FormField> &fields);
    void writeSuggestion(std::ostream &result, Analysis &analysis, const Config &config);

    // 普通分析串行复用模板编译表；先声明以覆盖 analysis_ 中借用表的结果的生命周期。
    FiftyFifty::Pool fiftyPool_;
    // 普通计算由 HTTP 请求串行访问；仅保留最近一次计算，前端负责串行引用其 id。
    std::unique_ptr<Analysis> analysis_;
    int nextAnalysis_;
    bool stopped_;
    std::string token_;
    mutable std::mutex mctsMutex_;
    std::string mctsUpdate_;
    // 借用 UI 入口的配置，生命周期覆盖 Session 和后台任务。
    const Config &config_;
    // 后台任务独占自己的 Analysis；线程先销毁，发布缓存与互斥锁后销毁。
    mutable std::mutex searchMutex_;
    std::condition_variable_any searchWake_;
    std::string searchUpdate_;
    long long searchBudget_;
    bool searchRunning_;
    std::jthread mctsWorker_;
    std::jthread searchWorker_;
};

} // 命名空间 mss::UI
