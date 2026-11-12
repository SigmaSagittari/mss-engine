#pragma once

#include "ui/internal/session.h"

namespace mss::UI {

struct HttpServer {
    HttpServer(Session &session);
    void run(bool openBrowser);

  private:
    Session &session_;
};

}
