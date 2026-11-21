#pragma once

#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#ifndef __EMSCRIPTEN__
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include "game/game.h"
#include "ui/ui.h"

namespace mss::UI {

struct Session {
    // 接口失败响应中的 errorCode，与 code/ui/web/i18n.js 的 ApiError 使用同一编号。
    // 编号 34 由浏览器求解线程单独返回。
    enum class Error {
        None = 0, // 行数、列数和雷数均通过校验时的内部返回值。
        TemplatePrefix = 1, // 模板数量须为整数，范围是 1 到目录中的模板总数。
        LtrEventLimit = 2, // 启用长期风险时，参与计算的事件数上限须为正整数。
        BruteLimit = 3, // 暴力接管的可行摆法数阈值须为非负整数，填 0 关闭暴力接管。
        ThreadLimit = 4, // 原生版暴力接管启用多线程时，线程上限须为正整数。
        NodeLimit = 5, // Anytime 自动停止的节点数须为正整数，或填 -1 表示不限。
        SearchWidth = 6, // Anytime 第一步、第二步和后续每步的候选格数量上限均须为正整数。
        BoardParameters = 7, // 行数、列数或雷数缺失或读不成整数。Eff Board 也用此码报告这三项越界。
        BoardRange = 8, // 行列数各须为 1 到 100，雷数须为 0 到格数减 1。Eff Board 用此码报告安全格不足以开出角落零格。
        BoardCells = 9, // 盘面每格须对应一个字符，只允许 0 至 8、?（未揭开）、M（已知雷）、S（已知安全）、G（搜索目标）。
        SolverOrder = 10, // 概率求解器的计算顺序只接受 auto 或 autoSA。
        AnalysisExpired = 11, // 分析编号格式有误，或与 Session 保存的最近一次分析的编号不符。
        CellIndex = 12, // 开局首点或查询详情的格子编号须为 0 到格数减 1 的整数。
        IoeBeginner = 13, // IOE 分析要求初级盘面（9 行 9 列、10 雷）。
        IoeClicks = 14, // IOE 的累计点击数须为非负整数，包含插旗和拔旗。
        IoeMissingClicks = 15, // 盘面已有揭开的格子，累计点击数却为 0。
        PlayerFlags = 16, // 旗位串须与盘面串等长，每格用 0（无旗）或 1（有旗）表示。
        IoeCandidateLimit = 17, // 若传入根筛候选摆法上限，须为正整数。
        IoeDomLimit = 18, // 若传入后续搜索的首次查询上限，须为正整数。
        IoeFlagOnOpen = 19, // 玩家旗落在已揭开的格子上。
        IoeUnsolvable = 20, // 不存在同时满足已揭数字、已知雷格、安全格和总雷数的摆法。
        EffThreshold = 21, // Eff Board 的最低 ZNE 须为有限的正数。
        Seed = 22, // 普通开局和 Eff Board 接收的种子须恰好 32 个十六进制字符，高 64 位在前。
        Minefield = 23, // 自动开格接收的实际雷位须每格一个字符，用 0（安全）或 1（雷）表示。
        MineCount = 24, // 自动开格收到的实际雷位所含雷数与盘面填写的总雷数不符。
        BoardMismatch = 25, // 自动开格时，已揭数字与实际雷位不符，或盘面含人工指定的雷格、安全格。
        SearchTargetMine = 26, // 盘面没有可开的确定安全格，且指定的搜索目标中有确定雷。
        SearchAction = 27, // 搜索控制请求只接受暂停（1）或继续（2）。
        UnknownEndpoint = 28, // 请求的路径没有对应的接口。
        BoardLogicConflict = 29, // 基础推理得出的格子状态、邻雷数或可行总雷数与给定盘面冲突。
        BoardMineCountConflict = 30, // 各连通块单独有解，计入块外未知格后仍拼不出指定的总雷数。
        BoardComponentConflict = 31, // 至少一个连通块找不到满足该块所有数字约束的摆法。
        HttpMethod = 32, // 原生 HTTP 接口收到 POST 以外的请求方法。
        RequestOrigin = 33, // 请求来源与原生服务地址不符，或会话令牌有误。
        IoeRootMemoryLimit = 35, // 若传入根筛前沿表内存上限（MiB），须为正整数。
        IoeMinPercent = 36 // 若传入最低 IOE 百分比，须为正整数。
    };

    Session();
    ~Session();
    std::string index(std::string content) const;
    const std::string &token() const;
    bool stopped() const;
    std::string handle(const std::string &path, const std::string &body);
    std::string analyzeIoe(const std::string &body, const std::function<void(const std::string &)> &publish = {});
    bool startSearch(const std::string &body, std::string &response);
    std::string searchUpdate() const;
    void stopAnalysis();
    // 返回失败响应的 JSON 字符串，调用方按 errorCode 查找提示。
    std::string error(Error code) const;

  private:
    enum class SearchAction { Pause = 1, Resume = 2 };
    struct FormField;
    struct Analysis;
    struct SearchTask;
    struct Workspace;
    Workspace &getWorkspace();
#ifdef __EMSCRIPTEN__
    // 浏览器轮询时分批推进搜索并返回进度。暂停或搜完时返回现有进度。
    std::string advanceSearch();
#endif

    // 解码 application/x-www-form-urlencoded 请求正文，按出现顺序保留字段。
    std::vector<FormField> parseForm(const std::string &requestBody) const;
    // 返回第一个同名字段解码后的文本。缺少该字段时返回空串。
    std::string formFieldText(const std::vector<FormField> &fields, const std::string &fieldName) const;
    // 整段文本须是 int 范围内的十进制整数。返回 true 后才使用 number。
    bool parseInteger(const std::string &decimalText, int &number) const;
    // 读取恰好 32 个十六进制字符，高 64 位在前。返回 true 后才使用 seed。
    bool parseSeed(const std::string &hexText, U128 &seed) const;

