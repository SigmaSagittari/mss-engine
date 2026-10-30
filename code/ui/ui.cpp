#include "ui/ui.h"

#include "core/config.h"
#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    const JavaEval::Config javaEval(0.10L, 0.20L, 8, false, false, true, 0.001L, 0.9L);
    JavaEval::Config searchEval = javaEval;
    searchEval.interchangeable = searchEval.equivalence = searchEval.dominance = true;
    const GameControl::Config game{ShapeSolver::Config{35, ShapeSolverConfig::Order::Auto, 15, {8, 50000, 40000.0, 0.1}}};
    const Config config{game, MovePolicy::Basic::Algorithm::JavaEval, javaEval, AnytimeSearch::Config(searchEval, 1280, 6, 3, 1), {FiftyFifty::Algorithm::Templates, LongTermRisk::Config(0.025L), FiftyFifty::TemplateConfig(100, TemplateLongTermRisk::Config(15))}, {1000, {false, true, 5}}, MonteCarlo::Config(0.05L, 1.0L)};
    Session session(config);
    HttpServer server(session, config);
    server.run();
}

}
