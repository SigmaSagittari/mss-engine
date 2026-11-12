#include "ui/ui.h"

#ifndef __EMSCRIPTEN__
#include "ui/internal/http_server.h"
#endif

namespace mss::UI {

MovePolicy::Config::Guess Config::guess() const {
    const bool templates = fiftyFifty.algorithm == FiftyFifty::Algorithm::Templates;
    return {anytime.selection.filter, algorithm == MovePolicy::Algorithm::Anytime ? &anytime.selection.exchange : nullptr, longTermRisk,
            {fiftyFifty.algorithm, templates ? nullptr : &fiftyFifty.oldJava, templates ? &fiftyFifty.templates : nullptr,
             templates && longTermRisk ? &fiftyFifty.eventLimit : nullptr}, &javaEval, &anytime};
}

MovePolicy::Config Config::policy(const MovePolicy::Config::Guess &guess) const {
    return {algorithm, algorithm == MovePolicy::Algorithm::LowRisk ? nullptr : &guess, brute.limit != 0 ? &brute : nullptr};
}

Config::Config(bool parallel)
    : annealing{8, 50000, 40000.0, 0.1}, game{{35, ShapeSolverConfig::Order::Auto, 15, nullptr}},
      algorithm(MovePolicy::Algorithm::JavaEval), longTermRisk(true),
      javaEval{0.001L}, anytime{{{8, 0.10L, 0.20L}, {true, true, true}}, 0.001L, 1280, {6, 3, 1}},
      fiftyFifty{FiftyFifty::Algorithm::Templates, {0.025L}, {100}, 15}, brute{1000, {false, parallel ? 5 : 1}} {}

#ifndef __EMSCRIPTEN__
void run(bool openBrowser) {
    Session session;
    HttpServer server(session);
    server.run(openBrowser);
}
#endif

}
