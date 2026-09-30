#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/shape_solver/shape_solver.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/config.h"
#include "core/utility/bit_mask.h"
#include "core/utility/dynamic_bitset.h"
#include "core/utility/flat_hashtable.h"
#include "core/utility/grid.h"
#include "core/utility/hash.h"

namespace mss {

struct BruteForce {
    struct Result {
        struct Move {
            // 按当前棋盘尺寸编码的 0-based CellId。
            ObservedBoard::CellId cell = -1;
            // 点击此格后能保证继续获胜的完整布局数，不是概率。
            int wins = 0;
        };

        // 后端递归访问的节点数；并行模式为所有工作线程的合计。
        long long nodes = 0;
        // 全候选模式按候选顺序排列；单步模式为空或只有一个推荐步。
        std::vector<Move> moves;
    };

  private:
    using ConfigId = std::uint32_t;
    using CandidateId = std::uint32_t;

  public:
    struct Workspace {
        // DFS 赋值枚举复用此工作区；必须与传入 ShapeSolver 的对象相同，且不能并发共享。
        ShapeSolver::Workspace &shapeSolver;

        explicit Workspace(ShapeSolver::Workspace &shapeSolver);

        struct CommonSession {
            struct Candidate {
                ObservedBoard::CellId cell = -1;
                // links 中该候选邻居区间的起点和数量；每格最多 8 个邻居。
                std::uint32_t linksOffset = 0;
                std::uint8_t linksCount = 0;
                // 不在候选集合里的 F 邻居数，预先并入揭示数字。
                std::uint8_t fixedMines = 0;
            };

            int candidateCount = 0;
            // 完整雷位方案数量；每个方案对应一个 ConfigId。
            int possibilityCount = 0;
            std::vector<Candidate> candidates;
            // 候选邻居的 CandidateId 连续存储，由 linksOffset/linksCount 定位。
            std::vector<int> links;
            // 每行恰有这么多项；组件模式由调用方给定，全盘模式由剩余总雷数确定。
            int minesPerConfig = 0;
            // [ConfigId][雷序号] 的候选下标表；布局枚举后供两个后端共同读取。
            RawGrid<CandidateId> mineCandidateIds;
        } commonSession;

        struct Build {
            // CellId → CandidateId；无候选的位置为 -1。
            std::vector<int> candidateAt;
            // 全盘入口中的 T 格，组件入口中的局外格都作为自由选雷位置。
            std::vector<CandidateId> tCells;
            // 枚举当前方案时临时放入的雷位置；递归返回时恢复为空。
            std::vector<CandidateId> placed;
            // 每个组件的 DFS 赋值连续存储，偏移和数量按组件下标记录。
            std::vector<int> assignmentOffsets;
            std::vector<int> assignmentCounts;
            std::vector<char> assignments;
        } build;

        struct Normal {
            // 每一层保存一个递归深度的暂存；递归子调用只写更深层，返回后父层缓冲仍有效。
            struct Layer {
                // 当前方案集合中每个候选被判为雷的方案数，以及按其排序的尝试顺序。
                std::vector<int> deaths;
                std::vector<int> safeCells;
                std::vector<int> order;
                // 按点击揭示数字 0~8 分桶；认为候选为雷的方案不进入这些桶。
                std::array<std::vector<ConfigId>, 9> groups;
                std::vector<std::pair<int, int>> groupList;
                std::vector<U128> safeHashes;
                std::vector<int> safeGroupIds;
                std::vector<int> safeGroupSizes;
                std::vector<int> safeGroupOffsets;
                std::vector<ConfigId> safeGroupedConfigs;
                // 每项是 safeGroupedConfigs 中一个组的 {起点, 数量}，不持有跨层视图。
                std::vector<std::pair<int, int>> safeGroupList;
            };

            Layer &layer(int depth);
            void reset();

