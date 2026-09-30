#pragma once

#include <string>
#include <vector>

#include "core/workspace.h"

namespace mss::UI {

struct Session {
    Session();

    const std::string &token() const;
    bool stopped() const;
    std::string handle(const std::string &path, const std::string &body);

  private:
    struct FormField {
        std::string key;
        std::string value;
    };

    static std::vector<FormField> parseForm(const std::string &body);
    static std::string jsonString(const std::string &value);
    static std::string fixedProbability(long double value);
    static bool parseInteger(const std::string &value, int &result);
    static std::string cellString(const ObservedBoard::Result &board);
    static std::string probabilitiesJson(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Probability::Result &probability);
    static mss::U128 makeSeed();

    std::string value(const std::vector<FormField> &fields, const std::string &key) const;
    std::string error(const std::string &message) const;
    std::string analysisState() const;
    std::string analysisResult();
    std::string detail(int cell);
    bool calculateAnalysis();
    bool setOrderAlgo(const std::vector<FormField> &fields);
    bool setAnalysisBoard(const std::vector<FormField> &fields, bool allowForcedStates, std::string &response);
    // 按完整布局数选择 JavaEval 或 BruteForce，并返回推荐首步。
    std::string analyzeEndgame(int engineThreshold, bool includeProbabilities, bool checkAllMoves, bool multiThread);

    Workspace workspace_;
    Structure::Pool analysisShapes_;
    ShapeSolver::DistributionPool analysisDistributions_;
    ObservedBoard::Result analysisBoard_;
    Basic::Result analysisBasic_;
    Structure::Result analysisStructure_;
    Probability::Result analysisProbability_;
    ShapeSolver::OrderAlgo orderAlgo_ = ShapeSolver::OrderAlgo::Auto;
    bool analysisDirty_ = true;
    bool analysisReady_ = false;
    bool stopped_ = false;
    std::string token_;
    std::string analysisError_;
};

}
