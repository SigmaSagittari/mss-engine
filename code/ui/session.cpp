#include "ui/internal/session.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#include "core/config.h"
#include "core/assert.h"

namespace mss::UI {

Session::Session(const Config &config) : nextAnalysis_(1), stopped_(false), token_(makeToken()), config_(config), searchBudget_(0), searchRunning_(false) {}

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

std::string Session::fixedProbability(long double value) {
    std::ostringstream result;
    result << std::fixed << std::setprecision(6) << value;
    return result.str();
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

bool Session::parseConfig(const std::vector<FormField> &fields, JavaBrute::Config &config, MonteCarlo::PlayoutAlgo &strategy, std::string &response) {
    if (!parseInteger(value(fields, "limit"), config.candidateThreshold) || config.candidateThreshold < 1) {
        response = error("JavaBrute 切换阈值无效");
        return false;
    }
    if (!parseInteger(value(fields, "maxThreads"), config.bruteForce.threadCount) || config.bruteForce.threadCount < 1) {
        response = error("BruteForce 核心数必须是正整数");
        return false;
    }
    config.bruteForce.checkAllMoves = value(fields, "checkAllMoves") == "true";
    config.bruteForce.parallel = value(fields, "multiThread") == "true";
    const std::string name = value(fields, "strategy");
    if (name == "lowrisk")
        strategy = MonteCarlo::PlayoutAlgo::LowRisk;
    else if (name == "java")
        strategy = MonteCarlo::PlayoutAlgo::JavaEval;
    else if (name == "javaBrute")
        strategy = MonteCarlo::PlayoutAlgo::JavaBrute;
    else {
        response = error("建议策略无效");
        return false;
    }
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
    const Basic::Result &basic = position.basic();
    const Structure::Result &structure = position.structure();
    result << std::setprecision(18) << ",\"probabilities\":[";
    for (int row = 0; row < board.cells.rows(); ++row)
        for (int col = 0; col < board.cells.cols(); ++col) {
            if (row != 0 || col != 0)
                result << ',';
            result << Probability::mineProbability(probability, board.id(row, col), board, basic, structure);
        }
    result << ']';
}

void Session::writeSuggestion(std::ostream &result, Analysis &analysis, const JavaBrute::Config &config, MonteCarlo::PlayoutAlgo strategy) {
    GameControl::Position &position = analysis.position;
    span<const ObservedBoard::CellId> suggested = position.safeMoves();
    const ObservedBoard::CellId lowRisk = suggested.size == 0 ? position.lowRiskMove() : -1;
    const bool safe = suggested.size != 0 || lowRisk < 0;
    if (safe || strategy == MonteCarlo::PlayoutAlgo::LowRisk) {
        if (!safe) suggested = span<const ObservedBoard::CellId>(&lowRisk, 1);
        result << ",\"engine\":" << jsonString(safe ? "Safe" : "LowRisk") << ",\"recommended\":";
        writeCells(result, suggested);
        result << ",\"candidates\":";
        writeCells(result, suggested);
        return;
    }
    PositionState &state = position.state();
    if (strategy == MonteCarlo::PlayoutAlgo::JavaBrute && state.probability.candidates < config.candidateThreshold) {
        BruteForce::Result evaluated;
        BruteForce::solve(state.board, state.basic, state.structure, state.shapes, config.bruteForce, evaluated, analysis.javaBrute.bruteForce);
        vector<ObservedBoard::CellId> candidates;
        const BruteForce::Result::Move *best = nullptr;
        for (int i = 0; i < (int)evaluated.moves.size; ++i) {
            const BruteForce::Result::Move &move = evaluated.moves.data[i];
            candidates.push_back(move.cell);
            if (!best || move.wins > best->wins)
                best = &move;
        }
        result << ",\"engine\":\"BruteForce\",\"configurationCount\":" << (int)state.probability.candidates << ",\"nodes\":" << evaluated.nodes << ",\"recommended\":";
        writeCells(result, best ? span<const ObservedBoard::CellId>(&best->cell, 1) : span<const ObservedBoard::CellId>());
        result << ",\"candidates\":";
        writeCells(result, span<const ObservedBoard::CellId>(candidates.data, (int)candidates.size));
        result << ",\"moves\":[";
        for (int i = 0; i < (int)evaluated.moves.size; ++i) {
            if (i != 0)
                result << ',';
            result << "{\"cell\":" << evaluated.moves.data[i].cell << ",\"wins\":" << evaluated.moves.data[i].wins << '}';
        }
        result << ']';
    } else {
        Selection::Reason selectionReason;
        const JavaEval::Result evaluated = JavaEval::analyze(state, analysis.config.shapeSolver, config.javaEval, analysis.javaEval, &selectionReason);
        vector<ObservedBoard::CellId> candidates;
        const JavaEval::Candidate *best = nullptr;
        for (int i = 0; i < (int)evaluated.candidates.size; ++i) {
            const JavaEval::Candidate &move = evaluated.candidates.data[i];
            candidates.push_back(move.cell);
            if (!best || move.value > best->value) best = &move;
        }
        const std::string reason = selectionReason == Selection::Reason::Forced ? "forced5050" : selectionReason == Selection::Reason::Semi ? "5050OrSafe" : "score";
        result << ",\"engine\":\"JavaEval\",\"reason\":" << jsonString(reason) << ",\"recommended\":";
        writeCells(result, best ? span<const ObservedBoard::CellId>(&best->cell, 1) : span<const ObservedBoard::CellId>());
        result << ",\"candidates\":";
        writeCells(result, span<const ObservedBoard::CellId>(candidates.data, (int)candidates.size));
    }
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
        JavaBrute::Config config = config_.javaBrute;
        MonteCarlo::PlayoutAlgo strategy;
        if (!parseConfig(fields, config, strategy, response))
            return response;
        writeSuggestion(result, *analysis_, config, strategy);
    }
    const std::string cellText = value(fields, "cell");
    if (!cellText.empty()) {
        int cell;
        const ObservedBoard::Result &board = position.observedBoard();
        if (!parseInteger(cellText, cell) || cell < 0 || cell >= board.cells.rows() * board.cells.cols())
            return error("格子编号无效");
        const ObservedBoard::CellState state = board.cells[cell / board.cells.cols()][cell % board.cells.cols()];
        result << ",\"probability\":" << jsonString(fixedProbability(position.mineProbability(cell)));
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
            if (observed.probability[9] == 1.0L) {
                // 爆炸结局的评分为 0，没有安全分支。
                result << ",\"javaScore\":" << jsonString(fixedProbability(0.0L));
            } else {
                // 详情只完整评估所选格，不使用候选筛选、剪枝或支配快路径。
                vector<ObservedBoard::CellId> cells;
                cells.push_back(cell);
                const JavaEval::Result evaluated = JavaEval::analyze(position.state(), analysis_->config.shapeSolver, config_.javaBrute.javaEval, analysis_->javaEval, nullptr, &cells);
                result << ",\"javaScore\":" << jsonString(fixedProbability(evaluated.candidates.data[0].value));
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

std::string Session::mctsResult(const MonteCarlo::Result &snapshot, bool complete) {
    const ObservedBoard::CellId recommended = snapshot.recommended();
    long double winrate = snapshot.samples == 0 ? 0 : (long double)snapshot.wins / snapshot.samples;
    if (!snapshot.winrate)
        for (int i = 0; i < (int)snapshot.candidates.size; ++i) {
            const MonteCarlo::Candidate &candidate = snapshot.candidates.data[i];
            if (candidate.cell == recommended)
                winrate = candidate.samples == 0 ? 0 : (long double)candidate.wins / candidate.samples;
        }
    std::ostringstream result;
    result << std::setprecision(12) << "{\"ok\":true,\"simulations\":" << snapshot.samples << ",\"value\":" << winrate << ",\"recommended\":" << recommended << ",\"winrate\":" << (snapshot.winrate ? "true" : "false") << ",\"complete\":" << (complete ? "true" : "false") << ",\"moves\":[";
    bool first = true;
    for (int i = 0; i < (int)snapshot.candidates.size; ++i) {
        const MonteCarlo::Candidate &candidate = snapshot.candidates.data[i];
        if (candidate.samples == 0)
            continue;
        if (!first)
            result << ',';
        first = false;
        result << "{\"cell\":" << candidate.cell << ",\"visits\":" << jsonString(std::to_string(candidate.samples)) << ",\"wins\":" << jsonString(std::to_string(candidate.wins)) << ",\"winrate\":" << (long double)candidate.wins / candidate.samples << '}';
    }
    result << "]}";
    return result.str();
}

bool Session::startMcts(const std::string &body, std::string &response) {
    stopMcts();
    if (mctsWorker_.joinable())
        mctsWorker_.join();
    const std::vector<FormField> fields = parseForm(body);
    JavaBrute::Config bruteForce = config_.javaBrute;
    MonteCarlo::PlayoutAlgo strategy;
    if (!parseConfig(fields, bruteForce, strategy, response))
        return false;
    MonteCarlo::Config config = config_.monteCarlo;
    config.javaBrute = bruteForce;
    config.playout = strategy;
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
        mctsUpdate_ = "{\"ok\":true,\"simulations\":0,\"value\":0,\"recommended\":-1,\"winrate\":false,\"complete\":false,\"moves\":[]}";
    }
    // 根输入及池随独立任务存活；普通分析的下一份快照不会覆盖 MC 正在使用的数据。
    Analysis *task = analysis.release();
    mctsWorker_ = std::jthread([this, task, config, targets, complete](std::stop_token stop) {
        const std::unique_ptr<Analysis> analysis(task);
        PositionState &state = analysis->position.state();
        MonteCarlo::Result result = MonteCarlo::analyze(state, analysis->config.shapeSolver, config, analysis->monteCarlo, targets.size == 0 ? nullptr : &targets);
        auto publish = [&]() {
            const std::string update = mctsResult(result, complete);
            const std::lock_guard<std::mutex> lock(mctsMutex_);
            mctsUpdate_ = update;
        };
        publish();
        std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (!stop.stop_requested()) {
            MonteCarlo::update(result, state, analysis->config.shapeSolver, config, analysis->monteCarlo);
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
    std::array<int, 3> width;
    const std::array<const char *, 3> names{"rootWidth", "middleWidth", "deepWidth"};
    for (int i = 0; i < 3; ++i)
        if (!parseInteger(value(fields, names[i]), width[i]) || width[i] < 1) { response = error("搜索候选宽度必须是正整数"); return false; }
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
    AnytimeSearch::Config config = config_.anytimeSearch;
    for (int i = 0; i < 3; ++i) config.width[i] = width[i];
    config.fullAnalysis = targets.size != 0;
    {
        const std::lock_guard<std::mutex> lock(searchMutex_);
        searchBudget_ = 0;
        searchRunning_ = value(fields, "run") == "true";
        searchUpdate_ = "{\"ok\":true,\"nodes\":0,\"completedDepth\":0,\"finished\":false,\"safe\":false,\"recommended\":-1,\"moves\":[]";
    }
    Analysis *task = analysis.release();
    searchWorker_ = std::jthread([this, task, config, targets, safe](std::stop_token stop) {
        const std::unique_ptr<Analysis> analysis(task);
        PositionState &state = analysis->position.state();
        AnytimeSearch::Pool &pool = analysis->searchPool;
        AnytimeSearch::Result result;
        if (safe.size == 0) {
            // 先算完第零层；用户停止或预算为零都不会留下空推荐。
            result = AnytimeSearch::analyze(state, pool, analysis->config.shapeSolver, config, analysis->anytimeSearch, 0, targets.size == 0 ? nullptr : &targets);
        }
        auto publish = [&]() {
            std::ostringstream json;
            json << std::setprecision(18) << "{\"ok\":true,\"nodes\":" << result.node << ",\"completedDepth\":" << result.completedDepth << ",\"finished\":" << (result.finished ? "true" : "false") << ",\"safe\":" << (safe.size != 0 ? "true" : "false") << ",\"recommended\":" << (safe.size != 0 ? safe.data[0] : result.recommended(pool)) << ",\"moves\":[";
            const int count = safe.size != 0 ? (int)safe.size : (int)result.roots.count;
            for (int i = 0; i < count; ++i) {
                if (i != 0) json << ',';
                if (safe.size != 0) {
                    json << "{\"cell\":" << safe.data[i] << ",\"multiplier\":1,\"depth\":0,\"value\":1,\"branches\":[]}";
                    continue;
                }
                const AnytimeSearch::Pool::Node &candidate = pool.nodes.data.data[result.roots.offset + i];
                json << "{\"cell\":" << candidate.cell << ",\"multiplier\":" << candidate.multiplier << ",\"depth\":" << candidate.expandedDepth << ",\"value\":" << candidate.value << ",\"branches\":[";
                bool first = true;
                for (int j = 0; j < (int)candidate.child.count; ++j) {
                    const AnytimeSearch::Pool::Node &branch = pool.nodes.data.data[candidate.child.offset + j];
                    if (!branch.prepared && !branch.finished) continue;
                    const JavaEval::Branch &transition = pool.transitions.data[branch.transition];
                    int digit = -1;
                    for (int k = 0; k < (int)transition.boardDelta.changes.count; ++k) {
                        const ObservedBoard::Delta::Change &change = state.basicPool.board.changes.data.data[transition.boardDelta.changes.offset + k];
                        if (change.cell == candidate.cell && change.next <= ObservedBoard::CellState::Num8) digit = (int)change.next;
                    }
                    assert_(digit >= 0, "搜索分支缺少根格揭示数字");
                    if (!first) json << ',';
                    first = false;
                    json << "{\"digit\":" << digit << ",\"value\":" << branch.value << ",\"depth\":" << branch.expandedDepth << '}';
                }
                json << "]}";
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
                budget = searchRunning_ ? 100 : (std::min)(searchBudget_, 100LL);
                if (!searchRunning_) searchBudget_ -= budget;
            }
            AnytimeSearch::update(result, state, pool, analysis->config.shapeSolver, config, analysis->anytimeSearch, budget);
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
