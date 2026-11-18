#include "ui/ui.h"

#ifndef __EMSCRIPTEN__
#include "ui/internal/http_server.h"
#endif

namespace mss::UI {

Config::Config(bool parallel)
    : annealing{8, 50000, 40000.0, 0.1}, game{{35, ShapeSolverConfig::Order::Auto, 15, nullptr}},
      longTermRisk(true),
      anytime{{{8, 0.10L, 0.20L}, {true, true, true}}, 0.001L, -1, {6, 3, 1}},
      fiftyFifty{100, 15}, brute{1000, {false, parallel ? 5 : 1}} {}

#ifndef __EMSCRIPTEN__
void run(bool openBrowser) {
    Session session;
    HttpServer server(session);
    server.run(openBrowser);
}
#endif

}