            // [ConfigId][CandidateId]；雷表与揭示表在一次搜索期间只读。
            RawGrid<std::uint8_t> mineByConfig;
            RawGrid<std::uint8_t> revealByConfig;
            // 递归路径上的尚未点击候选格集合，返回时每个分支都恢复原状。
            DynamicBitset unopenedCandidates;
            std::vector<ConfigId> configIds;
            // safeGroupTable 将相同揭示向量映射到分组编号。
            FlatHashTable<U128, int, U128Hash> safeGroupTable;
            // 正值缓存精确胜局数，负值缓存未达阈值时可证明的胜局上界。
            FlatHashTable<U128, int, U128Hash> cache;
            std::deque<Layer> layers;
            long long nodes = 0;
        } normal;

        struct MultiMask {
            template <typename Mask> struct Layout {
                // 只读的完整布局表；线程只共享引用，不复制这三张大表。
                std::vector<Mask> mineMaskByConfig;
                RawGrid<std::uint64_t> mineByteWordsByConfig;
                RawGrid<std::uint8_t> revealByConfig;
            };

            template <typename Mask> struct Session {
                const Layout<Mask> &layout;
                // 每个搜索线程独有的未开候选掩码和节点计数。
                Mask unopenedCandidates;
                long long nodes = 0;
            };

            struct Layer {
                // 每层缓冲对应一个递归深度；groups 按 0~8 的安全揭示值分桶。
                std::vector<int> deaths;
                std::vector<int> order;
                std::array<std::vector<ConfigId>, 9> groups;
                std::vector<std::pair<int, int>> groupList;
                std::vector<int> safeGroupIds;
                std::vector<int> safeGroupSizes;
                std::vector<int> safeGroupOffsets;
                std::vector<ConfigId> safeGroupedConfigs;
                std::vector<std::pair<int, int>> safeGroupList;
            };

            struct Scratch {
                Layer &layer(int depth);
                void reset();

                std::deque<Layer> layers;
                FlatHashTable<U128, int, U128Hash> safeGroupTable;
                // 仅 Java-lite 排序时使用的固定容量计数，不随递归深度分配。
                std::array<std::array<int, bitMask<8>::kBitCount>, 9> javaLiteMineCounts{};
                std::array<int, 9> javaLiteGroupSizes{};
                std::array<int, 9 * 9 * 9> smallGroupCounts{};
                std::vector<int> smallGroupKeys;
                std::vector<int> smallGroupConfigKeys;
            };

            struct Worker {
                // 每个并行线程独占 scratch/cache；useSharedCache 只在根并行任务期间开启。
                Scratch scratch;
                FlatHashTable<U128, int, U128Hash> cache;
                bool useSharedCache = false;
            } main;

            struct Parallel {
                struct SharedCache {
                    struct Shard {
                        std::mutex mutex;
                        FlatHashTable<U128, int, U128Hash> table;
                    };

                    // 根并行线程共享此缓存；按键分片加锁，避免所有子问题争用同一把锁。
                    // 256 个分片沿用 1.4 的并行缓存设置，只影响锁争用；1.5 尚未重新调参。
                    std::array<Shard, 256> shards;
                    // 每个 ConfigId 的预混哈希，供并行线程用同一套无序集合键访问分片缓存。
                    std::vector<std::uint64_t> hashes;

                    void reset(int configCount);
                    U128 hashConfigs(const std::vector<ConfigId> &configs, int start, int count) const;
                    int find(const U128 &key);
                    void store(const U128 &key, int value);
                } sharedCache;

                // 根候选排序及各 worker 的上界、节点数均由并行调度独占复用。
                std::vector<int> order;
                std::vector<int> deaths;
                std::vector<int> uppers;
                std::vector<long long> nodes;
                std::vector<Worker> workers;
                std::vector<std::jthread> threads;
            } parallel;