    // 创建原生会话时生成一次。令牌随首页发给浏览器，用于核对 API 请求头 X-Mss-Token。
    std::string generateSessionToken() const;
    // 从上到下、每行从左到右生成 cells 字段的内容。0 至 8 是已揭数字，
    // M 是已知雷，S 是已知安全，? 是未揭开的格子。
    std::string encodeBoardCells(const ObservedBoard::Result &board) const;
    // 加上双引号，并转义文本中的引号、反斜杠和控制字符，返回可放入 JSON 的字符串值。
    std::string quoteJsonString(const std::string &text) const;
    // 将已算出的可行摆法数按科学计数法格式化为 JSON 字符串，小数点后保留 6 位。
    std::string candidateCountJson(long double layoutCount) const;
    // 向调用方的响应 JSON 流写入带引号的种子字符串，共 32 个十六进制字符，高 64 位在前。字段名由调用方写。
    void writeSeedJson(std::ostream &responseJson, U128 seed) const;
    // 向响应 JSON 流写入带引号的实际雷位，按行排列，0 表示安全，1 表示雷。字段名由调用方写。
    void writeMinefieldJson(std::ostream &responseJson, const Grid<char> &layout) const;
    // 向响应 JSON 对象追加 probabilities 字段，数组按行排列，每格一个雷率。
    void writeProbabilitiesJson(std::ostream &responseJson, const Position::Result &position, const Probability::BoxResult &boxes, const Probability::Pool &probabilities) const;
    // 按 highlight 请求追加 structureInstances 或 logicComponents 字段，数组中的每组包含要高亮的格号。
    void writeHighlightJson(std::ostream &responseJson, Position::Result &position, Position::Pool &positions, const std::string &highlight) const;
    // 生成搜索进度 JSON，写到 moves 数组结束。发布时再补 paused 字段和最后的右花括号。
    std::string searchUpdateJsonPrefix(const SearchTask &task) const;
    // 向该招法的 JSON 对象追加各数字结局的估值（branches）和各搜索深度的估值（history）。
    void writeMoveDetailsJson(std::ostream &responseJson, const SuggestResult::Details &details, ObservedBoard::CellId cell) const;

    // 读取概率求解器的计算顺序。algo 字段接受 auto 或 autoSA。
    bool parseOrderAlgo(const std::vector<FormField> &fields, ShapeSolverConfig::Order &algo);
    // 读取从模板目录开头取多少个模板，以及启用长期风险时计入多少个事件。失败时把错误 JSON 写入 response。
    bool parseFiftyFifty(const std::vector<FormField> &fields, Config &config, std::string &response);
    // 从表单读取暴力接管、Anytime 和 50-50 设置。失败时把错误 JSON 写入 response。
    bool parseConfig(const std::vector<FormField> &fields, Config &config, std::string &response);
    // 读取行数、列数和总雷数，区分整数格式错误和数值超范围。
    Error parseBoardParameters(const std::vector<FormField> &fields, int &rows, int &cols, int &mines) const;
    // 输出校验后的尺寸、总雷数和格子字符。失败时把错误 JSON 写入 response。
    bool parseBoard(const std::vector<FormField> &fields, int &rows, int &cols, int &mines, std::string &cells, std::string &response);
    // 用已校验的盘面字符构造已知盘面。? 和 G 都表示未揭开的格子，搜索目标由搜索入口另行读取。
    ObservedBoard::Result decodeBoardCells(int rows, int cols, int mines, const std::string &encodedCells) const;
    // 为表单中的固定盘面创建带编号的分析，并检查是否存在可行摆法。失败时把错误 JSON 写入 response。
    bool createAnalysis(const std::vector<FormField> &fields, std::unique_ptr<Analysis> &analysis, std::string &response, bool reuseIoe = false);

    // 分析新盘面，或用 analysisId 查本会话最近一次分析。返回摆法数和请求的雷率、分组或格子详情。
    std::string analyze(const std::vector<FormField> &fields);
    // 从种子生成首点安全的盘面。返回实际雷位、本局采用的种子和下次生成用的种子。
    std::string generateBoard(const std::vector<FormField> &fields);
    // 每次请求筛一批 Eff Board。返回筛选盘数和下批用的种子，命中时附上实际雷位和开局格子。
    std::string generateEffBoard(const std::vector<FormField> &fields);
    // 用本局实际雷位继续自动开确定安全格，直到没有确定安全格或对局结束。返回更新后的盘面。
    std::string playUntilNoSafe(const std::vector<FormField> &fields);
    // 按 action 暂停或继续当前搜索。暂停时保留搜索进度。
    std::string controlSearch(const std::vector<FormField> &fields);

    // 普通分析由请求串行访问，前端引用最近一次普通分析的 id。
    std::unique_ptr<Analysis> analysis_;
    std::unique_ptr<Analysis> ioeAnalysis_; // IOE 先算的盘面，随后可由同盘的普通分析接管。
    int nextAnalysis_;
    bool stopped_;
    std::string token_;
    const Config config_;
    bool searchRunning_;
#ifdef __EMSCRIPTEN__
    std::unique_ptr<SearchTask> searchTask_;
#else
    // 后台任务独占自己的 Analysis；线程先销毁，发布缓存与互斥锁后销毁。
    mutable std::mutex searchMutex_;
    std::condition_variable_any searchWake_;
    std::string searchUpdate_;
    std::jthread searchWorker_;
#endif
};

} // 命名空间 mss::UI
