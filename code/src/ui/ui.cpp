#include "ui/ui.h"

#include "core/config.h"
#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    const Config config{BruteForce::Config{false, BruteForce::Config::Solver::BitwiseRootParallel, 1000, 5}, {0.025L, 1e-9L}, Pseudo::Config(1e-9L), {0.001L, 0.9L}, {0.10L, 0.20L, 8}, {{6, 3, 1}, false}, {0.05L, 1.0L}};
    Session session(config);
    HttpServer server(session, config);
    server.run();
}

}
