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
    cells.reserve(board.board.rows() * board.board.cols());
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            const ObservedBoard::CellState state = board.board[row][col];
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

bool Session::parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolver::OrderAlgo &algo) {
    const std::string name = value(fields, "algo");
    if (name == "auto")
        algo = ShapeSolver::OrderAlgo::Auto;
    else if (name == "autoSA")
        algo = ShapeSolver::OrderAlgo::AutoSA;
    else
        return false;
    return true;
}

bool Session::parseConfig(const std::vector<FormField> &fields, BruteForce::Config &config, winrate::SuggestAlgo &strategy, std::string &response) {
    if (!parseInteger(value(fields, "limit"), config.candidateThreshold) || config.candidateThreshold < 1) {
        response = error("JavaBrute 切换阈值无效");
        return false;
    }
    if (!parseInteger(value(fields, "maxThreads"), config.threadLimit) || config.threadLimit < 1) {
        response = error("BruteForce 核心数必须是正整数");
        return false;
    }
    config.checkAllMoves = value(fields, "checkAllMoves") == "true";
    config.solver = value(fields, "multiThread") == "true" ? BruteForce::Config::Solver::BitwiseRootParallel : BruteForce::Config::Solver::Bitwise;
    const std::string name = value(fields, "strategy");
    if (name == "lowrisk")
        strategy = winrate::SuggestAlgo::LowRisk;
    else if (name == "java")
        strategy = winrate::SuggestAlgo::JavaEval;
    else if (name == "javaBrute")
        strategy = winrate::SuggestAlgo::JavaBrute;
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
            board.board[cell / cols][cell % cols] = (ObservedBoard::CellState)(cells[cell] - '0');
        else if (cells[cell] == 'M')
            board.board[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedMine;
        else if (cells[cell] == 'S')
            board.board[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedSafe;
    }
    return board;
}

bool Session::createAnalysis(const std::vector<FormField> &fields, std::unique_ptr<Analysis> &analysis, std::string &response) {
    ShapeSolver::OrderAlgo algo;
    if (!parseOrderAlgo(fields, algo)) {
        response = error("ShapeSolver 算法无效");
        return false;
    }
    int rows, cols, mines;
    std::string cells;
    if (!parseBoard(fields, rows, cols, mines, cells, response))
        return false;
    analysis = std::make_unique<Analysis>(nextAnalysis_++, algo, makeBoard(rows, cols, mines, cells));
    // 人工快照的局部检查和全盘存在性检查由 Position 构造完成，只在输入边界读取一次。
    if (analysis->position.error().failed) {
        response = error(analysis->position.error().reason);
        return false;
    }
    return true;
}

void Session::writeCells(std::ostream &result, const std::vector<ObservedBoard::CellId> &cells) {
    result << '[';
    for (int i = 0; i < (int)cells.size(); ++i) {
        if (i != 0)
            result << ',';
        result << cells[i];
    }
    result << ']';
}

void Session::writeProbabilities(std::ostream &result, const GameControl::Position &position, ShapeSolver::OrderAlgo algo) {
    const ObservedBoard::Result &board = position.observedBoard();
    const Probability::Result &probability = position.probability(algo);
    const Basic::Result &basic = position.basic();
    const Structure::Result &structure = position.structure();
    result << std::setprecision(18) << ",\"probabilities\":[";
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            if (row != 0 || col != 0)
                result << ',';
            result << probability.mineProbability(board.id(row, col), board, basic, structure);
        }
    result << ']';
}

