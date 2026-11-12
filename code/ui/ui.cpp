#include "ui/ui.h"

#ifndef __EMSCRIPTEN__
#include "ui/internal/http_server.h"
#endif

namespace mss::UI {

MovePolicy::Config::Guess Config::guess() const {
    return {anytime.selection.filter, anytime.selection.exchange, longTermRisk,
            {fiftyFifty.prefix, longTermRisk ? &fiftyFifty.eventLimit : nullptr}, &anytime};
}

MovePolicy::Config Config::policy(const MovePolicy::Config::Guess &guess) const {
    return {algorithm, algorithm == MovePolicy::Algorithm::LowRisk ? nullptr : &guess, brute.limit != 0 ? &brute : nullptr};
}

Config::Config(bool parallel)
    : annealing{8, 50000, 40000.0, 0.1}, game{{35, ShapeSolverConfig::Order::Auto, 15, nullptr}},
      algorithm(MovePolicy::Algorithm::Anytime), longTermRisk(true),
      anytime{{{8, 0.10L, 0.20L}, {true, true, true}}, 0.001L, 51200, {6, 3, 1}},
      fiftyFifty{100, 15}, brute{1000, {false, parallel ? 5 : 1}} {}

#ifndef __EMSCRIPTEN__
void run(bool openBrowser) {
    Session session;
    HttpServer server(session);
    server.run(openBrowser);
}
#endif

}
