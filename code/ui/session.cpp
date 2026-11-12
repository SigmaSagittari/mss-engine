#include "ui/internal/session.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#endif

#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"
#include "core/config.h"
#include "core/assert.h"

namespace mss::UI {

struct Session::FormField {
    std::string key;
    std::string value;
};
// 一次计算拥有固定盘面及其结果、缓存；后续查询只引用 id，不接受盘面修改。
struct Session::Analysis {
    int id;
    Suggest::Pool suggestions;
    Position::Pool positions;
    Position::CheckedResult checked;

    Analysis(int id, const Position::Config &config, const ObservedBoard::Result &board)
        : id(id), positions(suggestions.combinations), checked(Position().analyzeChecked(board, positions, config, positions.probabilities)) {}
    ~Analysis() { suggestions.anytime.clear(positions); }

    long double mineProbability(ObservedBoard::CellId cell) {
        Position::Result &position = checked.result;
        const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
        return Probability().mineProbability(probability.boxes, positions.probabilities, cell, position.board, position.basic, position.structure);
    }
};

struct Session::MctsTask {
    std::unique_ptr<Analysis> analysis;
    Config config;
    FiftyFifty::Config fifty;
    MovePolicy::Config policy;
    long double layouts;
    bool complete;
    MonteCarlo::Result result;
    MonteCarlo::Pool pool;

    MctsTask(std::unique_ptr<Analysis> &source, const Config &settings, const vector<ObservedBoard::CellId> &targets, bool complete)
        : analysis(source.release()), config(settings), fifty{config.fiftyFifty.algorithm, &config.fiftyFifty.oldJava, &config.fiftyFifty.templates},
          policy(config.policy(fifty)), layouts(Position().analyzeProbability(analysis->checked.result, analysis->positions).totalWays),
          complete(complete), result(initialize(targets)), pool(analysis->positions.structure, analysis->suggestions) {}

    MonteCarlo::Result initialize(const vector<ObservedBoard::CellId> &targets) {
        AnytimeSearch::Result searched;
        SuggestResult::Details details;
        SuggestResult initialized = MovePolicy().suggest(analysis->checked.result, analysis->positions, policy, analysis->suggestions, targets, searched, details);
        analysis->suggestions.anytime.clear(analysis->positions);
        MonteCarlo::Result result(initialized);
        if (!result.winrate && result.exact.engine != SuggestResult::Engine::BruteForce)
            for (int i = 0; i < (int)initialized.candidates.size; ++i)
                result.candidates.push_back(MonteCarlo::Candidate(initialized.candidates[i].cell));
        return result;
    }

    bool finished() const { return result.exact.engine == SuggestResult::Engine::BruteForce || (complete && result.samples != 0); }
    void step() { MonteCarlo().update(result, analysis->checked.result, policy, pool); }
};

struct Session::SearchTask {
    std::unique_ptr<Analysis> analysis;
    AnytimeSearch::Config config;
    vector<ObservedBoard::CellId> safe;
    long double layouts;
    AnytimeSearch::Result result;
    SuggestResult::Details details;
    SuggestResult initialized;

    SearchTask(std::unique_ptr<Analysis> &source, const Config &settings, const vector<ObservedBoard::CellId> &targets,
               const vector<ObservedBoard::CellId> &safe)
        : analysis(source.release()), config(settings.anytime), safe(safe),
          layouts(Position().analyzeProbability(analysis->checked.result, analysis->positions).totalWays),
          initialized(initialize(settings, targets)) {
        if (initialized.engine == SuggestResult::Engine::BruteForce) result.nodes = initialized.nodes;
    }

    SuggestResult initialize(const Config &settings, const vector<ObservedBoard::CellId> &targets) {
        const FiftyFifty::Config fifty{settings.fiftyFifty.algorithm, &settings.fiftyFifty.oldJava, &settings.fiftyFifty.templates};
        return MovePolicy().suggest(analysis->checked.result, analysis->positions, settings.policy(fifty), analysis->suggestions, targets, result, details);
    }