            // 根据候选格数选择最窄掩码布局；variant 只保存当前宽度的可复用数据。
            std::variant<Layout<bitMask<1>>, Layout<bitMask<2>>, Layout<bitMask<4>>, Layout<bitMask<8>>> layouts;
            // 本次方案的连续编号表，供不同 worker 只读共享。
            std::vector<ConfigId> configIds;
        } multiMask;
    };

    // board、basic、structure 必须描述同一观测状态，shapes 必须是 structure 所属的池。
    // checkAllMoves 为 true 时逐一返回所有 H/T 候选格；否则只按 minWins 搜索并返回一条推荐步。
    static Result solve(const ObservedBoard::Result &board, const Basic::Result &basic, const Structure::Result &structure, const Structure::Pool &shapes, const Config &options, Workspace &workspace);
    // components 按对应 Structure 池中的 InstanceId 给出，offFrontierCells 是自由选雷格；两者合计恰有 mines 个雷。
    // 输出模式仍只由 checkAllMoves 决定，不会因入口类型自动展开所有候选格。
    static Result solveComponent(const ObservedBoard::Result &board, const Basic::Result &basic, const std::vector<Structure::InstanceId> &components, const std::vector<ObservedBoard::CellId> &offFrontierCells, const Structure::Pool &shapes, int mines, const Config &options, Workspace &workspace);

  private:
    using CommonSession = Workspace::CommonSession;

    struct NormalSolver;
    template <typename Mask> struct MultiMaskSolver;
    template <typename Mask> struct MultiMaskParallelSolver;

    // 以下三个阈值原样沿用 1.4，1.5 尚未重新调参。
    // 方案数低于此值时跳过候选打分，避免小残局支付排序成本。
    static constexpr int javaLiteConfigThreshold = 10000;
    // 方案数低于此值时不启动根并行，避免线程开销。
    static constexpr int parallelConfigThreshold = 4096;
    // 子问题小于此值时用线程私有缓存，避免共享锁开销。
    static constexpr int sharedConfigThreshold = 32;

    static void buildCommonSession(CommonSession &session, const ObservedBoard::Result &board, const Basic::Result &basic,
                                   const Structure::Result &structure, const Structure::Pool &shapes, Workspace &workspace);
    static void buildComponentCommonSession(CommonSession &session, const ObservedBoard::Result &board, const Basic::Result &basic,
                                            const std::vector<Structure::InstanceId> &components,
                                            const std::vector<ObservedBoard::CellId> &offFrontierCells,
                                            const Structure::Pool &shapes, int mines, Workspace &workspace);
    static void populateCommonSession(CommonSession &session, const ObservedBoard::Result &board, const Basic::Result &basic,
                                      const Structure::Pool &shapes, const std::vector<Structure::InstanceId> &components,
                                      Workspace &workspace, int mines);
    static void enumerateComponents(CommonSession &session, const Structure::Pool &shapes,
                                    const std::vector<Structure::InstanceId> &components, Workspace &workspace,
                                    int component, int usedMines, bool writeConfigs);
    static void enumerateBoxCells(CommonSession &session, const Structure::Pool &shapes,
                                  const std::vector<Structure::InstanceId> &components, Workspace &workspace,
                                  int component, int assignmentStart, int box, int cellStart, int remaining,
                                  int componentMines, int usedMines, bool writeConfigs);
    static void enumerateTCells(CommonSession &session, Workspace &workspace, int start, int remaining, bool writeConfigs);
    static void writeConfig(CommonSession &session, Workspace &workspace, bool writeConfigs);
    static Result solveCommonSession(const CommonSession &common, const Config &options, Workspace &workspace);
    static Result runNormal(const CommonSession &common, const Config &options, Workspace::Normal &workspace);
    template <typename Mask> static Result solveWithMask(const CommonSession &common, const Config &options, Workspace &workspace);
    static U128 hashConfigs(const std::vector<ConfigId> &configs, int start, int count);
    static void saveFail(const U128 &key, int upper, int count, FlatHashTable<U128, int, U128Hash> &table);
};

} // 命名空间 mss
