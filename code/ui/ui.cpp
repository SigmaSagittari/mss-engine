#include "ui/ui.h"

#ifndef __EMSCRIPTEN__
#include "ui/internal/http_server.h"
#endif

namespace mss::UI {

MovePolicy::Config Config::policy(const FiftyFifty::Config &fifty) const {
    return {algorithm, &selection, &fifty, &javaEval, &anytime, {brute.limit, &brute.config}};
}

Config::Config(bool parallel)
    : game{{35, ShapeSolverConfig::Order::Auto, 15, {8, 50000, 40000.0, 0.1}}},
      algorithm(MovePolicy::Algorithm::JavaEval), selection{true, true, true, true, 8, 0.10L, 0.20L},
      javaEval{0.001L}, anytime{selection, 0.001L, 1280, {6, 3, 1}},
      fiftyFifty{FiftyFifty::Algorithm::Templates, {0.025L}, {100, 15}}, brute{1000, {false, parallel, parallel ? 5 : 1}} {}

#ifndef __EMSCRIPTEN__
void run(bool openBrowser) {
    Session session;
    HttpServer server(session);
    server.run(openBrowser);
}
#endif

}