    long long step(long long budget) {
        const long long before = result.nodes;
        config.nodeLimit = budget;
        AnytimeSearch().update(result, analysis->checked.result, analysis->positions, config, analysis->suggestions.anytime, details);
        return result.nodes - before;
    }
};

Session::Session()
#ifdef __EMSCRIPTEN__
    : nextAnalysis_(1), stopped_(false), config_(false), searchBudget_(0), searchRunning_(false) {}
#else
    : nextAnalysis_(1), stopped_(false), token_(makeToken()), config_(true), searchBudget_(0), searchRunning_(false), searchBudgetEpoch_(0) {}
#endif

Session::~Session() = default;

std::string Session::index(std::string content) const {
    const Config &config = config_;
    auto replace = [&](const std::string &marker, const std::string &value) {
        content.replace(content.find(marker), marker.size(), value);
    };
    replace("__MSS_TOKEN__", token_);
    constexpr const char *algorithms[] = {"__MSS_POLICY_LOWRISK__", "__MSS_POLICY_JAVA__", "__MSS_POLICY_ANYTIME__"};
    constexpr const char *fifty[] = {"__MSS_FIFTY_OLDJAVA__", "__MSS_FIFTY_TEMPLATES__"};
    constexpr const char *widths[] = {"__MSS_SEARCH_ROOT_WIDTH__", "__MSS_SEARCH_MIDDLE_WIDTH__", "__MSS_SEARCH_DEEP_WIDTH__"};
    for (int i = 0; i < 3; ++i) replace(algorithms[i], config.algorithm == (MovePolicy::Algorithm)i ? "selected" : "");
    for (int i = 0; i < 2; ++i) replace(fifty[i], config.fiftyFifty.algorithm == (FiftyFifty::Algorithm)i ? "selected" : "");
    for (int i = 0; i < 3; ++i) replace(widths[i], std::to_string(config.anytime.width[i]));
    replace("__MSS_TEMPLATE_PREFIX__", std::to_string(config.fiftyFifty.templates.prefix));
    replace("__MSS_TEMPLATE_COUNT__", std::to_string(FiftyFiftyData().count()));
    replace("__MSS_LTR_EVENT_LIMIT__", std::to_string(config.fiftyFifty.templates.eventLimit));
    replace("__MSS_BRUTE_FORCE_THRESHOLD__", std::to_string(config.brute.limit));
    replace("__MSS_THREAD_LIMIT__", std::to_string(config.brute.config.threadCount));
    replace("__MSS_BRUTE_FORCE_CHECK_ALL__", config.brute.config.checkAllMoves ? "checked" : "");
    replace("__MSS_BRUTE_FORCE_MULTITHREAD__", config.brute.config.parallel ? "checked" : "");
    replace("__MSS_POLICY_NODES__", std::to_string(config.anytime.nodeLimit));
    const std::string versionMarker = "__MSS_VERSION__";
    while (content.find(versionMarker) != std::string::npos) replace(versionMarker, mss::version);
    return content;
}

const std::string &Session::token() const {
    return token_;
}

bool Session::stopped() const {
    return stopped_;
}

std::string Session::jsonString(const std::string &value) const {
    std::string result = "\"";
    for (int i = 0; i < (int)value.size(); ++i) {
        const unsigned char ch = (unsigned char)value[i];
        if (ch == '"' || ch == '\\') {
            result.push_back('\\');
            result.push_back((char)ch);
        } else if (ch == '\n') {
            result += "\\n";
        } else if (ch == '\r') {
            result += "\\r";
        } else if (ch == '\t') {
            result += "\\t";
        } else if (ch < 0x20) {
            const char digits[] = "0123456789abcdef";
            result += "\\u00";
            result.push_back(digits[ch >> 4]);
            result.push_back(digits[ch & 15]);
        } else {
            result.push_back((char)ch);
        }
    }
    result.push_back('"');
    return result;
}

bool Session::parseInteger(const std::string &value, int &result) const {
    const char *begin = value.data();
    const char *end = begin + value.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

std::vector<Session::FormField> Session::parseForm(const std::string &body) const {
    auto decode = [](const std::string &text) {
        std::string result;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '+') {
                result.push_back(' ');
            } else if (text[i] == '%' && i + 2 < text.size()) {
                const auto hexDigit = [](char ch) {
                    if (ch >= '0' && ch <= '9')
                        return ch - '0';
                    if (ch >= 'a' && ch <= 'f')
                        return ch - 'a' + 10;
                    if (ch >= 'A' && ch <= 'F')
                        return ch - 'A' + 10;
                    return -1;
                };
                const int high = hexDigit(text[i + 1]);
                const int low = hexDigit(text[i + 2]);
                if (high < 0 || low < 0)
                    return std::string();
                result.push_back((char)(high * 16 + low));
                i += 2;
            } else {
                result.push_back(text[i]);
            }
        }
        return result;
    };
    std::vector<FormField> fields;
    std::size_t start = 0;
    while (start < body.size()) {
        const std::size_t end = body.find('&', start);
        const std::size_t fieldEnd = end == std::string::npos ? body.size() : end;
        const std::size_t equals = body.find('=', start);
        const std::size_t keyEnd = equals == std::string::npos || equals >= fieldEnd ? fieldEnd : equals;
        const std::size_t valueStart = equals == std::string::npos || equals >= fieldEnd ? fieldEnd : equals + 1;
        fields.push_back({decode(body.substr(start, keyEnd - start)), decode(body.substr(valueStart, fieldEnd - valueStart))});
        start = fieldEnd + 1;
    }
    return fields;
}

std::string Session::value(const std::vector<FormField> &fields, const std::string &key) const {
    for (int i = 0; i < (int)fields.size(); ++i)
        if (fields[i].key == key)
            return fields[i].value;
    return "";
}

std::string Session::cellString(const ObservedBoard::Result &board) const {
    std::string cells;
    cells.reserve(board.cells.rows() * board.cells.cols());
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col) {
            const ObservedBoard::CellState state = board.cells[row][col];
            if (state < ObservedBoard::CellState::Hidden)
                cells.push_back((char)('0' + (int)state));
            else if (state == ObservedBoard::CellState::ForcedMine)
                cells.push_back('M');
            else if (state == ObservedBoard::CellState::ForcedSafe)
                cells.push_back('S');
            else
                cells.push_back('?');
        }
    return cells;
}

