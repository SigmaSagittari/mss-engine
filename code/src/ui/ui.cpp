#include "ui/ui.h"

#include "core/config.h"
#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    const Config config{Config::Global(5), false, BruteForce::Config::Solver::BitwiseRootParallel, 1000, {0.025L, 0.9L, 1e-9L}, Pseudo::Config(1e-9L), {0.001L, 0.10L, 0.20L, 0.9L, 0.0001L, 4, 1, 8}, {0.05L, 1.0L}};
    Session session(config);
    HttpServer server(session, config);
    server.run();
}

}
