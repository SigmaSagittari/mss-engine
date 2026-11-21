#include "ui/internal/session.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#endif

#include "algo/ioe_engine/bruteforce/bruteforce.h"
#include "algo/ioe_engine/eight_way/eight_way.h"
#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"
#include "core/algorithm/rng.h"
#include "core/config.h"
#include "core/assert.h"

namespace mss::UI {

struct Session::FormField {
    std::string key;
    std::string value;
};

struct Session::Workspace {
    Grid<char> layout;
    vector<int> effOpening;
};

__declspec(noinline) Session::Workspace &Session::getWorkspace() {
    static thread_local Workspace workspace;
    return workspace;
}

// 一次计算拥有固定盘面及其结果、缓存；后续查询只引用 id，不接受盘面修改。
struct Session::Analysis {
    int id;
    Suggest::Pool suggestions;
    Position::Pool positions;
    Position::CheckedResult checked;

    Analysis(int id, const Position::Config &config, const ObservedBoard::Result &board)
        : id(id), positions(suggestions.combinations), checked(Position().analyzeChecked(board, positions, config, positions.probabilities)) {}
    ~Analysis() { suggestions.clear(positions); }

    long double mineProbability(ObservedBoard::CellId cell) {
        Position::Result &position = checked.result;
        const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
        return Probability().mineProbability(probability.boxes, positions.probabilities, cell, position.board, position.basic, position.structure);
    }
};

struct Session::SearchTask {
    std::unique_ptr<Analysis> analysis;
    AnytimeSearch::Config config;
    const long long nodeLimit;
    vector<ObservedBoard::CellId> safe;
    long double layouts;
    AnytimeSearch::Result result;
    SuggestResult::Details details;
    SuggestResult initialized;

    SearchTask(std::unique_ptr<Analysis> &source, const Config &settings, const vector<ObservedBoard::CellId> &targets,
               const vector<ObservedBoard::CellId> &safe)
        : analysis(source.release()), config(settings.anytime), nodeLimit(settings.anytime.nodeLimit), safe(safe),
          layouts(Position().analyzeProbability(analysis->checked.result, analysis->positions).totalWays),
          initialized(initialize(settings, targets)) {
        if (initialized.engine == SuggestResult::Engine::BruteForce) result.nodes = initialized.nodes;
    }

    SuggestResult initialize(const Config &settings, const vector<ObservedBoard::CellId> &targets) {
        config.nodeLimit = 0;
        const MovePolicy::Config::Guess guess{settings.anytime.selection.filter, settings.anytime.selection.exchange, settings.longTermRisk,
                                             {settings.fiftyFifty.prefix, settings.longTermRisk ? &settings.fiftyFifty.eventLimit : nullptr}, &config};
        const MovePolicy::Config policy{MovePolicy::Algorithm::Anytime, &guess, settings.brute.limit != 0 ? &settings.brute : nullptr};
        return MovePolicy().suggest(analysis->checked.result, analysis->positions, policy, analysis->suggestions, targets, result, details);
    }

    bool limitReached() const {
        return !result.finished && nodeLimit != -1 && result.nodes >= nodeLimit;
    }