std::string Session::makeToken() const {
    std::random_device random;
    const std::uint64_t low = ((std::uint64_t)random() << 32) | (std::uint64_t)random();
    const std::uint64_t high = ((std::uint64_t)random() << 32) | (std::uint64_t)random();
    std::ostringstream token;
    token << std::hex << std::setfill('0') << std::setw(16) << low << std::setw(16) << high;
    return token.str();
}

std::string Session::error(const std::string &message) const {
    return "{\"ok\":false,\"error\":" + jsonString(message) + "}";
}

bool Session::parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolverConfig::Order &algo) {
    const std::string name = value(fields, "algo");
    if (name == "auto")
        algo = ShapeSolverConfig::Order::Auto;
    else if (name == "autoSA")
        algo = ShapeSolverConfig::Order::AutoSA;
    else
        return false;
    return true;
}

bool Session::parseFiftyFifty(const std::vector<FormField> &fields, Config &config, std::string &response) {
    const std::string fifty = value(fields, "fiftyFifty");
    if (fifty == "oldjava") {
        config.fiftyFifty.algorithm = FiftyFifty::Algorithm::OldJava;
        return true;
    }
    if (fifty != "templates") { response = error("50-50 算法无效"); return false; }
    int prefix, eventLimit;
    if (!parseInteger(value(fields, "templatePrefix"), prefix) || prefix < 1 || prefix > FiftyFiftyData().count()) { response = error("模板前缀超出目录"); return false; }
    if (!parseInteger(value(fields, "ltrEventLimit"), eventLimit) || eventLimit < 1) { response = error("LTR 模板匹配上限必须是正整数"); return false; }
    config.fiftyFifty.algorithm = FiftyFifty::Algorithm::Templates;
    config.fiftyFifty.templates = {prefix, eventLimit};
    return true;
}

bool Session::parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response) {
    const std::string name = value(fields, "strategy");
    if (name == "lowrisk") config.algorithm = MovePolicy::Algorithm::LowRisk;
    else if (name == "java") config.algorithm = MovePolicy::Algorithm::JavaEval;
    else if (name == "anytime") config.algorithm = MovePolicy::Algorithm::Anytime;
    else { response = error("建议算法无效"); return false; }
    if (!parseInteger(value(fields, "limit"), config.brute.limit) || config.brute.limit < 0) { response = error("暴力接管阈值必须是非负整数"); return false; }
    if (config.brute.limit != 0) {
#ifndef __EMSCRIPTEN__
        if (!parseInteger(value(fields, "maxThreads"), config.brute.config.threadCount) || config.brute.config.threadCount < 1) { response = error("BruteForce 核心数必须是正整数"); return false; }
        config.brute.config.parallel = value(fields, "multiThread") == "true";
#endif
        config.brute.config.checkAllMoves = value(fields, "checkAllMoves") == "true";
    }
    if (config.algorithm == MovePolicy::Algorithm::Anytime) {
        int nodes;
        if (!parseInteger(value(fields, "nodeBudget"), nodes) || nodes < 1) { response = error("Anytime 每步节点数必须是正整数"); return false; }
        config.anytime.nodeLimit = nodes;
        const char *names[] = {"rootWidth", "middleWidth", "deepWidth"};
        for (int i = 0; i < 3; ++i) if (!parseInteger(value(fields, names[i]), config.anytime.width[i]) || config.anytime.width[i] < 1) { response = error("搜索候选宽度必须是正整数"); return false; }
    }
    if (config.algorithm != MovePolicy::Algorithm::LowRisk && !parseFiftyFifty(fields, config, response)) return false;
    return true;
}

bool Session::parseBoard(const std::vector<FormField> &fields, int &rows, int &cols, int &mines, std::string &cells, std::string &response) {
    if (!parseInteger(value(fields, "rows"), rows) || !parseInteger(value(fields, "cols"), cols) || !parseInteger(value(fields, "mines"), mines)) {
        response = error("盘面参数无效");
        return false;
    }
    if (rows < 1 || cols < 1 || rows > 100 || cols > 100 || mines < 0 || mines >= rows * cols) {
        response = error("自定义盘面范围无效");
        return false;
    }
    cells = value(fields, "cells");
    if ((int)cells.size() != rows * cols || cells.find_first_not_of("012345678?MSG") != std::string::npos) {
        response = error("盘面数据无效");
        return false;
    }
    return true;
}

