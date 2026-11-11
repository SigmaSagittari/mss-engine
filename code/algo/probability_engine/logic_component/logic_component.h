#pragma once

#include "algo/probability_engine/basic/basic.h"
#include "algo/probability_engine/observed_board/observed_board.h"
#include "algo/probability_engine/structure/structure.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

struct LogicComponent {
    struct Result {
        struct Component {
            // 两段切片分别来自同一个 Pool 的 instanceIds 和 tCells。
            // 扩容后仍有效，清空、销毁或截掉对应段后失效。
            // instanceIds 中的 ID 指向传给 analyze 的 instances.list。
            vectorPool<Structure::InstanceId>::vector instanceIds;
            vectorPool<ObservedBoard::CellId>::vector tCells;

            Component(vectorPool<Structure::InstanceId>::vector instanceIds, vectorPool<ObservedBoard::CellId>::vector tCells)
                : instanceIds(instanceIds), tCells(tCells) {}
        };

        vector<Component> components;
    };

    struct Pool {
        vectorPool<Structure::InstanceId> instanceIds;
        vectorPool<ObservedBoard::CellId> tCells;
    };

    // 输入来自同一盘面，instances 是 structure.instanceIds 所引用的实例池。
    // 覆盖 result 和 pool 中的旧分块，复用容量。
    void analyze(Result &result, const ObservedBoard::Result &board, const Basic::Result &basic,
                 const Structure::Result &structure, const Structure::Pool::Instances &instances, Pool &pool);

  private:
    struct Workspace;
    Workspace &getWorkspace();
};

} // 命名空间 mss