    void step(long long batch) {
        config.nodeLimit = nodeLimit == -1 ? batch : (std::min)(nodeLimit - result.nodes, batch);
        AnytimeSearch().update(result, analysis->checked.result, analysis->positions, config, analysis->suggestions.anytime, details);
    }
};

Session::Session()
#ifdef __EMSCRIPTEN__
    : nextAnalysis_(1), stopped_(false), config_(false), searchRunning_(false) {}
#else
    : nextAnalysis_(1), stopped_(false), token_(makeToken()), config_(true), searchRunning_(false) {}
#endif

Session::~Session() = default;

std::string Session::index(std::string content) const {
    const Config &config = config_;
    auto replace = [&](const std::string &marker, const std::string &value) {
        content.replace(content.find(marker), marker.size(), value);
    };
    replace("__MSS_TOKEN__", token_);
    constexpr const char *widths[] = {"__MSS_SEARCH_ROOT_WIDTH__", "__MSS_SEARCH_MIDDLE_WIDTH__", "__MSS_SEARCH_DEEP_WIDTH__"};
    for (int i = 0; i < 3; ++i) replace(widths[i], std::to_string(config.anytime.width[i]));
    replace("__MSS_TEMPLATE_PREFIX__", std::to_string(config.fiftyFifty.prefix));
    replace("__MSS_TEMPLATE_COUNT__", std::to_string(FiftyFiftyData().count()));
    replace("__MSS_LTR_EVENT_LIMIT__", std::to_string(config.fiftyFifty.eventLimit));
    replace("__MSS_BRUTE_FORCE_THRESHOLD__", std::to_string(config.brute.limit));
    replace("__MSS_THREAD_LIMIT__", std::to_string(config.brute.config.threadCount));
    replace("__MSS_BRUTE_FORCE_CHECK_ALL__", config.brute.config.checkAllMoves ? "checked" : "");
    replace("__MSS_BRUTE_FORCE_MULTITHREAD__", config.brute.config.threadCount > 1 ? "checked" : "");
    replace("__MSS_POLICY_NODES__", std::to_string(config.anytime.nodeLimit));
    const std::string buildTimeMarker = "__MSS_BUILD_TIME__";
    if (content.find(buildTimeMarker) != std::string::npos) replace(buildTimeMarker, __DATE__ " " __TIME__);
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

std::string Session::error(Error code) const {
    return "{\"ok\":false,\"errorCode\":" + std::to_string((int)code) + "}";
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
    int prefix;
    if (!parseInteger(value(fields, "templatePrefix"), prefix) || prefix < 1 || prefix > FiftyFiftyData().count()) { response = error(Error::TemplatePrefix); return false; }
    if (config.longTermRisk && (!parseInteger(value(fields, "ltrEventLimit"), config.fiftyFifty.eventLimit) || config.fiftyFifty.eventLimit < 1)) {
        response = error(Error::LtrEventLimit);
        return false;
    }
    config.fiftyFifty.prefix = prefix;
    return true;
}

bool Session::parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response) {
    if (!parseInteger(value(fields, "limit"), config.brute.limit) || config.brute.limit < 0) { response = error(Error::BruteLimit); return false; }
    if (config.brute.limit != 0) {
#ifndef __EMSCRIPTEN__
        if (value(fields, "multiThread") != "true") config.brute.config.threadCount = 1;
        else if (!parseInteger(value(fields, "maxThreads"), config.brute.config.threadCount) || config.brute.config.threadCount < 1) {
            response = error(Error::ThreadLimit);
            return false;
        }
#endif
        config.brute.config.checkAllMoves = value(fields, "checkAllMoves") == "true";
    }
    int nodes;
    if (!parseInteger(value(fields, "nodeBudget"), nodes) || (nodes != -1 && nodes < 1)) { response = error(Error::NodeLimit); return false; }
    config.anytime.nodeLimit = nodes;
    const char *names[] = {"rootWidth", "middleWidth", "deepWidth"};
    for (int i = 0; i < 3; ++i) if (!parseInteger(value(fields, names[i]), config.anytime.width[i]) || config.anytime.width[i] < 1) { response = error(Error::SearchWidth); return false; }
    return parseFiftyFifty(fields, config, response);
}

Session::Error Session::parseBoardParameters(const std::vector<FormField> &fields, int &rows, int &cols, int &mines) const {
    if (!parseInteger(value(fields, "rows"), rows) || !parseInteger(value(fields, "cols"), cols) || !parseInteger(value(fields, "mines"), mines))
        return Error::BoardParameters;
    if (rows < 1 || cols < 1 || rows > 100 || cols > 100 || mines < 0 || mines >= rows * cols)
        return Error::BoardRange;
    return Error::None;
}

bool Session::parseBoard(const std::vector<FormField> &fields, int &rows, int &cols, int &mines, std::string &cells, std::string &response) {
    const Error parameters = parseBoardParameters(fields, rows, cols, mines);
    if (parameters != Error::None) {
        response = error(parameters);
        return false;
    }
    cells = value(fields, "cells");
    if ((int)cells.size() != rows * cols || cells.find_first_not_of("012345678?MSG") != std::string::npos) {
        response = error(Error::BoardCells);
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
        response = error(Error::SolverOrder);
        return false;
    }
    int rows, cols, mines;
    std::string cells;
    if (!parseBoard(fields, rows, cols, mines, cells, response))
        return false;
    Position::Config config = config_.game;
    config.shapeSolver.order = algo;
    config.shapeSolver.sa = algo == ShapeSolverConfig::Order::AutoSA ? &config_.annealing : nullptr;
    analysis.reset(new Analysis(nextAnalysis_++, config, makeBoard(rows, cols, mines, cells)));
    // 人工快照的局部检查和全盘存在性检查由 Position 构造完成，只在输入边界读取一次。
    const Position::CheckError checked = analysis->checked.error;
    if (checked != Position::CheckError::None) {
        response = error(checked == Position::CheckError::BasicConflict ? Error::BoardLogicConflict :
                         checked == Position::CheckError::MineCountConflict ? Error::BoardMineCountConflict :
                         Error::BoardComponentConflict);
        return false;
    }
    return true;
}

std::string Session::candidateCount(long double layouts) const {
    std::ostringstream count;
    count << std::scientific << std::setprecision(6) << layouts;
    return jsonString(count.str());
}

void Session::writeProbabilities(std::ostream &result, const Position::Result &position, const Probability::BoxResult &boxes, const Probability::Pool &probabilities) const {
    const ObservedBoard::Result &board = position.board;
    result << std::setprecision(18) << ",\"probabilities\":[";
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col) {
            if (row != 0 || col != 0)
                result << ',';
            result << Probability().mineProbability(boxes, probabilities, board.id(row, col), board, position.basic, position.structure);
        }
    result << ']';
}