ObservedBoard::Result Session::makeBoard(int rows, int cols, int mines, const std::string &cells) const {
    ObservedBoard::Result board(rows, cols, mines);
    for (int cell = 0; cell < rows * cols; ++cell) {
        if (cells[cell] >= '0' && cells[cell] <= '8')
            board.cells[cell / cols][cell % cols] = (ObservedBoard::CellState)(cells[cell] - '0');
        else if (cells[cell] == 'M')
            board.cells[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedMine;
        else if (cells[cell] == 'S')
            board.cells[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedSafe;
    }
    return board;
}

bool Session::createAnalysis(const std::vector<FormField> &fields, std::unique_ptr<Analysis> &analysis, std::string &response) {
    ShapeSolverConfig::Order algo;
    if (!parseOrderAlgo(fields, algo)) {
        response = error("ShapeSolver 算法无效");
        return false;
    }
    int rows, cols, mines;
    std::string cells;
    if (!parseBoard(fields, rows, cols, mines, cells, response))
        return false;
    Position::Config config = config_.game;
    config.shapeSolver.order = algo;
    analysis.reset(new Analysis(nextAnalysis_++, config, makeBoard(rows, cols, mines, cells)));
    // 人工快照的局部检查和全盘存在性检查由 Position 构造完成，只在输入边界读取一次。
    if (analysis->checked.error) {
        response = error(analysis->checked.error);
        return false;
    }
    return true;
}

std::string Session::candidateCount(long double layouts) const {
    std::ostringstream count;
    count << std::scientific << std::setprecision(6) << layouts;
    return jsonString(count.str());
}

void Session::writeProbabilities(std::ostream &result, Position::Result &position, Position::Pool &positions) const {
    const ObservedBoard::Result &board = position.board;
    const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
    result << std::setprecision(18) << ",\"probabilities\":[";
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col) {
            if (row != 0 || col != 0)
                result << ',';
            result << Probability().mineProbability(probability.boxes, positions.probabilities, board.id(row, col), board, position.basic, position.structure);
        }
    result << ']';
}

void Session::writeMoveDetails(std::ostream &result, const SuggestResult::Details &details, ObservedBoard::CellId cell) const {
    result << ",\"branches\":[";
    bool first = true;
    for (int i = 0; i < (int)details.outcomes.size; ++i) {
        const SuggestResult::Details::Outcome &branch = details.outcomes.data[i];
        if (branch.cell != cell) continue;
        if (!first) result << ',';
        first = false;
        result << "{\"digit\":" << branch.digit << ",\"value\":" << branch.value << '}';
    }
    result << "],\"history\":[";
    first = true;
    for (int i = 0; i < (int)details.history.size; ++i) {
        const SuggestResult::Details::Layer &layer = details.history.data[i];
        if (layer.cell != cell) continue;
        if (!first) result << ',';
        first = false;
        result << "{\"depth\":" << layer.depth << ",\"value\":" << layer.value << '}';
    }
    result << ']';
}

void Session::writeSuggestion(std::ostream &result, Analysis &analysis, const Config &config, const vector<ObservedBoard::CellId> &targets) {
    const FiftyFifty::Config fifty{config.fiftyFifty.algorithm, &config.fiftyFifty.oldJava, &config.fiftyFifty.templates};
    const MovePolicy::Config policy = config.policy(fifty);
    AnytimeSearch::Result searched;
    SuggestResult::Details details;
    const SuggestResult evaluated = MovePolicy().suggest(analysis.checked.result, analysis.positions, policy, analysis.suggestions,
                                                          targets, searched, details);
    result << std::setprecision(18);
    const bool safe = evaluated.engine == SuggestResult::Engine::Safe;
    const bool exact = evaluated.engine == SuggestResult::Engine::BruteForce;
    const char *engine = evaluated.engine == SuggestResult::Engine::None ? "Finished" : safe ? "Safe" : exact ? "BruteForce" : evaluated.engine == SuggestResult::Engine::LowRisk ? "LowRisk" : evaluated.engine == SuggestResult::Engine::JavaEval ? "JavaEval" : "Anytime";
    result << ",\"engine\":" << jsonString(engine) << ",\"reason\":" << jsonString(evaluated.reason == SuggestResult::Reason::Forced ? "forced5050" : evaluated.reason == SuggestResult::Reason::Semi ? "5050OrSafe" : "score") << ",\"nodes\":" << evaluated.nodes << ",\"recommended\":[";
    for (int i = 0; i < (int)evaluated.suggest.size; ++i) {
        if (i != 0) result << ',';
        result << evaluated.suggest[i];
    }
    result << "],\"candidates\":[";
    for (int i = 0; i < (int)evaluated.candidates.size; ++i) {
        if (i != 0) result << ',';
        result << evaluated.candidates[i].cell;
    }
    result << "],\"moves\":[";
    for (int i = 0; i < (int)evaluated.candidates.size; ++i) {
        if (i != 0) result << ',';
        const SuggestResult::Candidate &candidate = evaluated.candidates.data[i];
        result << "{\"cell\":" << candidate.cell << ",\"value\":" << candidate.value << ",\"probability\":" << analysis.mineProbability(candidate.cell);
        if (evaluated.engine == SuggestResult::Engine::Anytime) result << ",\"depth\":" << analysis.suggestions.anytime.moves.data[searched.roots.offset + i].expandedDepth;
        writeMoveDetails(result, details, candidate.cell);
        result << '}';
    }
    result << ']';
    analysis.suggestions.anytime.clear(analysis.positions);
}

std::string Session::analyze(const std::vector<FormField> &fields) {
    std::string response;
    const std::string idText = value(fields, "analysisId");
    if (idText.empty()) {
        std::unique_ptr<Analysis> next;
        if (!createAnalysis(fields, next, response))
            return response;
        analysis_.reset(next.release());
    } else {
        int id;
        if (!parseInteger(idText, id) || !analysis_ || id != analysis_->id)
            return error("计算上下文已失效");
    }
    Position::Result &position = analysis_->checked.result;
    Position::Pool &positions = analysis_->positions;
    const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
    std::ostringstream result;
    result << "{\"ok\":true,\"analysisId\":" << analysis_->id << ",\"candidateCount\":" << candidateCount(probability.totalWays);
    if (value(fields, "includeProbabilities") == "true")
        writeProbabilities(result, position, positions);
    if (value(fields, "suggest") == "true") {
        Config config = config_;
        if (!parseConfig(fields, config, response))
            return response;
        const std::string selected = value(fields, "targets");
        const ObservedBoard::Result &board = position.board;
        vector<ObservedBoard::CellId> targets;
        int previous = -1;
        for (std::size_t start = 0; start < selected.size();) {
            const std::size_t end = selected.find(',', start);
            int cell;
            if (!parseInteger(selected.substr(start, end - start), cell) || cell <= previous || cell >= board.cells.rows() * board.cells.cols())
                return error("G 格编号无效");
            const auto [row, col] = board.pos(cell);
            if (board.cells[row][col] != ObservedBoard::CellState::Hidden)
                return error("G 格必须是未揭格");
            if (analysis_->mineProbability(cell) == 1.0L)
                return error("G 格已确定为雷");
            targets.push_back(cell);
            previous = cell;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        writeSuggestion(result, *analysis_, config, targets);
    }
    const std::string cellText = value(fields, "cell");
    if (!cellText.empty()) {
        int cell;
        const ObservedBoard::Result &board = position.board;
        if (!parseInteger(cellText, cell) || cell < 0 || cell >= board.cells.rows() * board.cells.cols())
            return error("格子编号无效");
        const ObservedBoard::CellState state = board.cells[cell / board.cells.cols()][cell % board.cells.cols()];
        result << std::setprecision(18) << ",\"probability\":" << analysis_->mineProbability(cell);
        if (state < ObservedBoard::CellState::Hidden || state == ObservedBoard::CellState::ForcedMine) {
            result << ",\"observe\":null";
        } else {
            Observe::Result observed;
            Position().observe(position, probability, positions.probabilities, cell, observed);
            // 保留概率精度；前端只省略原值为 0 的结局，不能按六位小数舍入后的值筛选。
            result << std::setprecision(18) << ",\"observe\":[";
            for (int i = 0; i < 10; ++i) {
                if (i != 0)
                    result << ',';
                result << '"' << observed.probability[i] << '"';
            }
            result << ']';
        }
        if (value(fields, "risk") == "true") {
            if (state != ObservedBoard::CellState::Hidden || analysis_->mineProbability(cell) == 0.0L) result << ",\"longTermRisk\":null";
            else {
                Config config = config_;
                if (!parseFiftyFifty(fields, config, response)) return response;
                if (config.fiftyFifty.algorithm == FiftyFifty::Algorithm::OldJava) {
                    const LongTermRisk::Details risk = LongTermRisk().describe(cell, position, positions, probability, positions.probabilities, config.fiftyFifty.oldJava);
                    result << ",\"longTermRisk\":{\"algorithm\":\"oldjava\",\"body\":" << risk.body << ",\"restriction\":" << risk.restriction << '}';
                } else {
                    const FiftyFifty::Config fifty{FiftyFifty::Algorithm::Templates, nullptr, &config.fiftyFifty.templates};
                    FiftyFifty::Result risk;
                    FiftyFifty::Pool &pool = analysis_->suggestions.fiftyFifty;
                    FiftyFifty().prepareRisk(risk, position, positions, probability, positions.probabilities, fifty, pool);
                    const TemplateLongTermRisk::Details details = TemplateLongTermRisk().influence<TemplateLongTermRisk::DetailRequest::Details>(
                        cell, risk.templates, position, positions, probability, positions.probabilities, pool.templates);
                    result << ",\"longTermRisk\":{\"algorithm\":\"templates\",\"bodyUnion\":" << details.body << ",\"restrictionCounts\":[";
                    for (int k = 0; k < (int)details.restrictions.size; ++k) {
                        if (k != 0) result << ',';
                        result << details.restrictions[k];
                    }
                    result << "]}";
                }
            }
        }
    }
    result << '}';
    return result.str();
}

std::string Session::playUntilNoSafe(const std::vector<FormField> &fields) {
    std::string response;
    int rows, cols, totalMines;
    std::string cells;
    if (!parseBoard(fields, rows, cols, totalMines, cells, response))
        return response;
    ObservedBoard::Result board = makeBoard(rows, cols, totalMines, cells);
    ShapeSolverConfig::Order algo;
    if (!parseOrderAlgo(fields, algo))
        return error("ShapeSolver 算法无效");
    const std::string mines = value(fields, "minefield");
    if ((int)mines.size() != rows * cols || mines.find_first_not_of("01") != std::string::npos)
        return error("真实雷盘数据无效");
    Grid<char> grid(rows, cols, 0);
    int count = 0;
    for (int cell = 0; cell < rows * cols; ++cell) {
        grid[cell / cols][cell % cols] = mines[cell] - '0';
        count += grid[cell / cols][cell % cols];
    }
    if (count != board.totalMines)
        return error("真实雷盘与总雷数不符");
    Position::Config config = config_.game;
    config.shapeSolver.order = algo;
    Combination::Pool combinations;
    Game game(grid, ObservedBoard::Result(rows, cols, totalMines), config, combinations);
    vector<ObservedBoard::CellId> revealed;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const ObservedBoard::CellState state = board.cells[row][col];
            if (state == ObservedBoard::CellState::Hidden) continue;
            if (!ObservedBoard().isNumber(state) || ObservedBoard().numberValue(state) != game.number(col, row))
                return error("游玩观测与真实雷盘不符");
            revealed.push_back(board.id(row, col));
        }
    game.play(revealed.data, (int)revealed.size);
    game.playUntilNoSafe();
    const ObservedBoard::Result &played = game.position().board;
    std::ostringstream result;
    result << "{\"ok\":true,\"rows\":" << rows << ",\"cols\":" << cols << ",\"mines\":" << count << ",\"minefield\":" << jsonString(mines) << ",\"cells\":" << jsonString(cellString(played)) << ",\"lost\":" << (game.state() == Game::State::Lost ? "true" : "false") << ",\"won\":" << (game.state() == Game::State::Won ? "true" : "false");
    if (value(fields, "includeProbabilities") == "true" && game.state() == Game::State::Running) {
        Position::Pool positions(combinations);
        Position::Result position = Position().analyze(played, positions, config);
        const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
        result << ",\"candidateCount\":" << candidateCount(probability.totalWays);
        writeProbabilities(result, position, positions);
    }
    result << '}';
    return result.str();
}

std::string Session::mctsResult(const MonteCarlo::Result &snapshot, bool complete, long double candidates) const {
    const ObservedBoard::CellId recommended = snapshot.recommended();
    const bool exact = snapshot.exact.engine == SuggestResult::Engine::BruteForce;
    long double winrate = snapshot.samples == 0 ? 0 : (long double)snapshot.wins / snapshot.samples;
    if (exact) {
        for (int i = 0; i < (int)snapshot.exact.candidates.size; ++i)
            if (snapshot.exact.candidates.data[i].cell == recommended) winrate = snapshot.exact.candidates.data[i].value;
    } else if (!snapshot.winrate)
        for (int i = 0; i < (int)snapshot.candidates.size; ++i) {
            const MonteCarlo::Candidate &candidate = snapshot.candidates.data[i];
            if (candidate.cell == recommended)
                winrate = candidate.samples == 0 ? 0 : (long double)candidate.wins / candidate.samples;
        }
    std::ostringstream result;
    result << std::setprecision(12) << "{\"ok\":true,\"simulations\":" << snapshot.samples << ",\"value\":" << winrate << ",\"recommended\":" << recommended << ",\"winrate\":" << (snapshot.winrate ? "true" : "false") << ",\"exact\":" << (exact ? "true" : "false") << ",\"complete\":" << (complete ? "true" : "false") << ",\"candidateCount\":" << candidateCount(candidates) << ",\"moves\":[";
    bool first = true;
    if (exact) {
        for (int i = 0; i < (int)snapshot.exact.candidates.size; ++i) {
            const SuggestResult::Candidate &candidate = snapshot.exact.candidates.data[i];
            if (!first) result << ',';
            first = false;
            result << "{\"cell\":" << candidate.cell << ",\"winrate\":" << candidate.value << '}';
        }
    } else for (int i = 0; i < (int)snapshot.candidates.size; ++i) {
        const MonteCarlo::Candidate &candidate = snapshot.candidates.data[i];
        if (!first)
            result << ',';
        first = false;
        result << "{\"cell\":" << candidate.cell << ",\"visits\":" << jsonString(std::to_string(candidate.samples)) << ",\"wins\":" << jsonString(std::to_string(candidate.wins)) << ",\"winrate\":";
        if (candidate.samples == 0) result << "null";
        else result << (long double)candidate.wins / candidate.samples;
        result << '}';
    }
    result << "]}";
    return result.str();
}

bool Session::startMcts(const std::string &body, std::string &response) {
#ifndef __EMSCRIPTEN__
    mctsWorker_.request_stop();
    if (mctsWorker_.joinable())
        mctsWorker_.join();
#endif
    const std::vector<FormField> fields = parseForm(body);
    Config config = config_;
    if (!parseConfig(fields, config, response)) return false;
    std::unique_ptr<Analysis> analysis;
    if (!createAnalysis(fields, analysis, response))
        return false;
    vector<ObservedBoard::CellId> targets;
    const std::string cells = value(fields, "cells");
    const ObservedBoard::Result &board = analysis->checked.result.board;
    int revealed = 0;
    for (int cell = 0; cell < (int)cells.size(); ++cell) {
        if (cells[cell] == 'G')
            targets.push_back(cell);
        if (cells[cell] >= '0' && cells[cell] <= '8')
            ++revealed;
    }
    const bool complete = revealed + board.totalMines == (int)cells.size();
    if (!complete && targets.size != 0) {
        bool safe = false;
        for (int cell = 0; cell < (int)cells.size(); ++cell)
            if ((cells[cell] == '?' || cells[cell] == 'G' || cells[cell] == 'S') && analysis->mineProbability(cell) == 0.0L) {
                safe = true;
                break;
            }
        // 有必安格时 MC 忽略目标并采整局胜率；仅在实际比较首招时检查外部目标。
        if (!safe)
            for (int i = 0; i < (int)targets.size; ++i)
                if (analysis->mineProbability(targets.data[i]) == 1.0L) {
                    response = error("MC 目标格已确定为雷");
                    return false;
                }
    }
#ifdef __EMSCRIPTEN__
    mctsTask_.reset(new MctsTask(analysis, config, targets, complete));
#else
    {
        const std::lock_guard<std::mutex> lock(mctsMutex_);
        mctsUpdate_ = "{\"ok\":true,\"simulations\":0,\"value\":0,\"recommended\":-1,\"winrate\":false,\"exact\":false,\"complete\":false,\"moves\":[]}";
    }
    Analysis *source = analysis.release();
    mctsWorker_ = std::jthread([this, source, config, targets, complete](std::stop_token stop) {
        std::unique_ptr<Analysis> analysis(source);
        MctsTask task(analysis, config, targets, complete);
        auto publish = [&]() {
            const std::string update = mctsResult(task.result, task.complete, task.layouts);
            const std::lock_guard<std::mutex> lock(mctsMutex_);
            mctsUpdate_ = update;
        };
        publish();
        if (task.finished()) return;
        std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (!stop.stop_requested()) {
            task.step();
            if (task.finished()) break;
            if (std::chrono::steady_clock::now() >= next) {
                publish();
                next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            }
        }
        publish();
    });
#endif
    return true;
}

#ifndef __EMSCRIPTEN__
std::string Session::mctsUpdate() const {
    const std::lock_guard<std::mutex> lock(mctsMutex_);
    return mctsUpdate_;
}
#else
std::string Session::advanceMcts() {
    MctsTask &task = *mctsTask_;
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    while (!task.finished()) {
        task.step();
        if (std::chrono::steady_clock::now() >= end) break;
    }
    return mctsResult(task.result, task.complete, task.layouts);
}
#endif

bool Session::startSearch(const std::string &body, std::string &response) {
#ifndef __EMSCRIPTEN__
    searchWorker_.request_stop();
    if (searchWorker_.joinable()) searchWorker_.join();
#endif
    const std::vector<FormField> fields = parseForm(body);
    std::unique_ptr<Analysis> analysis;
    if (!createAnalysis(fields, analysis, response)) return false;
    vector<ObservedBoard::CellId> targets;
    const std::string cells = value(fields, "cells");
    for (int cell = 0; cell < (int)cells.size(); ++cell)
        if (cells[cell] == 'G') targets.push_back(cell);
    vector<ObservedBoard::CellId> safe;
    for (int cell = 0; cell < (int)cells.size(); ++cell)
        if ((cells[cell] == '?' || cells[cell] == 'G' || cells[cell] == 'S') && analysis->mineProbability(cell) == 0.0L)
            safe.push_back(cell);
    if (safe.size == 0)
        for (int i = 0; i < (int)targets.size; ++i)
            if (analysis->mineProbability(targets[i]) == 1.0L) { response = error("Search 目标格已确定为雷"); return false; }
    Config config = config_;
    if (!parseConfig(fields, config, response)) return false;
    if (config.algorithm != MovePolicy::Algorithm::Anytime) { response = error("搜索仅支持 Anytime"); return false; }
    config.anytime.nodeLimit = 0;
#ifdef __EMSCRIPTEN__
    searchBudget_ = 0;
    searchRunning_ = value(fields, "run") == "true";
    searchTask_.reset(new SearchTask(analysis, config, targets, safe));
#else
    {
        const std::lock_guard<std::mutex> lock(searchMutex_);
        searchBudget_ = 0;
        searchRunning_ = value(fields, "run") == "true";
        searchUpdate_ = "{\"ok\":true,\"nodes\":0,\"completedDepth\":0,\"finished\":false,\"safe\":false,\"recommended\":-1,\"moves\":[]";
    }
    Analysis *source = analysis.release();
    searchWorker_ = std::jthread([this, source, config, targets, safe](std::stop_token stop) {
        std::unique_ptr<Analysis> analysis(source);
        SearchTask task(analysis, config, targets, safe);
        auto publish = [&]() {
            const std::string update = searchResult(task);
            const std::lock_guard<std::mutex> lock(searchMutex_);
            searchUpdate_ = update;
        };
        publish();
        constexpr long long nodesPerUpdate = 5000;
        while (!task.result.finished && !stop.stop_requested()) {
            bool running;
            long long budget;
            unsigned long long epoch;
            {
                std::unique_lock<std::mutex> lock(searchMutex_);
                if (!searchWake_.wait(lock, stop, [&]() { return searchRunning_ || searchBudget_ > 0; })) break;
                running = searchRunning_;
                budget = running ? nodesPerUpdate : (std::min)(searchBudget_, nodesPerUpdate);
                epoch = searchBudgetEpoch_;
            }
            const long long spent = task.step(budget);
            if (!running) {
                const std::lock_guard<std::mutex> lock(searchMutex_);
                if (epoch == searchBudgetEpoch_)
                    searchBudget_ = task.result.finished ? 0 : (std::max)(0LL, searchBudget_ - spent);
            }
            publish();
        }
    });
#endif
    return true;
}

std::string Session::searchResult(const SearchTask &task) const {
    const AnytimeSearch::Pool &pool = task.analysis->suggestions.anytime;
    const AnytimeSearch::Result &result = task.result;
    const SuggestResult &initialized = task.initialized;
    const vector<ObservedBoard::CellId> &safe = task.safe;
    const bool exact = initialized.engine == SuggestResult::Engine::BruteForce;
    const SuggestResult snapshot = result.snapshot(pool);
    const ObservedBoard::CellId recommended = safe.size != 0 ? safe.data[0] : exact ? (initialized.suggest.size != 0 ? initialized.suggest.data[0] : -1) : result.recommended(pool);
    std::ostringstream json;
    json << std::setprecision(18) << "{\"ok\":true,\"engine\":" << jsonString(exact ? "BruteForce" : safe.size != 0 ? "Safe" : "Anytime") << ",\"candidateCount\":" << candidateCount(task.layouts) << ",\"nodes\":" << result.nodes << ",\"completedDepth\":" << result.completedDepth << ",\"finished\":" << (result.finished ? "true" : "false") << ",\"safe\":" << (safe.size != 0 ? "true" : "false") << ",\"recommended\":" << recommended << ",\"moves\":[";
    const int count = safe.size != 0 ? (int)safe.size : exact ? (int)initialized.candidates.size : (int)result.roots.count;
    for (int i = 0; i < count; ++i) {
        if (i != 0) json << ',';
        if (safe.size != 0) {
            json << "{\"cell\":" << safe.data[i] << ",\"depth\":0,\"value\":1,\"history\":[]}";
            continue;
        }
        if (exact) {
            const SuggestResult::Candidate &move = initialized.candidates.data[i];
            json << "{\"cell\":" << move.cell << ",\"depth\":0,\"value\":" << move.value << ",\"history\":[]}";
            continue;
        }
        const AnytimeSearch::Pool::Move &candidate = pool.moves.data[result.roots.offset + i];
        json << "{\"cell\":" << candidate.cell << ",\"depth\":" << candidate.expandedDepth << ",\"value\":" << snapshot.candidates[i].value;
        writeMoveDetails(json, task.details, candidate.cell);
        json << '}';
    }
    json << ']';
    return json.str();
}

#ifdef __EMSCRIPTEN__
std::string Session::advanceSearch() {
    SearchTask &task = *searchTask_;
    constexpr long long batch = 256;
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!task.result.finished && (searchRunning_ || searchBudget_ > 0)) {
        const long long spent = task.step(searchRunning_ ? batch : (std::min)(searchBudget_, batch));
        if (!searchRunning_) searchBudget_ = task.result.finished ? 0 : (std::max)(0LL, searchBudget_ - spent);
        if (std::chrono::steady_clock::now() >= end) break;
    }
    return searchResult(task) + ",\"paused\":" + (!searchRunning_ && searchBudget_ == 0 ? "true" : "false") + '}';
}
#endif

