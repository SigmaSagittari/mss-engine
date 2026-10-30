#include "ui/internal/session.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#include "algo/winrate_engine/fifty_fifty/templates/long_term_risk.h"
#include "core/config.h"
#include "core/assert.h"

namespace mss::UI {

Session::Session(const Config &config) : fiftyPool_(semiPool_, riskPool_), nextAnalysis_(1), stopped_(false), token_(makeToken()), config_(config), searchBudget_(0), searchRunning_(false) {}

const std::string &Session::token() const {
    return token_;
}

bool Session::stopped() const {
    return stopped_;
}

std::string Session::jsonString(const std::string &value) {
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

bool Session::parseInteger(const std::string &value, int &result) {
    const char *begin = value.data();
    const char *end = begin + value.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

std::vector<Session::FormField> Session::parseForm(const std::string &body) {
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

std::string Session::cellString(const ObservedBoard::Result &board) {
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

std::string Session::makeToken() {
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

bool Session::parseTemplateRisk(const std::vector<FormField> &fields, Config &config, std::string &response) {
    int prefix, eventLimit;
    if (!parseInteger(value(fields, "templatePrefix"), prefix) || prefix < 1 || prefix > FiftyFifty::templateCount()) { response = error("模板前缀超出目录"); return false; }
    if (!parseInteger(value(fields, "ltrEventLimit"), eventLimit) || eventLimit < 1) { response = error("LTR 模板匹配上限必须是正整数"); return false; }
    config.fiftyFifty.templates.prefix = prefix;
    config.fiftyFifty.templates.risk.eventLimit = eventLimit;
    return true;
}

bool Session::parseFiftyFifty(const std::vector<FormField> &fields, Config &config, std::string &response) {
    const std::string fifty = value(fields, "fiftyFifty");
    if (fifty == "oldjava") config.fiftyFifty.algorithm = FiftyFifty::Algorithm::OldJava;
    else if (fifty == "templates") config.fiftyFifty.algorithm = FiftyFifty::Algorithm::Templates;
    else { response = error("50-50 算法无效"); return false; }
    return config.fiftyFifty.algorithm == FiftyFifty::Algorithm::OldJava || parseTemplateRisk(fields, config, response);
}

bool Session::parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response) {
    const std::string name = value(fields, "strategy");
    if (name == "lowrisk") config.algorithm = MovePolicy::Basic::Algorithm::LowRisk;
    else if (name == "java") config.algorithm = MovePolicy::Basic::Algorithm::JavaEval;
    else if (name == "anytime") config.algorithm = MovePolicy::Basic::Algorithm::Anytime;
    else { response = error("建议算法无效"); return false; }
    if (!parseInteger(value(fields, "limit"), config.brute.limit) || config.brute.limit < 0) { response = error("暴力接管阈值必须是非负整数"); return false; }
    if (config.brute.limit != 0) {
        if (!parseInteger(value(fields, "maxThreads"), config.brute.config.threadCount) || config.brute.config.threadCount < 1) { response = error("BruteForce 核心数必须是正整数"); return false; }
        config.brute.config.checkAllMoves = value(fields, "checkAllMoves") == "true";
        config.brute.config.parallel = value(fields, "multiThread") == "true";
    }
    if (config.algorithm == MovePolicy::Basic::Algorithm::Anytime) {
        int nodes;
        if (!parseInteger(value(fields, "nodeBudget"), nodes) || nodes < 1) { response = error("Anytime 每步节点数必须是正整数"); return false; }
        config.anytime.nodeLimit = nodes;
        const char *names[] = {"rootWidth", "middleWidth", "deepWidth"};
        for (int i = 0; i < 3; ++i) if (!parseInteger(value(fields, names[i]), config.anytime.width[i]) || config.anytime.width[i] < 1) { response = error("搜索候选宽度必须是正整数"); return false; }
    }
    if (config.algorithm != MovePolicy::Basic::Algorithm::LowRisk && !parseFiftyFifty(fields, config, response)) return false;
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

ObservedBoard::Result Session::makeBoard(int rows, int cols, int mines, const std::string &cells) {
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
    GameControl::Config config = config_.game;
    config.shapeSolver.order = algo;
    analysis.reset(new Analysis(nextAnalysis_++, config, makeBoard(rows, cols, mines, cells)));
    // 人工快照的局部检查和全盘存在性检查由 Position 构造完成，只在输入边界读取一次。
    if (!analysis->position.valid()) {
        response = error(analysis->position.error());
        return false;
    }
    return true;
}

void Session::writeCells(std::ostream &result, span<const ObservedBoard::CellId> cells) {
    result << '[';
    for (int i = 0; i < cells.size; ++i) {
        if (i != 0)
            result << ',';
        result << cells[i];
    }
    result << ']';
}

void Session::writeProbabilities(std::ostream &result, GameControl::Position &position) {
    const ObservedBoard::Result &board = position.observedBoard();
    const Probability::Result &probability = position.probability();
    const Probability::Pool &probabilities = position.state().probabilityPool;
    const Basic::Result &basic = position.basic();
    const Structure::Result &structure = position.structure();
    result << std::setprecision(18) << ",\"probabilities\":[";
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col) {
            if (row != 0 || col != 0)
                result << ',';
            result << Probability::mineProbability(probability, probabilities, board.id(row, col), board, basic, structure);
        }
    result << ']';
}

void Session::writeMoveDetails(std::ostream &result, const SuggestResult::Details &details, ObservedBoard::CellId cell) {
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

void Session::writeSuggestion(std::ostream &result, Analysis &analysis, const Config &config) {
    const MovePolicy policy({config.algorithm, config.algorithm == MovePolicy::Basic::Algorithm::JavaEval ? &config.javaEval : nullptr, config.algorithm == MovePolicy::Basic::Algorithm::Anytime ? &config.anytime : nullptr}, {config.fiftyFifty.algorithm, &config.fiftyFifty.oldJava, &config.fiftyFifty.templates}, {config.brute.limit, config.brute.limit == 0 ? nullptr : &config.brute.config});

    SuggestResult evaluated;
    AnytimeSearch::Result searched;
    SuggestResult::Details details;
    analysis.position.suggest(policy, fiftyPool_, analysis.searchPool, analysis.anytimeSearch, analysis.bruteForce, evaluated, nullptr, &searched, &details);
    result << std::setprecision(18);
    const bool safe = evaluated.engine == SuggestResult::Engine::Safe;
    const bool exact = evaluated.engine == SuggestResult::Engine::BruteForce;
    const char *engine = evaluated.engine == SuggestResult::Engine::None ? "Finished" : safe ? "Safe" : exact ? "BruteForce" : evaluated.engine == SuggestResult::Engine::LowRisk ? "LowRisk" : evaluated.engine == SuggestResult::Engine::JavaEval ? "JavaEval" : "Anytime";
    vector<ObservedBoard::CellId> candidates;
    for (int i = 0; i < (int)evaluated.candidates.size; ++i) candidates.push_back(evaluated.candidates.data[i].cell);
    result << ",\"engine\":" << jsonString(engine) << ",\"reason\":" << jsonString(evaluated.reason == SuggestResult::Reason::Forced ? "forced5050" : evaluated.reason == SuggestResult::Reason::Semi ? "5050OrSafe" : "score") << ",\"nodes\":" << evaluated.nodes << ",\"recommended\":";
    writeCells(result, span<const ObservedBoard::CellId>(evaluated.suggest.data, (int)evaluated.suggest.size));
    result << ",\"candidates\":";
    writeCells(result, span<const ObservedBoard::CellId>(candidates.data, (int)candidates.size));
    result << ",\"moves\":[";
    for (int i = 0; i < (int)evaluated.candidates.size; ++i) {
        if (i != 0) result << ',';
        const SuggestResult::Candidate &candidate = evaluated.candidates.data[i];
        result << "{\"cell\":" << candidate.cell << ",\"value\":" << candidate.value << ",\"probability\":" << analysis.position.mineProbability(candidate.cell);
        if (evaluated.engine == SuggestResult::Engine::Anytime) result << ",\"depth\":" << analysis.searchPool.nodes.data.data[searched.roots.offset + i].expandedDepth;
        writeMoveDetails(result, details, candidate.cell);
        result << '}';
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
            return error("计算上下文已失效");
    }
    GameControl::Position &position = analysis_->position;
    std::ostringstream count;
    count << std::scientific << std::setprecision(6) << position.probability().candidates;
    std::ostringstream result;
    result << "{\"ok\":true,\"analysisId\":" << analysis_->id << ",\"candidateCount\":" << jsonString(count.str());
    if (value(fields, "includeProbabilities") == "true")
        writeProbabilities(result, position);
    if (value(fields, "suggest") == "true") {
        Config config = config_;
        if (!parseConfig(fields, config, response))
            return response;
        writeSuggestion(result, *analysis_, config);
    }
    const std::string cellText = value(fields, "cell");
    if (!cellText.empty()) {
        int cell;
        const ObservedBoard::Result &board = position.observedBoard();
        if (!parseInteger(cellText, cell) || cell < 0 || cell >= board.cells.rows() * board.cells.cols())
            return error("格子编号无效");
        const ObservedBoard::CellState state = board.cells[cell / board.cells.cols()][cell % board.cells.cols()];
        result << std::setprecision(18) << ",\"probability\":" << position.mineProbability(cell);
        if (state < ObservedBoard::CellState::Hidden || state == ObservedBoard::CellState::ForcedMine) {
            result << ",\"observe\":null";
        } else {
            Observe::Result observed;
            position.observe(cell, observed);
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
            if (state != ObservedBoard::CellState::Hidden || position.mineProbability(cell) == 0.0L) result << ",\"longTermRisk\":null";
            else {
                Config config = config_;
                if (!parseFiftyFifty(fields, config, response)) return response;
                FiftyFifty::Workspace &workspace = analysis_->anytimeSearch.selection.fiftyFifty;
                PositionState &current = position.state();
                if (config.fiftyFifty.algorithm == FiftyFifty::Algorithm::OldJava) {
                    const LongTermRisk::Details risk = FiftyFifty::describeOldJava(cell, current, current.probabilityPool, analysis_->config.shapeSolver, config.fiftyFifty.oldJava, workspace.oldJava);
                    result << ",\"longTermRisk\":{\"algorithm\":\"oldjava\",\"body\":" << risk.body << ",\"restriction\":" << risk.restriction << '}';
                } else {
                    TemplateLongTermRisk::Result risk;
                    TemplateLongTermRisk::update(risk, current, current.probabilityPool, config.fiftyFifty.templates.prefix, config.fiftyFifty.templates.risk, fiftyPool_.templates.risk, workspace.templates.risk);
                    const TemplateLongTermRisk::Description description = TemplateLongTermRisk::describe(cell, current, current.probabilityPool, risk, analysis_->config.shapeSolver, workspace.templates.risk);
                    result << ",\"longTermRisk\":{\"algorithm\":\"templates\",\"bodyUnion\":" << description.bodyUnion << ",\"restrictionCounts\":[";
                    for (int k = 0; k < description.noBodyRestrictionBins; ++k) {
                        if (k != 0) result << ',';
                        result << TemplateLongTermRisk::noBodyByRestrictionCount(risk, cell, k);
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
    GameControl::MineBoard mineBoard(grid);
    // 固定 Game 信任雷盘与观测一致；只在接收外部完整游戏时核对一次，不另建人工 Position。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const ObservedBoard::CellState state = board.cells[row][col];
            if (state == ObservedBoard::CellState::ForcedMine || state == ObservedBoard::CellState::ForcedSafe || (state < ObservedBoard::CellState::Hidden && (int)state != mineBoard.number(row, col)))
                return error("游玩观测与真实雷盘不符");
        }
    GameControl::Workspace workspace;
    // 只揭开确定安全格，不调用 suggest，招法池保持空。
    GameControl::SuggestPool suggestions;
    GameControl::Config config = config_.game;
    config.shapeSolver.order = algo;
    GameControl::Game game(mineBoard, board, workspace, config, suggestions);
    game.playUntilNoSafe();
    std::ostringstream result;
    result << "{\"ok\":true,\"rows\":" << rows << ",\"cols\":" << cols << ",\"mines\":" << count << ",\"minefield\":" << jsonString(mines) << ",\"cells\":" << jsonString(cellString(game.position().observedBoard())) << ",\"lost\":" << (game.state() == GameControl::Game::State::Lost ? "true" : "false") << ",\"won\":" << (game.state() == GameControl::Game::State::Won ? "true" : "false");
    if (value(fields, "includeProbabilities") == "true" && game.state() == GameControl::Game::State::Running) {
        std::ostringstream candidateCount;
        candidateCount << std::scientific << std::setprecision(6) << game.position().probability().candidates;
        result << ",\"candidateCount\":" << jsonString(candidateCount.str());
        writeProbabilities(result, game.position());
    }
    result << '}';
    return result.str();
}

std::string Session::mctsResult(const MonteCarlo::Result &snapshot, bool complete, long double candidates) {
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
    std::ostringstream layoutCount;
    layoutCount << std::scientific << std::setprecision(6) << candidates;
    std::ostringstream result;
    result << std::setprecision(12) << "{\"ok\":true,\"simulations\":" << snapshot.samples << ",\"value\":" << winrate << ",\"recommended\":" << recommended << ",\"winrate\":" << (snapshot.winrate ? "true" : "false") << ",\"exact\":" << (exact ? "true" : "false") << ",\"complete\":" << (complete ? "true" : "false") << ",\"candidateCount\":" << jsonString(layoutCount.str()) << ",\"moves\":[";
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
    stopMcts();
    if (mctsWorker_.joinable())
        mctsWorker_.join();
    const std::vector<FormField> fields = parseForm(body);
    Config config = config_;
    if (!parseConfig(fields, config, response)) return false;
    std::unique_ptr<Analysis> analysis;
    if (!createAnalysis(fields, analysis, response))
        return false;
    vector<ObservedBoard::CellId> targets;
    const std::string cells = value(fields, "cells");
    const ObservedBoard::Result &board = analysis->position.observedBoard();
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
            if ((cells[cell] == '?' || cells[cell] == 'G' || cells[cell] == 'S') && analysis->position.mineProbability(cell) == 0.0L) {
                safe = true;
                break;
            }
        // 有必安格时 MC 忽略目标并采整局胜率；仅在实际比较首招时检查外部目标。
        if (!safe)
            for (int i = 0; i < (int)targets.size; ++i)
                if (analysis->position.mineProbability(targets.data[i]) == 1.0L) {
                    response = error("MC 目标格已确定为雷");
                    return false;
                }
    }
    {
        const std::lock_guard<std::mutex> lock(mctsMutex_);
        mctsUpdate_ = "{\"ok\":true,\"simulations\":0,\"value\":0,\"recommended\":-1,\"winrate\":false,\"exact\":false,\"complete\":false,\"moves\":[]}";
    }
    // 根输入及池随独立任务存活；普通分析的下一份快照不会覆盖 MC 正在使用的数据。
    Analysis *task = analysis.release();
    mctsWorker_ = std::jthread([this, task, config, targets, complete](std::stop_token stop) {
        TemplateSemi::Pool semiPool;
        TemplateLongTermRisk::Pool riskPool;
        FiftyFifty::Pool fiftyPool(semiPool, riskPool);
        const std::unique_ptr<Analysis> analysis(task);
        PositionState &state = analysis->position.state();
        const MovePolicy policy({config.algorithm, config.algorithm == MovePolicy::Basic::Algorithm::JavaEval ? &config.javaEval : nullptr, config.algorithm == MovePolicy::Basic::Algorithm::Anytime ? &config.anytime : nullptr}, {config.fiftyFifty.algorithm, &config.fiftyFifty.oldJava, &config.fiftyFifty.templates}, {config.brute.limit, config.brute.limit == 0 ? nullptr : &config.brute.config});
        MonteCarlo::Result result = MonteCarlo::analyze(analysis->position, config.monteCarlo, policy, fiftyPool, analysis->searchPool, analysis->monteCarlo, targets.size == 0 ? nullptr : &targets);
        auto publish = [&]() {
            const std::string update = mctsResult(result, complete, analysis->position.probability().candidates);
            const std::lock_guard<std::mutex> lock(mctsMutex_);
            mctsUpdate_ = update;
        };
        publish();
        if (result.exact.engine == SuggestResult::Engine::BruteForce) return;
        std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (!stop.stop_requested()) {
            MonteCarlo::update(result, state, analysis->config.shapeSolver, config.monteCarlo, policy, fiftyPool, analysis->monteCarlo);
            if (complete)
                break;
            if (std::chrono::steady_clock::now() >= next) {
                publish();
                next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            }
        }
        publish();
    });
    return true;
}

void Session::stopMcts() {
    mctsWorker_.request_stop();
}

std::string Session::mctsUpdate() const {
    const std::lock_guard<std::mutex> lock(mctsMutex_);
    return mctsUpdate_;
}

bool Session::startSearch(const std::string &body, std::string &response) {
    stopSearch();
    if (searchWorker_.joinable()) searchWorker_.join();
    const std::vector<FormField> fields = parseForm(body);
    std::unique_ptr<Analysis> analysis;
    if (!createAnalysis(fields, analysis, response)) return false;
    vector<ObservedBoard::CellId> targets;
    const std::string cells = value(fields, "cells");
    for (int cell = 0; cell < (int)cells.size(); ++cell)
        if (cells[cell] == 'G') targets.push_back(cell);
    vector<ObservedBoard::CellId> safe;
    const span<const ObservedBoard::CellId> safes = analysis->position.safeMoves();
    for (int i = 0; i < safes.size; ++i) safe.push_back(safes.data[i]);
    if (safe.size == 0)
        for (int i = 0; i < (int)targets.size; ++i)
            if (analysis->position.mineProbability(targets.data[i]) == 1.0L) { response = error("Search 目标格已确定为雷"); return false; }
    Config config = config_;
    if (!parseConfig(fields, config, response)) return false;
    if (config.algorithm != MovePolicy::Basic::Algorithm::Anytime) { response = error("搜索仅支持 Anytime"); return false; }
    config.anytime.nodeLimit = 0;
    {
        const std::lock_guard<std::mutex> lock(searchMutex_);
        searchBudget_ = 0;
        searchRunning_ = value(fields, "run") == "true";
        searchUpdate_ = "{\"ok\":true,\"nodes\":0,\"completedDepth\":0,\"finished\":false,\"safe\":false,\"recommended\":-1,\"moves\":[]";
    }
    Analysis *task = analysis.release();
    searchWorker_ = std::jthread([this, task, config, targets, safe](std::stop_token stop) {
        TemplateSemi::Pool semiPool;
        TemplateLongTermRisk::Pool riskPool;
        FiftyFifty::Pool fiftyPool(semiPool, riskPool);
        const std::unique_ptr<Analysis> analysis(task);
        PositionState &state = analysis->position.state();
        AnytimeSearch::Pool &pool = analysis->searchPool;
        const FiftyFifty::Config fifty(config.fiftyFifty.algorithm, &config.fiftyFifty.oldJava, &config.fiftyFifty.templates);
        const MovePolicy policy({config.algorithm, config.algorithm == MovePolicy::Basic::Algorithm::JavaEval ? &config.javaEval : nullptr, config.algorithm == MovePolicy::Basic::Algorithm::Anytime ? &config.anytime : nullptr}, fifty, {config.brute.limit, config.brute.limit == 0 ? nullptr : &config.brute.config});
        SuggestResult initialized;
        AnytimeSearch::Result result;
        SuggestResult::Details details;
        std::ostringstream layoutCount;
        layoutCount << std::scientific << std::setprecision(6) << analysis->position.probability().candidates;
        if (safe.size == 0) {
            // 先算完第零层；用户停止或预算为零都不会留下空推荐。
            analysis->position.suggest(policy, fiftyPool, pool, analysis->anytimeSearch, analysis->bruteForce, initialized, targets.size == 0 ? nullptr : &targets, &result, &details);
        }
        if (initialized.engine == SuggestResult::Engine::BruteForce) result.nodes = initialized.nodes;
        auto publish = [&]() {
            const bool exact = initialized.engine == SuggestResult::Engine::BruteForce;
            const ObservedBoard::CellId recommended = safe.size != 0 ? safe.data[0] : exact ? (initialized.suggest.size != 0 ? initialized.suggest.data[0] : -1) : result.recommended(pool);
            std::ostringstream json;
            json << std::setprecision(18) << "{\"ok\":true,\"engine\":" << jsonString(exact ? "BruteForce" : safe.size != 0 ? "Safe" : "Anytime") << ",\"candidateCount\":" << jsonString(layoutCount.str()) << ",\"nodes\":" << result.nodes << ",\"completedDepth\":" << result.completedDepth << ",\"finished\":" << (result.finished ? "true" : "false") << ",\"safe\":" << (safe.size != 0 ? "true" : "false") << ",\"recommended\":" << recommended << ",\"moves\":[";
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
                const AnytimeSearch::Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
                json << "{\"cell\":" << candidate.cell << ",\"depth\":" << candidate.expandedDepth << ",\"value\":" << candidate.value;
                writeMoveDetails(json, details, candidate.cell);
                json << '}';
            }
            const std::lock_guard<std::mutex> lock(searchMutex_);
            json << ']';
            searchUpdate_ = json.str();
        };
        publish();
        while (!result.finished && !stop.stop_requested()) {
            long long budget;
            {
                std::unique_lock<std::mutex> lock(searchMutex_);
                if (!searchWake_.wait(lock, stop, [&]() { return searchRunning_ || searchBudget_ > 0; })) break;
                budget = searchRunning_ ? 350 : (std::min)(searchBudget_, 350LL);
                if (!searchRunning_) searchBudget_ -= budget;
            }
            AnytimeSearch::Config stepConfig = config.anytime;
            stepConfig.nodeLimit = budget;
            AnytimeSearch::update(result, state, pool, analysis->config.shapeSolver, stepConfig, fifty, fiftyPool, analysis->anytimeSearch, &details);
            publish();
        }
    });
    return true;
}

std::string Session::controlSearch(const std::string &body) {
    const std::vector<FormField> fields = parseForm(body);
    const std::string action = value(fields, "action");
    int nodes = 0;
    if (action == "step" && (!parseInteger(value(fields, "nodes"), nodes) || nodes < 1)) return error("节点预算必须是正整数");
    if (action != "pause" && action != "resume" && action != "step") return error("搜索操作无效");
    {
        const std::lock_guard<std::mutex> lock(searchMutex_);
        searchRunning_ = action == "resume";
        if (action == "step") searchBudget_ += nodes;
        else searchBudget_ = 0;
    }
    searchWake_.notify_one();
    return "{\"ok\":true}";
}

void Session::stopSearch() {
    searchWorker_.request_stop();
}

std::string Session::searchUpdate() const {
    const std::lock_guard<std::mutex> lock(searchMutex_);
    // 缓存保存计算结果前缀；暂停状态读取时补齐，控制操作无需等待下一次计算发布。
    return searchUpdate_ + ",\"paused\":" + (!searchRunning_ && searchBudget_ == 0 ? "true" : "false") + '}';
}

std::string Session::handle(const std::string &path, const std::string &body) {
    const std::vector<FormField> fields = parseForm(body);
    if (path == "/api/analyze")
        return analyze(fields);
    if (path == "/api/play-until-no-safe")
        return playUntilNoSafe(fields);
    if (path == "/api/quit") {
        stopped_ = true;
        stopMcts();
        stopSearch();
        return "{\"ok\":true}";
    }
    return error("未知接口");
}

} // 命名空间 mss::UI