void Session::writeHighlight(std::ostream &result, Position::Result &position, Position::Pool &positions, const std::string &highlight) const {
    if (highlight != "structureInstances" && highlight != "logicComponents") return;
    const Structure::Pool::Instances &instances = positions.structure.instances;
    auto writeCell = [&](ObservedBoard::CellId cell, bool &first) {
        if (!first) result << ',';
        first = false;
        result << cell;
    };
    auto writeInstance = [&](Structure::InstanceId id, bool &first) {
        const Structure::Instance &instance = instances.list[id];
        for (const vectorPool<ObservedBoard::CellId>::vector &box : instances.boxes.span(instance.boxes))
            for (ObservedBoard::CellId cell : instances.cells.span(box))
                writeCell(cell, first);
        for (ObservedBoard::CellId cell : instances.constraintCells.span(instance.constraintCells))
            writeCell(cell, first);
    };
    result << ",\"" << highlight << "\":[";
    if (highlight == "structureInstances") {
        for (int i = 0; i < (int)position.structure.instanceIds.size; ++i) {
            if (i != 0) result << ',';
            result << '[';
            bool first = true;
            writeInstance(position.structure.instanceIds[i], first);
            result << ']';
        }
    } else {
        const Position::LogicComponentResult &components = Position().analyzeLogicComponents(position, positions);
        for (int i = 0; i < (int)components.result.components.size; ++i) {
            const LogicComponent::Result::Component &component = components.result.components[i];
            if (i != 0) result << ',';
            result << '[';
            bool first = true;
            for (Structure::InstanceId id : components.pool.instanceIds.span(component.instanceIds))
                writeInstance(id, first);
            for (ObservedBoard::CellId cell : components.pool.tCells.span(component.tCells))
                writeCell(cell, first);
            result << ']';
        }
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
            return error(Error::AnalysisExpired);
    }
    Position::Result &position = analysis_->checked.result;
    Position::Pool &positions = analysis_->positions;
    std::ostringstream result;
    result << "{\"ok\":true,\"analysisId\":" << analysis_->id;
    const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
    result << ",\"candidateCount\":" << candidateCount(probability.totalWays);
    if (value(fields, "includeProbabilities") == "true")
        writeProbabilities(result, position, probability.boxes, positions.probabilities);
    writeHighlight(result, position, positions, value(fields, "highlight"));
    const std::string cellText = value(fields, "cell");
    if (!cellText.empty()) {
        int cell;
        const ObservedBoard::Result &board = position.board;
        if (!parseInteger(cellText, cell) || cell < 0 || cell >= board.cells.rows() * board.cells.cols())
            return error(Error::CellIndex);
        const ObservedBoard::CellState state = board.cells[cell / board.cells.cols()][cell % board.cells.cols()];
        const long double mine = Probability().mineProbability(probability.boxes, positions.probabilities, cell, board, position.basic, position.structure);
        result << std::setprecision(18) << ",\"probability\":" << mine;
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
            if (state != ObservedBoard::CellState::Hidden || mine == 0.0L) result << ",\"longTermRisk\":null";
            else {
                Config config = config_;
                if (!parseFiftyFifty(fields, config, response)) return response;
                const FiftyFifty::Config fifty{config.fiftyFifty.prefix, &config.fiftyFifty.eventLimit};
                TemplateLongTermRisk::Result risk;
                FiftyFifty::Pool &pool = analysis_->suggestions.fiftyFifty;
                const FiftyFiftyData::Pool &table = FiftyFifty().prepareRisk(risk, position, positions, probability, positions.probabilities, fifty, pool);
                const TemplateLongTermRisk::Details details = TemplateLongTermRisk().influence<TemplateLongTermRisk::DetailRequest::Details>(
                    cell, risk, position, positions, probability, positions.probabilities, table);
                result << ",\"longTermRisk\":{\"algorithm\":\"templates\",\"bodyUnion\":" << details.body << ",\"restrictionCounts\":[";
                for (int k = 0; k < (int)details.restrictions.size; ++k) {
                    if (k != 0) result << ',';
                    result << details.restrictions[k];
                }
                result << "]}";
            }
        }
        const std::string group = value(fields, "groupDetail");
        if (group == "structureInstances" || group == "logicComponents") {
            vector<Position::MineCount> counts;
            const Position::Group type = group == "structureInstances" ? Position::Group::StructureInstance : Position::Group::LogicComponent;
            Position().groupMineCounts(position, positions, cell, type, counts);
            result << ",\"groupMineCounts\":[";
            for (int i = 0; i < (int)counts.size; ++i) {
                if (i != 0) result << ',';
                const Position::MineCount &count = counts[i];
                result << "{\"mines\":" << count.mines << ",\"ways\":\"" << count.ways << "\",\"probability\":\"" << count.probability << "\"}";
            }
            result << ']';
        }
    }
    result << '}';
    return result.str();
}