std::string Session::controlSearch(const std::vector<FormField> &fields) {
    const std::string action = value(fields, "action");
    int nodes = 0;
    if (action == "step" && (!parseInteger(value(fields, "nodes"), nodes) || nodes < 1)) return error("节点预算必须是正整数");
    if (action != "pause" && action != "resume" && action != "step") return error("搜索操作无效");
#ifndef __EMSCRIPTEN__
    const std::lock_guard<std::mutex> lock(searchMutex_);
#endif
    searchRunning_ = action == "resume";
    if (action == "step") searchBudget_ += nodes;
    else {
        searchBudget_ = 0;
#ifndef __EMSCRIPTEN__
        ++searchBudgetEpoch_;
#endif
    }
#ifndef __EMSCRIPTEN__
    searchWake_.notify_one();
#endif
    return "{\"ok\":true}";
}

void Session::stopAnalysis() {
#ifdef __EMSCRIPTEN__
    mctsTask_.reset();
    searchTask_.reset();
#else
    mctsWorker_.request_stop();
    searchWorker_.request_stop();
#endif
}

#ifndef __EMSCRIPTEN__
std::string Session::searchUpdate() const {
    const std::lock_guard<std::mutex> lock(searchMutex_);
    // 缓存保存计算结果前缀；暂停状态读取时补齐，控制操作无需等待下一次计算发布。
    return searchUpdate_ + ",\"paused\":" + (!searchRunning_ && searchBudget_ == 0 ? "true" : "false") + '}';
}

