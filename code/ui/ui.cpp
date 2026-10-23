#include "ui/ui.h"

#include "core/config.h"
#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    const JavaEval::Config javaEval(Selection::Config(0.10L, 0.20L, 8), 0.001L, 0.9L);
    const GameControl::Config game{ShapeSolver::Config{35, ShapeSolverConfig::Order::Auto, 15, 8, 50000, 40000.0, 0.1}};
    const Config config{game, MovePolicy::Basic::Algorithm::JavaEval, javaEval, AnytimeSearch::Config(javaEval, 256, 6, 3, 1, false), MovePolicy::FiftyFifty::Algorithm::OldJava, Pseudo::Config(1e-9L), FiftyFifty::TemplateConfig(20, 1e-9L), LongTermRisk::Config(0.025L, 1e-9L), {1000, {false, true, 5}}, MonteCarlo::Config(0.05L, 1.0L)};
    Session session(config);
    HttpServer server(session, config);
    server.run();
}

}