std::string Session::analyzeIoe(const std::string &body, const std::function<void(const std::string &)> &publish) {
    const std::vector<FormField> fields = parseForm(body);
    std::string response;
    int rows, cols, mines;
    std::string cells;
    if (!parseBoard(fields, rows, cols, mines, cells, response))
        return response;
    if (rows != 9 || cols != 9 || mines != 10)
        return error(Error::IoeBeginner);
    int clicks;
    if (!parseInteger(value(fields, "clicks"), clicks) || clicks < 0)
        return error(Error::IoeClicks);
    if (clicks == 0 && cells.find_first_of("012345678") != std::string::npos)
        return error(Error::IoeMissingClicks);
    const std::string mask = value(fields, "playerFlags");
    if (mask.size() != cells.size() || mask.find_first_not_of("01") != std::string::npos)
        return error(Error::PlayerFlags);
    int candidateLimit = 1'000'000;
    int domLimit = 1'000'000;
    const std::string candidateLimitText = value(fields, "candidateLimit");
    const std::string domLimitText = value(fields, "domLimit");
    if ((!candidateLimitText.empty() && !parseInteger(candidateLimitText, candidateLimit)) || candidateLimit < 1)
        return error(Error::IoeCandidateLimit);
    if ((!domLimitText.empty() && !parseInteger(domLimitText, domLimit)) || domLimit < 1)
        return error(Error::IoeDomLimit);
    Grid<bool> playerFlags(rows, cols, false);
    for (int cell = 0; cell < rows * cols; ++cell) {
        if (mask[cell] == '1' && cells[cell] >= '0' && cells[cell] <= '8')
            return error(Error::IoeFlagOnOpen);
        playerFlags[cell / cols][cell % cols] = mask[cell] == '1';
    }
    std::function<void(const IoeBruteForce::Progress &)> progress;
    if (publish) progress = [&](const IoeBruteForce::Progress &state) {
        std::ostringstream update;
        update << "{\"progress\":{\"phase\":" << (int)state.phase
               << ",\"candidateCount\":" << state.candidates << ",\"enumerated\":" << state.enumerated
               << ",\"rootAnalyzed\":" << state.rootAnalyzed << ",\"rootEligible\":" << state.rootEligible
               << ",\"rootDomCalls\":" << state.rootDomCalls << ",\"searchDomCalls\":" << state.searchDomCalls
               << ",\"domLimit\":" << state.domLimit << "}}";
        publish(update.str());
    };
    const IoeBruteForce::Result analysis = IoeBruteForce().solve(makeBoard(rows, cols, mines, cells), playerFlags, clicks, candidateLimit, domLimit, progress);
    if (analysis.status == IoeBruteForce::Status::InvalidBoard)
        return error(Error::IoeUnsolvable);
    std::ostringstream result;
    result << std::setprecision(18) << "{\"ok\":true,\"status\":" << (int)analysis.status
           << ",\"candidateCount\":" << analysis.candidates
           << ",\"rootEligible\":" << analysis.rootEligible << ",\"wins\":" << analysis.wins
           << ",\"rootDomCalls\":" << analysis.rootDomCalls << ",\"searchDomCalls\":" << analysis.searchDomCalls
           << ",\"expectedScore\":";
    if (std::isfinite(analysis.expectedScore)) result << analysis.expectedScore;
    else result << "null";
    result << ",\"winningIoe\":[";
    for (size_t i = 0; i < analysis.ioeDistribution.size; ++i) {
        if (i != 0) result << ',';
        const IoeBruteForce::Result::IoeCount &entry = analysis.ioeDistribution[i];
        result << '[' << entry.numerator << ',' << entry.denominator << ',' << entry.won << ',' << entry.discarded << ']';
    }
    result << "],\"move\":";
    if (analysis.action == IoeBruteForce::Action::None) {
        result << "null";
    } else {
        result << "{\"kind\":" << (int)analysis.action << ",\"cell\":" << analysis.cell << ",\"addFlags\":[";
        for (int i = 0; i < analysis.addedCount; ++i) {
            if (i != 0) result << ',';
            result << analysis.addedFlags[i];
        }
        result << "],\"removeFlags\":[";
        for (int i = 0; i < analysis.removedCount; ++i) {
            if (i != 0) result << ',';
            result << analysis.removedFlags[i];
        }
        result << "]}";
    }
    result << '}';
    return result.str();
}

