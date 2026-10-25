#include "ui/ui.h"

#include "core/config.h"
#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    const JavaEval::Config javaEval(Selection::Config(0.10L, 0.20L, 8, false, false, true), 0.001L, 0.9L);
    JavaEval::Config searchEval = javaEval;
    searchEval.selection.interchangeable = searchEval.selection.equivalence = searchEval.selection.dominance = true;
    const GameControl::Config game{ShapeSolver::Config{35, ShapeSolverConfig::Order::Auto, 15, 8, 50000, 40000.0, 0.1}};
    const Config config{game, MovePolicy::Basic::Algorithm::JavaEval, javaEval, AnytimeSearch::Config(searchEval, 900, 6, 3, 1), MovePolicy::FiftyFifty::Algorithm::OldJava, FiftyFifty::TemplateConfig(20), LongTermRisk::Config(0.025L), {1000, {false, true, 5}}, MonteCarlo::Config(0.05L, 1.0L)};
    Session session(config);
    HttpServer server(session, config);
    server.run();
}

}
