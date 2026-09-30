#include "ui/ui.h"

#include "ui/internal/http_server.h"

namespace mss::UI {

void run() {
    Session session;
    HttpServer server(session);
    server.run();
}

}
 