bool Session::parseSeed(const std::string &value, U128 &seed) const {
    return value.size() == 32 &&
           std::from_chars(value.data(), value.data() + 16, seed.hi, 16).ptr == value.data() + 16 &&
           std::from_chars(value.data() + 16, value.data() + 32, seed.lo, 16).ptr == value.data() + 32;
}

void Session::writeSeed(std::ostream &result, U128 seed) const {
    result << '"' << std::hex << std::setfill('0') << std::setw(16) << seed.hi
           << std::setw(16) << seed.lo << std::dec << '"';
}

void Session::writeMinefield(std::ostream &result, const Grid<char> &layout) const {
    result << '"';
    const int count = layout.rows() * layout.cols();
    for (int cell = 0; cell < count; ++cell)
        result << (char) ('0' + layout[0][cell]);
    result << '"';
}

std::string Session::generateBoard(const std::vector<FormField> &fields) {
    int rows, cols, mines, first;
    const Error parameters = parseBoardParameters(fields, rows, cols, mines);
    if (parameters != Error::None) return error(parameters);
    if (!parseInteger(value(fields, "first"), first) || first < 0 || first >= rows * cols)
        return error(Error::CellIndex);
    U128 initial;
    if (!parseSeed(value(fields, "seed"), initial)) return error(Error::Seed);

    Workspace &workspace = getWorkspace();
    Grid<char> &layout = workspace.layout;
    layout.resize(rows, cols, 0);
    Minefield minefield;
    const U128 seed = minefield.safeSeed(rows * cols, mines, initial, first);
    Random random(seed.lo, seed.hi);
    minefield.generate(layout, mines, random);
    const U128 nextSeed = {random.next(), random.next()};

    std::ostringstream result;
    result << "{\"ok\":true,\"seed\":";
    writeSeed(result, seed);
    result << ",\"nextSeed\":";
    writeSeed(result, nextSeed);
    result << ",\"minefield\":";
    writeMinefield(result, layout);
    result << '}';
    return result.str();
}

