#pragma once

#include "ui/internal/session.h"

namespace mss::UI {

struct HttpServer {
    HttpServer(Session &session, const Config &config);
    void run();

  private:
    Session &session_;
    const Config &config_;
};

}