void Session::writeSuggestion(std::ostream &result, Analysis &analysis, const GameControl::Config &config, winrate::SuggestAlgo strategy) {
    GameControl::Position &position = analysis.position;
    std::vector<ObservedBoard::CellId> suggested = position.safeMoves(analysis.algo);
    const ObservedBoard::CellId lowRisk = suggested.empty() ? position.lowRiskMove(analysis.algo) : -1;
    const bool safe = !suggested.empty() || lowRisk < 0;
    if (safe || strategy == winrate::SuggestAlgo::LowRisk) {
        if (!safe)
            suggested.push_back(lowRisk);
        result << ",\"engine\":" << jsonString(safe ? "Safe" : "LowRisk") << ",\"recommended\":";
        writeCells(result, suggested);
        result << ",\"candidates\":";
        writeCells(result, suggested);
        return;
    }
    const winrate::Input input = position.input(analysis.algo);
    if (strategy == winrate::SuggestAlgo::JavaBrute && input.probability.candidates() < config.bruteForce.candidateThreshold) {
        const BruteForce::Result evaluated = BruteForce::solve(input.board, input.basic, input.structure, input.shapes, config.bruteForce, analysis.workspace.game.bruteForce);
        std::vector<ObservedBoard::CellId> candidates;
        std::vector<ObservedBoard::CellId> recommended;
        const BruteForce::Result::Move *best = nullptr;
        for (const BruteForce::Result::Move &move : evaluated.moves) {
            candidates.push_back(move.cell);
            if (!best || move.wins > best->wins)
                best = &move;
        }
        if (best)
            recommended.push_back(best->cell);
        result << ",\"engine\":\"BruteForce\",\"configurationCount\":" << (int)input.probability.candidates() << ",\"nodes\":" << evaluated.nodes << ",\"recommended\":";
        writeCells(result, recommended);
        result << ",\"candidates\":";
        writeCells(result, candidates);
        result << ",\"moves\":[";
        for (int i = 0; i < (int)evaluated.moves.size(); ++i) {
            if (i != 0)
                result << ',';
            result << "{\"cell\":" << evaluated.moves[i].cell << ",\"wins\":" << evaluated.moves[i].wins << '}';
        }
        result << ']';
    } else {
        MoveEval::Selection::Reason selectionReason;
        const JavaEval::Result evaluated = JavaEval::analyze(input, config.javaEval, analysis.workspace.game.javaEval, {}, &selectionReason);
        std::vector<ObservedBoard::CellId> candidates;
        std::vector<ObservedBoard::CellId> recommended;
        for (const JavaEval::Analysis &move : evaluated) candidates.push_back(move.cellId);
        if (!evaluated.empty()) recommended.push_back(std::max_element(evaluated.begin(), evaluated.end(), [](const JavaEval::Analysis &lhs, const JavaEval::Analysis &rhs) { return lhs.value < rhs.value; })->cellId);
        const std::string reason = selectionReason == MoveEval::Selection::Reason::ForcedFiftyFifty ? "forced5050" : selectionReason == MoveEval::Selection::Reason::FiftyFiftyOrSafe ? "5050OrSafe" : selectionReason == MoveEval::Selection::Reason::BlindGuess ? "blindGuess" : "score";
        result << ",\"engine\":\"JavaEval\",\"reason\":" << jsonString(reason) << ",\"recommended\":";
        writeCells(result, recommended);
        result << ",\"candidates\":";
        writeCells(result, candidates);
    }
}

