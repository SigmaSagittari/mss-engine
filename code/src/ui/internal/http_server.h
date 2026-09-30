#pragma once

#include "ui/internal/session.h"

namespace mss::UI {

struct HttpServer {
    explicit HttpServer(Session &session);
    void run();

  private:
    Session &session_;
};

}