#endif

std::string Session::handle(const std::string &path, const std::string &body) {
#ifdef __EMSCRIPTEN__
    if (path == "/api/anytime-search/update") return advanceSearch();
    if (path == "/api/mcts/update") return advanceMcts();
    if (path == "/api/anytime-search/start" || path == "/api/mcts/start") {
        stopAnalysis();
        std::string response;
        const bool started = path == "/api/anytime-search/start" ? startSearch(body, response) : startMcts(body, response);
        return started ? "{\"ok\":true}" : response;
    }
    if (path == "/api/anytime-search/stop" || path == "/api/mcts/stop") {
        stopAnalysis();
        return "{\"ok\":true}";
    }
#endif
    const std::vector<FormField> fields = parseForm(body);
    if (path == "/api/analyze")
        return analyze(fields);
    if (path == "/api/play-until-no-safe")
        return playUntilNoSafe(fields);
    if (path == "/api/anytime-search/control")
        return controlSearch(fields);
    if (path == "/api/quit") {
        stopped_ = true;
        stopAnalysis();
        return "{\"ok\":true}";
    }
    return error("未知接口");
}

} // 命名空间 mss::UI

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(mss) {
    emscripten::class_<mss::UI::Session>("Session")
        .constructor<>()
        .function("index", &mss::UI::Session::index)
        .function("handle", &mss::UI::Session::handle);
}
#endif
