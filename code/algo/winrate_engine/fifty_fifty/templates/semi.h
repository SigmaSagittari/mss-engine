#pragma once

#include "algo/winrate_engine/fifty_fifty/templates/template_data.h"
#include "game/position.h"

namespace mss {

enum class FiftyFiftyKind;

struct TemplateSemi {
    // templates 已按所需目录前缀编译；输出按格号升序，存在 Forced 时只返回 Forced 格。
    FiftyFiftyKind detect(vector<ObservedBoard::CellId> &cells, const Position::Result &position,
                      const Position::ProbabilityResult &probability, const Probability::Pool &probabilities,
                      const FiftyFiftyData::Pool &templates);

  private:
    struct Facts;
    struct Workspace;
    Workspace &getWorkspace();
    void propagate(Facts &facts, vector<int> &pending, vector<unsigned char> &queued);
    bool matches(const FiftyFiftyData::Pool &templates, const FiftyFiftyData::Rooted &rooted,
                 const Facts &facts, ObservedBoard::CellId root);
};

} // 命名空间 mss
