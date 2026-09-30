#pragma once

#include <string>
#include <vector>

#include "core/workspace.h"
#include "game/game.h"

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
    bool setPosition(ObservedBoard::Result board, ShapeSolver::OrderAlgo orderAlgo, std::string &response);
    bool parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolver::OrderAlgo &algo);
    bool setAnalysisBoard(const std::vector<FormField> &fields, bool allowForcedStates, ShapeSolver::OrderAlgo orderAlgo, std::string &response);
    std::string suggest(const Config &config);

    Workspace workspace_;
    std::unique_ptr<GameControl::Position> analysisPosition_;
    bool stopped_ = false;
    std::string token_;
};

}
