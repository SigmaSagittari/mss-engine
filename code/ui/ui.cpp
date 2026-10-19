#include "ui/ui.h"

#include "core/config.h"
#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    const JavaEval::Config javaEval(Selection::Config(Pseudo::Config(1e-9L), LongTermRisk::Config(0.025L, 1e-9L), 0.10L, 0.20L, 8), 0.001L, 0.9L);
    const GameControl::Config game{ShapeSolver::Config{35, ShapeSolverConfig::Order::Auto, 15, 8, 50000, 40000.0, 0.1}};
    const JavaBrute::Config javaBrute(1000, BruteForce::Config{false, true, 5}, javaEval);
    const Config config(game, javaBrute, AnytimeSearch::Config(javaEval, 6, 3, 1, false), MonteCarlo::Config(MonteCarlo::PlayoutAlgo::JavaBrute, javaEval, javaBrute, 0.05L, 1.0L));
    Session session(config);
    HttpServer server(session, config);
    server.run();
}

}