std::string Session::generateEffBoard(const std::vector<FormField> &fields) {
    int rows, cols, mines;
    if (parseBoardParameters(fields, rows, cols, mines) != Error::None)
        return error(Error::BoardParameters);
    const int size = rows * cols;
    // 角上的零格及其邻格占用的安全格最少。
    if (size - mines < (std::min)(rows, 2) * (std::min)(cols, 2))
        return error(Error::BoardRange);

    const std::string input = value(fields, "zne");
    char *end;
    const double threshold = std::strtod(input.c_str(), &end);
    if (end == input.c_str() || end != input.c_str() + input.size() || !std::isfinite(threshold) || threshold <= 0)
        return error(Error::EffThreshold);

    int domExtraClicks = 0;
    if (value(fields, "dom") == "true") {
        const std::string level = value(fields, "difficulty");
        if (level == "beginner" && rows == 9 && cols == 9 && mines == 10) domExtraClicks = 2;
        else if (level == "intermediate" && rows == 16 && cols == 16 && mines == 40) domExtraClicks = 4;
        else if (level == "advanced" && rows == 16 && cols == 30 && mines == 99) domExtraClicks = 9;
    }

    U128 seed;
    if (!parseSeed(value(fields, "seed"), seed)) return error(Error::Seed);

    Workspace &workspace = getWorkspace();
    Grid<char> &layout = workspace.layout;
    vector<int> &opening = workspace.effOpening;
    layout.resize(rows, cols, 0);
    Minefield minefield;
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);
    int attempts = 0, domCalls = 0;
    U128 boardSeed = seed;
    EightWay::Filter status;
    for (;;) {
        Random random(boardSeed.lo, boardSeed.hi);
        minefield.generate(layout, mines, random);
        seed.lo = random.next();
        seed.hi = random.next();
        status = EightWay().filterOpening(layout, threshold, domExtraClicks, opening);
        ++attempts;
        domCalls += status.ranDom;
        if (status.accepted || std::chrono::steady_clock::now() >= deadline) break;
        layout.fill(0);
        boardSeed = seed;
    }

    std::ostringstream result;
    result << "{\"ok\":true,\"attempts\":" << attempts << ",\"domCalls\":" << domCalls << ",\"nextSeed\":";
    writeSeed(result, seed);
    if (status.accepted) {
        result << ",\"seed\":";
        writeSeed(result, boardSeed);
        result << ",\"minefield\":";
        writeMinefield(result, layout);
        result << ",\"opening\":[";
        for (std::size_t index = 0; index < opening.size; ++index) {
            if (index != 0) result << ',';
            result << opening[index];
        }
        result << ']';
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
        return error(Error::SolverOrder);
    const std::string mines = value(fields, "minefield");
    if ((int)mines.size() != rows * cols || mines.find_first_not_of("01") != std::string::npos)
        return error(Error::Minefield);
    Grid<char> grid(rows, cols, 0);
    int count = 0;
    for (int cell = 0; cell < rows * cols; ++cell) {
        grid[cell / cols][cell % cols] = mines[cell] - '0';
        count += grid[cell / cols][cell % cols];
    }
    if (count != board.totalMines)
        return error(Error::MineCount);
    Position::Config config = config_.game;
    config.shapeSolver.order = algo;
    config.shapeSolver.sa = algo == ShapeSolverConfig::Order::AutoSA ? &config_.annealing : nullptr;
    Combination::Pool combinations;
    Game game(grid, ObservedBoard::Result(rows, cols, totalMines), config, combinations);
    vector<ObservedBoard::CellId> revealed;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const ObservedBoard::CellState state = board.cells[row][col];
            if (state == ObservedBoard::CellState::Hidden) continue;
            if (!ObservedBoard().isNumber(state) || ObservedBoard().numberValue(state) != game.number(col, row))
                return error(Error::BoardMismatch);
            revealed.push_back(board.id(row, col));
        }
    game.play(revealed.data, (int)revealed.size);
    game.playUntilNoSafe();
    const ObservedBoard::Result &played = game.position().board;
    std::ostringstream result;
    result << "{\"ok\":true,\"rows\":" << rows << ",\"cols\":" << cols << ",\"mines\":" << count << ",\"minefield\":" << jsonString(mines) << ",\"cells\":" << jsonString(cellString(played)) << ",\"lost\":" << (game.state() == Game::State::Lost ? "true" : "false") << ",\"won\":" << (game.state() == Game::State::Won ? "true" : "false");
    const bool includeProbabilities = value(fields, "includeProbabilities") == "true";
    const std::string highlight = value(fields, "highlight");
    if ((includeProbabilities || !highlight.empty()) && game.state() == Game::State::Running) {
        Position::Pool positions(combinations);
        Position::Result position = Position().analyze(played, positions, config);
        if (includeProbabilities) {
            const Position::ProbabilityResult probability = Position().analyzeProbability(position, positions);
            result << ",\"candidateCount\":" << candidateCount(probability.totalWays);
            writeProbabilities(result, position, probability.boxes, positions.probabilities);
        }
        writeHighlight(result, position, positions, highlight);
    }
    result << '}';
    return result.str();
}

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
            if (analysis->mineProbability(targets[i]) == 1.0L) { response = error(Error::SearchTargetMine); return false; }
    Config config = config_;
    if (!parseConfig(fields, config, response)) return false;
    if (targets.size != 0) config.anytime.width[0] = 0;
