#include "ui/internal/http_server.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include "core/config.h"

namespace mss::UI {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string host;
    std::string origin;
    std::string token;
    std::string body;
};

struct WinsockRuntime {
    WSADATA data;
    bool started;

    WinsockRuntime() : started(WSAStartup(MAKEWORD(2, 2), &data) == 0) {}
    ~WinsockRuntime() {
        if (started)
            WSACleanup();
    }
};

std::string lower(std::string value) {
    for (int i = 0; i < (int)value.size(); ++i)
        if (value[i] >= 'A' && value[i] <= 'Z')
            value[i] = (char)(value[i] - 'A' + 'a');
    return value;
}

std::string trim(std::string value) {
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
        value.erase(value.begin());
    while (!value.empty() && (value[value.size() - 1] == ' ' || value[value.size() - 1] == '\t'))
        value.pop_back();
    return value;
}

bool readRequest(SOCKET client, HttpRequest &request) {
    std::string data;
    char buffer[8192];
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return false;
        data.append(buffer, (std::size_t)received);
        if (data.size() > 16384)
            return false;
        headerEnd = data.find("\r\n\r\n");
    }
    const std::size_t requestLineEnd = data.find("\r\n");
    if (requestLineEnd == std::string::npos || requestLineEnd > headerEnd)
        return false;
    const std::string requestLine = data.substr(0, requestLineEnd);
    const std::size_t firstSpace = requestLine.find(' ');
    const std::size_t secondSpace = requestLine.find(' ', firstSpace == std::string::npos ? 0 : firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos)
        return false;
    request.method = requestLine.substr(0, firstSpace);
    request.path = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::size_t contentLength = 0;
    std::size_t lineStart = requestLineEnd + 2;
    while (lineStart < headerEnd) {
        const std::size_t lineEnd = data.find("\r\n", lineStart);
        const std::string line = data.substr(lineStart, lineEnd - lineStart);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string name = lower(trim(line.substr(0, colon)));
            const std::string value = trim(line.substr(colon + 1));
            if (name == "host")
                request.host = value;
            else if (name == "origin")
                request.origin = value;
            else if (name == "x-mss-token")
                request.token = value;
            else if (name == "content-length") {
                const char *begin = value.data();
                const char *end = begin + value.size();
                const std::from_chars_result parsed = std::from_chars(begin, end, contentLength);
                if (parsed.ec != std::errc() || parsed.ptr != end)
                    return false;
            } else if (name == "transfer-encoding" && lower(value) != "identity") {
                return false;
            }
        }
        lineStart = lineEnd + 2;
    }
    if (contentLength > 65536)
        return false;
    const std::size_t bodyStart = headerEnd + 4;
    const std::size_t requiredSize = bodyStart + contentLength;
    while (data.size() < requiredSize) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return false;
        data.append(buffer, (std::size_t)received);
    }
    request.body.assign(data.data() + bodyStart, contentLength);
    return true;
}

bool sendAll(SOCKET client, const std::string &data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const std::size_t remaining = data.size() - sent;
        const int chunkSize = (int)(remaining < 8192 ? remaining : 8192);
        const int count = send(client, data.data() + sent, chunkSize, 0);
        if (count <= 0)
            return false;
        sent += count;
    }
    return true;
}

bool sendResponse(SOCKET client, int status, const std::string &contentType, const std::string &body) {
    std::string reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Internal Server Error";
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: " + contentType + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n" + body;
    return sendAll(client, response);
}

