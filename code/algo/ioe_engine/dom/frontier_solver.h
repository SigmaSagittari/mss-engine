#pragma once

#include "algo/ioe_engine/dom/click_graph.h"

namespace mss::ioe_dom {

struct FrontierSolver {
    int compute(const ClickGraph &model, int allowance);

private:
    struct Workspace;
    struct FactorPlan;
    struct Dominance;
    Workspace &getWorkspace();
    void makeFactorPlan(const ClickGraph &model, FactorPlan &plan);
};

} // 命名空间 mss::ioe_dom