#ifdef __EMSCRIPTEN__
    searchRunning_ = true;
    searchTask_.reset(new SearchTask(analysis, config, targets, safe));
#else
    {
        const std::lock_guard<std::mutex> lock(searchMutex_);
        searchRunning_ = true;
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
        while (!task.result.finished && !task.limitReached() && !stop.stop_requested()) {
            {
                std::unique_lock<std::mutex> lock(searchMutex_);
                if (!searchWake_.wait(lock, stop, [&]() { return searchRunning_; })) break;
            }
            task.step(nodesPerUpdate);
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
    const SuggestResult::Engine engine = exact ? SuggestResult::Engine::BruteForce : safe.size != 0 ? SuggestResult::Engine::Safe : SuggestResult::Engine::Anytime;
    json << std::setprecision(18) << "{\"ok\":true,\"engine\":" << (int)engine << ",\"candidateCount\":" << candidateCount(task.layouts) << ",\"nodes\":" << result.nodes << ",\"completedDepth\":" << result.completedDepth << ",\"finished\":" << (result.finished ? "true" : "false") << ",\"limitReached\":" << (task.limitReached() ? "true" : "false") << ",\"safe\":" << (safe.size != 0 ? "true" : "false") << ",\"recommended\":" << recommended << ",\"moves\":[";
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
    while (!task.result.finished && !task.limitReached() && searchRunning_) {
        task.step(batch);
        if (std::chrono::steady_clock::now() >= end) break;
    }
    return searchResult(task) + ",\"paused\":" + (!searchRunning_ ? "true" : "false") + '}';
}
#endif

std::string Session::controlSearch(const std::vector<FormField> &fields) {
    int action;
    if (!parseInteger(value(fields, "action"), action) || (action != (int)SearchAction::Pause && action != (int)SearchAction::Resume))
        return error(Error::SearchAction);
#ifndef __EMSCRIPTEN__
    const std::lock_guard<std::mutex> lock(searchMutex_);
#endif
    searchRunning_ = action == (int)SearchAction::Resume;
#ifndef __EMSCRIPTEN__
    searchWake_.notify_one();
#endif
    return "{\"ok\":true}";
}

void Session::stopAnalysis() {
#ifdef __EMSCRIPTEN__
    searchTask_.reset();
#else
    searchWorker_.request_stop();
#endif
}

#ifndef __EMSCRIPTEN__
std::string Session::searchUpdate() const {
    const std::lock_guard<std::mutex> lock(searchMutex_);
    // 缓存保存计算结果前缀；暂停状态读取时补齐，控制操作无需等待下一次计算发布。
    return searchUpdate_ + ",\"paused\":" + (!searchRunning_ ? "true" : "false") + '}';
}

#endif

std::string Session::handle(const std::string &path, const std::string &body) {
    if (path == "/api/analyze-ioe")
        return analyzeIoe(body);
#ifdef __EMSCRIPTEN__
    if (path == "/api/anytime-search/update") return advanceSearch();
    if (path == "/api/anytime-search/start") {
        stopAnalysis();
        std::string response;
        const bool started = startSearch(body, response);
        return started ? "{\"ok\":true}" : response;
    }
    if (path == "/api/anytime-search/stop") {
        stopAnalysis();
        return "{\"ok\":true}";
    }
#endif
    const std::vector<FormField> fields = parseForm(body);
    if (path == "/api/analyze")
        return analyze(fields);
    if (path == "/api/generate-board")
        return generateBoard(fields);
    if (path == "/api/eff-board")
        return generateEffBoard(fields);
    if (path == "/api/play-until-no-safe")
        return playUntilNoSafe(fields);
    if (path == "/api/anytime-search/control")
        return controlSearch(fields);
    if (path == "/api/quit") {
        stopped_ = true;
        stopAnalysis();
        return "{\"ok\":true}";
    }
    return error(Error::UnknownEndpoint);
}

} // 命名空间 mss::UI

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(mss) {
    emscripten::class_<mss::UI::Session>("Session")
        .constructor<>()
        .function("index", &mss::UI::Session::index)
        .function("handle", &mss::UI::Session::handle)
        .function("analyzeIoeStream", emscripten::optional_override([](mss::UI::Session &session, const std::string &body, emscripten::val publish) {
            return session.analyzeIoe(body, [&](const std::string &update) { publish(update); });
        }));
}
#endif