std::string Session::analyze(const std::vector<FormField> &fields) {
    std::string response;
    const std::string idText = value(fields, "analysisId");
    if (idText.empty()) {
        std::unique_ptr<Analysis> next;
        if (!createAnalysis(fields, next, response))
            return response;
        analysis_ = std::move(next);
    } else {
        int id;
        if (!parseInteger(idText, id) || !analysis_ || id != analysis_->id)
            return error("计算上下文已失效");
    }
    GameControl::Position &position = analysis_->position;
    std::ostringstream count;
    count << std::scientific << std::setprecision(6) << position.probability(analysis_->algo).candidates();
    std::ostringstream result;
    result << "{\"ok\":true,\"analysisId\":" << analysis_->id << ",\"candidateCount\":" << jsonString(count.str());
    if (value(fields, "includeProbabilities") == "true")
        writeProbabilities(result, position, analysis_->algo);
    if (value(fields, "suggest") == "true") {
        BruteForce::Config bruteForce = config_.game.bruteForce;
        winrate::SuggestAlgo strategy;
        if (!parseConfig(fields, bruteForce, strategy, response))
            return response;
        GameControl::Config config = config_.game; config.bruteForce = bruteForce;
        writeSuggestion(result, *analysis_, config, strategy);
    }
    const std::string cellText = value(fields, "cell");
    if (!cellText.empty()) {
        int cell;
        const ObservedBoard::Result &board = position.observedBoard();
        if (!parseInteger(cellText, cell) || cell < 0 || cell >= board.board.rows() * board.board.cols())
            return error("格子编号无效");
        const ObservedBoard::CellState state = board.board[cell / board.board.cols()][cell % board.board.cols()];
        result << ",\"probability\":" << jsonString(fixedProbability(position.mineProbability(cell, analysis_->algo)));
        if (state < ObservedBoard::CellState::Hidden || state == ObservedBoard::CellState::ForcedMine) {
            result << ",\"observe\":null";
        } else {
            const Observe::Result observed = position.observe(cell, analysis_->algo);
            // 保留概率精度；前端只省略原值为 0 的结局，不能按六位小数舍入后的值筛选。
            result << std::setprecision(18) << ",\"observe\":[";
            for (int i = 0; i < (int)observed.probability.size(); ++i) {
                if (i != 0)
                    result << ',';
                result << '"' << observed.probability[i] << '"';
            }
            result << ']';
            if (observed.probability[9] == 1.0L) {
                // 爆炸结局的评分为 0，没有安全分支。
                result << ",\"javaScore\":" << jsonString(fixedProbability(0.0L));
            } else {
                const winrate::Input input = position.input(analysis_->algo);
                // 详情只完整评估所选格，不使用候选筛选、剪枝或支配快路径。
                const JavaEval::Result evaluated = JavaEval::analyze(input, config_.game.javaEval, analysis_->workspace.game.javaEval, std::span{&cell, 1});
                result << ",\"javaScore\":" << jsonString(fixedProbability(state == ObservedBoard::CellState::ForcedSafe ? 1.0L : evaluated.front().value)) << ",\"javaBranches\":[";
                for (int i = 0; i < (int)evaluated.front().branches.size(); ++i) {
                    const MoveEval::Branch &branch = evaluated.front().branches[i];
                    if (i != 0) result << ',';
                    result << "{\"digit\":" << branch.digit << ",\"value\":" << branch.value << '}';
                }
                result << ']';
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
    ShapeSolver::OrderAlgo algo;
    if (!parseOrderAlgo(fields, algo))
        return error("ShapeSolver 算法无效");
    const std::string mines = value(fields, "minefield");
    if ((int)mines.size() != rows * cols || mines.find_first_not_of("01") != std::string::npos)
        return error("真实雷盘数据无效");
    RawGrid<char> grid(rows, cols, 0);
    int count = 0;
    for (int cell = 0; cell < rows * cols; ++cell) {
        grid[cell / cols][cell % cols] = mines[cell] - '0';
        count += grid[cell / cols][cell % cols];
    }
    if (count != board.totalMines)
        return error("真实雷盘与总雷数不符");
    GameControl::MineBoard mineBoard(std::move(grid));
    // 固定 Game 信任雷盘与观测一致；只在接收外部完整游戏时核对一次，不另建人工 Position。
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const ObservedBoard::CellState state = board.board[row][col];
            if (state == ObservedBoard::CellState::ForcedMine || state == ObservedBoard::CellState::ForcedSafe || (state < ObservedBoard::CellState::Hidden && (int)state != mineBoard.number(row, col)))
                return error("游玩观测与真实雷盘不符");
        }
    GameControl::Workspace workspace;
    // 只揭开确定安全格，不调用 suggest，招法池保持空。
    GameControl::SuggestPool suggestions;
    GameControl::Game game(std::move(mineBoard), std::move(board), workspace, suggestions);
    game.playUntilNoSafe(algo);
    std::ostringstream result;
    result << "{\"ok\":true,\"rows\":" << rows << ",\"cols\":" << cols << ",\"mines\":" << count << ",\"minefield\":" << jsonString(mines) << ",\"cells\":" << jsonString(cellString(game.position().observedBoard())) << ",\"lost\":" << (game.state() == GameControl::Game::State::Lost ? "true" : "false") << ",\"won\":" << (game.state() == GameControl::Game::State::Won ? "true" : "false");
    if (value(fields, "includeProbabilities") == "true" && game.state() == GameControl::Game::State::Running) {
        std::ostringstream candidateCount;
        candidateCount << std::scientific << std::setprecision(6) << game.position().probability(algo).candidates();
        result << ",\"candidateCount\":" << jsonString(candidateCount.str());
        writeProbabilities(result, game.position(), algo);
    }
    result << '}';
    return result.str();
}

std::string Session::mctsResult(const MonteCarlo::Result &snapshot, bool complete) {
    const ObservedBoard::CellId recommended = snapshot.recommended();
    long double winrate = snapshot.samples() == 0 ? 0 : (long double)snapshot.wins() / snapshot.samples();
    if (!snapshot.winrate)
        for (const MonteCarlo::Candidate &candidate : snapshot.candidates)
            if (candidate.cellId == recommended)
                winrate = candidate.samples == 0 ? 0 : (long double)candidate.wins / candidate.samples;
    std::ostringstream result;
    result << std::setprecision(12) << "{\"ok\":true,\"simulations\":" << snapshot.samples() << ",\"value\":" << winrate << ",\"recommended\":" << recommended << ",\"winrate\":" << (snapshot.winrate ? "true" : "false") << ",\"complete\":" << (complete ? "true" : "false") << ",\"moves\":[";
    bool first = true;
    for (const MonteCarlo::Candidate &candidate : snapshot.candidates) {
        if (candidate.samples == 0)
            continue;
        if (!first)
            result << ',';
        first = false;
        result << "{\"cell\":" << candidate.cellId << ",\"visits\":" << jsonString(std::to_string(candidate.samples)) << ",\"wins\":" << jsonString(std::to_string(candidate.wins)) << ",\"winrate\":" << (long double)candidate.wins / candidate.samples << '}';
    }
    result << "]}";
    return result.str();
}

bool Session::startMcts(const std::string &body, std::string &response) {
    stopMcts();
    if (mctsWorker_.joinable())
        mctsWorker_.join();
    const std::vector<FormField> fields = parseForm(body);
    BruteForce::Config bruteForce = config_.game.bruteForce;
    winrate::SuggestAlgo strategy;
    if (!parseConfig(fields, bruteForce, strategy, response))
        return false;
    MonteCarlo::Config config = config_.monteCarlo; config.game.bruteForce = bruteForce;
    std::unique_ptr<Analysis> analysis;
    if (!createAnalysis(fields, analysis, response))
        return false;
    std::vector<ObservedBoard::CellId> targets;
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
    if (!complete && !targets.empty()) {
        bool safe = false;
        for (int cell = 0; cell < (int)cells.size(); ++cell)
            if ((cells[cell] == '?' || cells[cell] == 'G' || cells[cell] == 'S') && analysis->position.mineProbability(cell, analysis->algo) == 0.0L) {
                safe = true;
                break;
            }
        // 有必安格时 MC 忽略目标并采整局胜率；仅在实际比较首招时检查外部目标。
        if (!safe)
            for (ObservedBoard::CellId cell : targets)
                if (analysis->position.mineProbability(cell, analysis->algo) == 1.0L) {
                    response = error("MC 目标格已确定为雷");
                    return false;
                }
    }
    {
        const std::lock_guard<std::mutex> lock(mctsMutex_);
        mctsUpdate_ = "{\"ok\":true,\"simulations\":0,\"value\":0,\"recommended\":-1,\"winrate\":false,\"complete\":false,\"moves\":[]}";
    }
    // 根输入及池随独立任务存活；普通分析的下一份快照不会覆盖 MC 正在使用的数据。
    mctsWorker_ = std::jthread([this, analysis = std::move(analysis), config, strategy, targets = std::move(targets), complete](std::stop_token stop) {
        const winrate::Input input = analysis->position.input(analysis->algo);
        MonteCarlo::Result result = MonteCarlo::analyze(input, strategy, config, analysis->workspace.monteCarlo, targets);
        auto publish = [&]() {
            const std::string update = mctsResult(result, complete);
            const std::lock_guard<std::mutex> lock(mctsMutex_);
            mctsUpdate_ = update;
        };
        publish();
        std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (MonteCarlo::update(result, input, config, analysis->workspace.monteCarlo, stop)) {
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
    std::vector<ObservedBoard::CellId> targets;
    const std::string cells = value(fields, "cells");
    for (int cell = 0; cell < (int)cells.size(); ++cell)
        if (cells[cell] == 'G') targets.push_back(cell);
    std::vector<ObservedBoard::CellId> safe = analysis->position.safeMoves(analysis->algo);
    if (safe.empty())
        for (ObservedBoard::CellId cell : targets)
            if (analysis->position.mineProbability(cell, analysis->algo) == 1.0L) { response = error("Search 目标格已确定为雷"); return false; }
    AnytimeSearch::Config config = config_.anytimeSearch; config.width = width; config.fullAnalysis = !targets.empty();
    {
        const std::lock_guard<std::mutex> lock(searchMutex_);
        searchBudget_ = 0;
        searchRunning_ = value(fields, "run") == "true";
        searchUpdate_ = "{\"ok\":true,\"nodes\":0,\"completedDepth\":0,\"finished\":false,\"safe\":false,\"recommended\":-1,\"moves\":[]";
    }
    searchWorker_ = std::jthread([this, analysis = std::move(analysis), config, targets = std::move(targets), safe = std::move(safe)](std::stop_token stop) {
        const winrate::Input input = analysis->position.input(analysis->algo);
        AnytimeSearch::Result result;
        if (!safe.empty()) {
            // 专用 anytime 入口也接受有安全格的盘面，并完整返回全部安全格。
            for (ObservedBoard::CellId cell : safe) result.candidates.emplace_back(cell, 1.0L, 1.0L);
            result.finished = true;
        } else {
            // 先算完第零层；用户停止或预算为零都不会留下空推荐。
            result = AnytimeSearch::analyze(input, config, analysis->workspace.anytimeSearch, 0, stop, targets);
        }
        auto publish = [&]() {
            std::ostringstream json;
            json << std::setprecision(18) << "{\"ok\":true,\"nodes\":" << result.node << ",\"completedDepth\":" << result.completedDepth << ",\"finished\":" << (result.finished ? "true" : "false") << ",\"safe\":" << (!safe.empty() ? "true" : "false") << ",\"recommended\":" << result.recommended() << ",\"moves\":[";
            for (int i = 0; i < (int)result.candidates.size(); ++i) {
                const AnytimeSearch::Candidate &candidate = result.candidates[i];
                if (i != 0) json << ',';
                json << "{\"cell\":" << candidate.cellId << ",\"multiplier\":" << candidate.multiplier << ",\"depth\":" << candidate.value.size() - 1 << ",\"value\":[";
                for (int depth = 0; depth < (int)candidate.value.size(); ++depth) {
                    if (depth != 0) json << ',';
                    json << candidate.value[depth];
                }
                json << "],\"branches\":[";
                for (int branch = 0; branch < (int)candidate.branches.size(); ++branch) {
                    const AnytimeSearch::Candidate::Branch &score = candidate.branches[branch];
                    if (branch != 0) json << ',';
                    json << "{\"digit\":" << score.digit << ",\"value\":" << score.value << ",\"depth\":" << score.depth << '}';
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
            AnytimeSearch::update(result, config, analysis->workspace.anytimeSearch, budget, stop);
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

} // namespace mss::UI
