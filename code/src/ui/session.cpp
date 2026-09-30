#include "ui/internal/session.h"

#include <charconv>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#include "algo/probability_engine/probability/observe.h"

namespace mss::UI {

Session::Session() {
    const U128 seed = makeSeed();
    std::ostringstream token;
    token << std::hex << std::setfill('0') << std::setw(16) << seed.lo << std::setw(16) << seed.hi;
    token_ = token.str();
}

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

bool Session::setOrderAlgo(const std::vector<FormField> &fields) {
    const std::string algoValue = value(fields, "algo");
    if (algoValue.empty())
        return true;
    ShapeSolver::OrderAlgo algo;
    if (algoValue == "auto")
        algo = ShapeSolver::OrderAlgo::Auto;
    else if (algoValue == "autoSA")
        algo = ShapeSolver::OrderAlgo::AutoSA;
    else
        return false;
    if (algo != orderAlgo_) {
        orderAlgo_ = algo;
        analysisDirty_ = true;
    }
    return true;
}

std::string Session::error(const std::string &message) const {
    return "{\"ok\":false,\"error\":" + jsonString(message) + "}";
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

std::string Session::probabilitiesJson(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability) {
    std::ostringstream result;
    result << std::setprecision(18) << '[';
    for (int row = 0; row < board.board.rows(); ++row)
        for (int col = 0; col < board.board.cols(); ++col) {
            if (row != 0 || col != 0)
                result << ',';
            result << probability.mineProbability(board.id(row, col), board, basic, structure);
        }
    result << ']';
    return result.str();
}

U128 Session::makeSeed() {
    std::random_device random;
    const std::uint64_t low = ((std::uint64_t)random() << 32) | (std::uint64_t)random();
    const std::uint64_t high = ((std::uint64_t)random() << 32) | (std::uint64_t)random();
    return {low, high};
}

std::string Session::analysisState() const {
    std::ostringstream result;
    result << "{\"ok\":true,\"mode\":\"analysis\",\"rows\":" << analysisBoard_.board.rows() << ",\"cols\":" << analysisBoard_.board.cols() << ",\"mines\":" << analysisBoard_.totalMines
           << ",\"cells\":" << jsonString(cellString(analysisBoard_)) << ",\"dirty\":" << (analysisDirty_ ? "true" : "false") << '}';
    return result.str();
}

bool Session::setAnalysisBoard(const std::vector<FormField> &fields, bool allowForcedStates, std::string &response) {
    int rows = 0;
    int cols = 0;
    int mines = 0;
    if (!parseInteger(value(fields, "rows"), rows) || !parseInteger(value(fields, "cols"), cols) || !parseInteger(value(fields, "mines"), mines)) {
        response = error("盘面参数无效");
        return false;
    }
    if (rows < 1 || cols < 1 || rows > 50 || cols > 50 || rows * cols > 2500 || mines < 0 || mines >= rows * cols) {
        response = error("自定义盘面范围无效");
        return false;
    }
    const std::string cells = value(fields, "cells");
    if ((int)cells.size() != rows * cols) {
        response = error("盘面数据长度无效");
        return false;
    }
    for (int cell = 0; cell < rows * cols; ++cell) {
        if (cells[cell] == '?' || (cells[cell] >= '0' && cells[cell] <= '8'))
            continue;
        if (allowForcedStates && (cells[cell] == 'M' || cells[cell] == 'S'))
            continue;
        response = error("盘面状态无效");
        return false;
    }
    // 游玩快照只传数字和隐藏格；分析导入额外允许 M/S 约束，旗标和真实雷位始终留在浏览器。
    analysisBoard_ = ObservedBoard::analyze(rows, cols, mines);
    for (int cell = 0; cell < rows * cols; ++cell) {
        if (cells[cell] >= '0' && cells[cell] <= '8')
            analysisBoard_.board[cell / cols][cell % cols] = (ObservedBoard::CellState)(cells[cell] - '0');
        else if (cells[cell] == 'M')
            analysisBoard_.board[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedMine;
        else if (cells[cell] == 'S')
            analysisBoard_.board[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedSafe;
    }
    analysisShapes_.clear();
    analysisDistributions_.clear();
    analysisReady_ = false;
    analysisDirty_ = true;
    analysisError_.clear();
    response = analysisState();
    return true;
}

bool Session::calculateAnalysis() {
    analysisDirty_ = false;
    analysisReady_ = false;
    analysisError_.clear();
    analysisShapes_.clear();
    analysisDistributions_.clear();
    analysisBasic_ = Basic::analyze(analysisBoard_, workspace_.basic);
    if (!Basic::checkValid(analysisBoard_, analysisBasic_)) {
        analysisError_ = "数字约束互相矛盾";
        return false;
    }
    analysisStructure_ = Structure::analyze(analysisBoard_, analysisBasic_, analysisShapes_, workspace_.structure);
    Probability::analyze(analysisBoard_, analysisBasic_, analysisStructure_, analysisShapes_, analysisDistributions_, analysisProbability_, workspace_.probability, orderAlgo_);
    if (analysisProbability_.candidates() == 0.0L) {
        analysisError_ = "没有满足全盘雷数约束的布局";
        return false;
    }
    analysisReady_ = true;
    return true;
}

std::string Session::analysisResult() {
    if (analysisDirty_ && !calculateAnalysis())
        return error(analysisError_);
    if (!analysisReady_)
        return error(analysisError_);
    std::ostringstream candidateCount;
    candidateCount << std::scientific << std::setprecision(6) << analysisProbability_.candidates();
    return "{\"ok\":true,\"rows\":" + std::to_string(analysisBoard_.board.rows()) + ",\"cols\":" + std::to_string(analysisBoard_.board.cols()) + ",\"cells\":" + jsonString(cellString(analysisBoard_)) + ",\"candidateCount\":" + jsonString(candidateCount.str()) + ",\"probabilities\":" + probabilitiesJson(analysisBoard_, analysisBasic_, analysisStructure_, analysisProbability_) + "}";
}

std::string Session::analyzeEndgame(int engineThreshold, bool includeProbabilities, bool checkAllMoves, bool multiThread) {
    if (analysisDirty_ && !calculateAnalysis())
        return error(analysisError_);
    if (!analysisReady_)
        return error(analysisError_);
    const long double candidateCount = analysisProbability_.candidates();
    std::ostringstream candidateText;
    candidateText << std::scientific << std::setprecision(6) << candidateCount;
    const std::string probabilityData = includeProbabilities ? ",\"probabilities\":" + probabilitiesJson(analysisBoard_, analysisBasic_, analysisStructure_, analysisProbability_) : "";
    if (candidateCount > engineThreshold) {
        const LongTermRiskReference::Config riskConfig{};
        const LongTermRiskReference::Influence risk = LongTermRiskReference::findInfluence(analysisBoard_, analysisBasic_, analysisStructure_, analysisProbability_, analysisShapes_, analysisDistributions_, {}, riskConfig, workspace_.longTermRisk);
        const JavaEvaluate::Config evaluateConfig{};
        const JavaEvaluate::Result java = JavaEvaluate::solve(analysisBoard_, analysisBasic_, analysisStructure_, analysisProbability_, analysisShapes_, analysisDistributions_, risk, {}, evaluateConfig, workspace_.javaEvaluate);
        std::ostringstream result;
        const int bestCell = java.x == 0 || java.y == 0 ? -1 : analysisBoard_.id(java.x - 1, java.y - 1);
        result << "{\"ok\":true,\"exceeded\":false,\"engine\":\"JavaEval\",\"candidateCount\":" << jsonString(candidateText.str()) << ",\"bestCell\":" << bestCell << ",\"candidates\":[";
        for (int i = 0; i < (int)java.candidates.size(); ++i) {
            if (i != 0)
                result << ',';
            result << analysisBoard_.id(java.candidates[i].x - 1, java.candidates[i].y - 1);
        }
        result << ']';
        if (includeProbabilities)
            result << probabilityData;
        result << '}';
        return result.str();
    }
    const int configurationCount = (int)candidateCount;
    BruteForce::Config options;
    options.checkAllMoves = checkAllMoves;
    options.solver = multiThread ? BruteForce::Solver::BitwiseRootParallel : BruteForce::Solver::Bitwise;
    const BruteForce::Result bruteForce = BruteForce::solve(analysisBoard_, analysisBasic_, analysisStructure_, analysisShapes_, options, workspace_.bruteForce);
    std::ostringstream result;
    result << "{\"ok\":true,\"exceeded\":false,\"engine\":\"BruteForce\",\"candidateCount\":" << jsonString(candidateText.str()) << ",\"configurationCount\":" << configurationCount << ",\"nodes\":" << bruteForce.nodes << ",\"moves\":[";
    for (int i = 0; i < (int)bruteForce.moves.size(); ++i) {
        if (i != 0)
            result << ',';
        result << "{\"cell\":" << bruteForce.moves[i].cell << ",\"wins\":" << bruteForce.moves[i].wins << '}';
    }
    result << ']';
    if (includeProbabilities)
        result << probabilityData;
    result << '}';
    return result.str();
}

std::string Session::detail(int cell) {
    if (cell < 0 || cell >= analysisBoard_.board.rows() * analysisBoard_.board.cols())
        return error("格子编号无效");
    if (analysisDirty_ && !calculateAnalysis())
        return error(analysisError_);
    if (!analysisReady_)
        return error(analysisError_);
    const int cols = analysisBoard_.board.cols();
    const int row = cell / cols;
    const int col = cell % cols;
    if (analysisBoard_.board[row][col] < ObservedBoard::CellState::Hidden)
        return "{\"ok\":true,\"probability\":\"0.000000\",\"observe\":null}";
    const std::string risk = fixedProbability(analysisProbability_.mineProbability(cell, analysisBoard_, analysisBasic_, analysisStructure_));
    const Observe::Result observed = Observe::observe(analysisBoard_, analysisBasic_, analysisStructure_, analysisShapes_, analysisProbability_, analysisDistributions_, cell, workspace_.probability, orderAlgo_);
    std::ostringstream result;
    result << "{\"ok\":true,\"probability\":" << jsonString(risk) << ",\"observe\":[";
    for (int i = 0; i < (int)observed.probability.size(); ++i) {
        if (i != 0)
            result << ',';
        result << jsonString(fixedProbability(observed.probability[i]));
    }
    result << "]}";
    return result.str();
}

std::string Session::handle(const std::string &path, const std::string &body) {
    const std::vector<FormField> fields = parseForm(body);
    if (path == "/api/copy-analysis" || path == "/api/analysis/import") {
        std::string response;
        setAnalysisBoard(fields, path == "/api/analysis/import", response);
        return response;
    }
    if (path == "/api/analysis/edit") {
        int cell = -1;
        const std::string cellText = value(fields, "cell");
        const std::string stateText = value(fields, "state");
        if (!parseInteger(cellText, cell) || stateText.size() != 1)
            return error("编辑参数无效");
        if (cell < 0 || cell >= analysisBoard_.board.rows() * analysisBoard_.board.cols())
            return error("格子编号无效");
        const char state = stateText[0];
        if (state != '?' && state != 'S' && state != 'M' && (state < '0' || state > '8'))
            return error("格子状态无效");
        const int cols = analysisBoard_.board.cols();
        if (state == '?')
            analysisBoard_.board[cell / cols][cell % cols] = ObservedBoard::CellState::Hidden;
        else if (state == 'M')
            analysisBoard_.board[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedMine;
        else if (state == 'S')
            analysisBoard_.board[cell / cols][cell % cols] = ObservedBoard::CellState::ForcedSafe;
        else
            analysisBoard_.board[cell / cols][cell % cols] = (ObservedBoard::CellState)(state - '0');
        analysisDirty_ = true;
        analysisReady_ = false;
        return analysisState();
    }
    if (path == "/api/analysis/run") {
        if (!setOrderAlgo(fields))
            return error("ShapeSolver 算法无效");
        return analysisResult();
    }
    if (path == "/api/endgame/analyze") {
        int engineThreshold = 0;
        if (!parseInteger(value(fields, "limit"), engineThreshold) || engineThreshold < 1)
            return error("JavaEval 切换阈值无效");
        if (!setOrderAlgo(fields))
            return error("ShapeSolver 算法无效");
        std::string response;
        if (!setAnalysisBoard(fields, value(fields, "mode") == "analysis", response))
            return response;
        return analyzeEndgame(engineThreshold, value(fields, "includeProbabilities") == "true", value(fields, "checkAllMoves") == "true", value(fields, "multiThread") == "true");
    }
    if (path == "/api/detail") {
        int cell = -1;
        if (value(fields, "mode") != "analysis")
            return error("格子详情仅在分析模式开放");
        if (!parseInteger(value(fields, "cell"), cell))
            return error("格子编号无效");
        if (!setOrderAlgo(fields))
            return error("ShapeSolver 算法无效");
        return detail(cell);
    }
    if (path == "/api/exit") {
        stopped_ = true;
        return "{\"ok\":true}";
    }
    return error("API 不存在");
}

}