std::string readAsset(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::filesystem::path assetDirectory() {
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    return std::filesystem::path(executable).parent_path() / L"ui";
}

HttpServer::HttpServer(Session &session, const Config &config) : session_(session), config_(config) {}

void HttpServer::run() {
    WinsockRuntime runtime;
    if (!runtime.started) {
        MessageBoxW(nullptr, L"无法启动本地网页服务。", L"mss0.16", MB_OK | MB_ICONERROR);
        return;
    }
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        MessageBoxW(nullptr, L"无法创建本地网页服务。", L"mss0.16", MB_OK | MB_ICONERROR);
        return;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    // 服务只监听回环接口；API 请求还会校验当前页面来源和本次进程令牌。
    if (bind(listener, (sockaddr *)&address, sizeof(address)) == SOCKET_ERROR || listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listener);
        MessageBoxW(nullptr, L"无法监听本地网页服务端口。", L"mss0.16", MB_OK | MB_ICONERROR);
        return;
    }
    int addressLength = sizeof(address);
    if (getsockname(listener, (sockaddr *)&address, &addressLength) == SOCKET_ERROR) {
        closesocket(listener);
        MessageBoxW(nullptr, L"无法取得本地网页服务端口。", L"mss0.16", MB_OK | MB_ICONERROR);
        return;
    }
    const unsigned short port = ntohs(address.sin_port);
    const std::string host = "127.0.0.1:" + std::to_string(port);
    const std::string origin = "http://" + host;
    std::printf("mss%s UI: %s/\n", mss::version, origin.c_str());
    std::fflush(stdout);
    // 此线程随后阻塞在 accept，须同步完成默认浏览器的激活。
    const std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
    const HRESULT browserCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(browserCom)) {
        closesocket(listener);
        MessageBoxW(nullptr, L"无法初始化默认浏览器启动环境。", L"mss0.16", MB_OK | MB_ICONERROR);
        return;
    }
    SHELLEXECUTEINFOW browser{};
    browser.cbSize = sizeof(browser);
    browser.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    browser.hwnd = GetConsoleWindow();
    browser.lpVerb = L"open";
    browser.lpFile = url.c_str();
    browser.nShow = SW_SHOWNORMAL;
    const BOOL browserStarted = ShellExecuteExW(&browser);
    CoUninitialize();
    if (!browserStarted) {
        std::printf("默认浏览器启动失败，请手动打开上面的本地地址。\n");
        std::fflush(stdout);
    }
    const std::filesystem::path assets = assetDirectory();
    std::jthread analysisStream;
    while (!session_.stopped()) {
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET)
            break;
        DWORD timeout = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        HttpRequest request;
        if (!readRequest(client, request)) {
            sendResponse(client, 400, "text/plain; charset=utf-8", "Bad Request");
            closesocket(client);
            continue;
        }
        if (request.host != host) {
            sendResponse(client, 403, "text/plain; charset=utf-8", "Forbidden");
        } else if (request.path == "/" || request.path == "/state.js" || request.path == "/game.js" || request.path == "/view.js" || request.path == "/analysis.js" || request.path == "/monte_carlo.js" || request.path == "/anytime_search.js" || request.path == "/main.js" || request.path == "/style.css") {
            if (request.method != "GET") {
                sendResponse(client, 405, "text/plain; charset=utf-8", "Method Not Allowed");
            } else {
                const std::string assetName = request.path == "/" ? "index.html" : request.path.substr(1);
                std::string content = readAsset(assets / assetName);
                if (request.path == "/") {
                    const std::string marker = "__MSS_TOKEN__";
                    content.replace(content.find(marker), marker.size(), session_.token());
                    const Config &config = config_;
                    const std::array<const char *, 3> algorithmMarkers{"__MSS_POLICY_LOWRISK__", "__MSS_POLICY_JAVA__", "__MSS_POLICY_ANYTIME__"};
                    for (int i = 0; i < 3; ++i) content.replace(content.find(algorithmMarkers[i]), std::string(algorithmMarkers[i]).size(), config.algorithm == (MovePolicy::Basic::Algorithm)i ? "selected" : "");
                    const std::array<const char *, 2> fiftyMarkers{"__MSS_FIFTY_OLDJAVA__", "__MSS_FIFTY_TEMPLATES__"};
                    for (int i = 0; i < 2; ++i) content.replace(content.find(fiftyMarkers[i]), std::string(fiftyMarkers[i]).size(), config.fiftyFifty.algorithm == (FiftyFifty::Algorithm)i ? "selected" : "");
                    const std::string prefixMarker = "__MSS_TEMPLATE_PREFIX__", countMarker = "__MSS_TEMPLATE_COUNT__";
                    content.replace(content.find(prefixMarker), prefixMarker.size(), std::to_string(config.fiftyFifty.templates.prefix));
                    content.replace(content.find(countMarker), countMarker.size(), std::to_string(FiftyFifty().templateCount()));
                    const std::string ltrEventLimitMarker = "__MSS_LTR_EVENT_LIMIT__";
                    content.replace(content.find(ltrEventLimitMarker), ltrEventLimitMarker.size(), std::to_string(config.fiftyFifty.templates.risk.eventLimit));
                    const std::string thresholdMarker = "__MSS_BRUTE_FORCE_THRESHOLD__";
                    content.replace(content.find(thresholdMarker), thresholdMarker.size(), std::to_string(config.brute.limit));
                    const std::string threadsMarker = "__MSS_THREAD_LIMIT__";
                    content.replace(content.find(threadsMarker), threadsMarker.size(), std::to_string(config.brute.config.threadCount));
                    const std::string checkAllMarker = "__MSS_BRUTE_FORCE_CHECK_ALL__";
                    content.replace(content.find(checkAllMarker), checkAllMarker.size(), config.brute.config.checkAllMoves ? "checked" : "");
                    const std::string multiThreadMarker = "__MSS_BRUTE_FORCE_MULTITHREAD__";
                    content.replace(content.find(multiThreadMarker), multiThreadMarker.size(), config.brute.config.parallel ? "checked" : "");
                    const std::array<const char *, 3> widthMarkers{"__MSS_SEARCH_ROOT_WIDTH__", "__MSS_SEARCH_MIDDLE_WIDTH__", "__MSS_SEARCH_DEEP_WIDTH__"};
                    for (int i = 0; i < 3; ++i) content.replace(content.find(widthMarkers[i]), std::string(widthMarkers[i]).size(), std::to_string(config.anytime.width[i]));
                    const std::string nodeMarker = "__MSS_POLICY_NODES__";
                    content.replace(content.find(nodeMarker), nodeMarker.size(), std::to_string(config.anytime.nodeLimit));
                    const std::string versionMarker = "__MSS_VERSION__";
                    std::size_t version = content.find(versionMarker);
                    while (version != std::string::npos) {
                        content.replace(version, versionMarker.size(), mss::version);
                        version = content.find(versionMarker, version + std::string(mss::version).size());
                    }
                }
                const std::string contentType = request.path == "/" ? "text/html; charset=utf-8" : request.path == "/style.css" ? "text/css; charset=utf-8" : "text/javascript; charset=utf-8";
                sendResponse(client, content.empty() ? 404 : 200, contentType, content);
            }
        } else if (request.path.rfind("/api/", 0) == 0) {
            if (request.method != "POST") {
                sendResponse(client, 405, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"只接受 POST 请求\"}");
            } else if (request.origin != origin || request.token != session_.token()) {
                sendResponse(client, 403, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"请求来源无效\"}");
            } else if (request.path == "/api/mcts/stop" || request.path == "/api/anytime-search/stop") {
                analysisStream.request_stop();
                session_.stopMcts();
                session_.stopSearch();
                sendResponse(client, 200, "application/json; charset=utf-8", "{\"ok\":true}");
            } else if (request.path == "/api/mcts/start" || request.path == "/api/anytime-search/start") {
                analysisStream.request_stop();
                session_.stopMcts();
                session_.stopSearch();
                if (analysisStream.joinable())
                    analysisStream.join();
                std::string response;
                const bool search = request.path == "/api/anytime-search/start";
                if (!(search ? session_.startSearch(request.body, response) : session_.startMcts(request.body, response))) {
                    sendResponse(client, 200, "application/json; charset=utf-8", response);
                } else if (sendAll(client, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n")) {
                    DWORD sendTimeout = 1000;
                    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&sendTimeout, sizeof(sendTimeout));
                    analysisStream = std::jthread([this, client, search](std::stop_token stop) {
                        std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now();
                        while (!stop.stop_requested()) {
                            next += std::chrono::milliseconds(100);
                            std::this_thread::sleep_until(next);
                            if (stop.stop_requested() || !sendAll(client, "data: " + (search ? session_.searchUpdate() : session_.mctsUpdate()) + "\n\n"))
                                break;
                        }
                        session_.stopMcts();
                        session_.stopSearch();
                        closesocket(client);
                    });
                    continue;
                } else {
                    session_.stopMcts();
                    session_.stopSearch();
                }
            } else if (request.path == "/api/anytime-search/control") {
                sendResponse(client, 200, "application/json; charset=utf-8", session_.controlSearch(request.body));
            } else {
                sendResponse(client, 200, "application/json; charset=utf-8", session_.handle(request.path, request.body));
            }
        } else {
            sendResponse(client, 404, "text/plain; charset=utf-8", "Not Found");
        }
        closesocket(client);
    }
    analysisStream.request_stop();
    session_.stopMcts();
    session_.stopSearch();
    if (analysisStream.joinable())
        analysisStream.join();
    closesocket(listener);
}

}
