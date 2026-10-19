#pragma once

#include "core/datastructure/span.h"
#include "core/datastructure/vector.h"
#include "core/datastructure/vector_pool.h"

namespace mss {

// 多项式视图：coefficients 借用连续数组，调用方保证它比 Poly 活得久。
// 系数下标 i 对应 start + i 次；空数组表示零多项式。
struct Poly {
    int start;
    span<const long double> coefficients;

    const long double *data() const { return coefficients.data; }
    int size() const { return coefficients.size; }
};

// 多项式乘积树：Workspace 只放池和节点，算法是静态函数。
struct PolyProductTree {
    struct Workspace {
        vectorPool<long double> values;
        struct Node {
            int start;
            vectorPool<long double>::vector coefficients;
        };
        // 叶子补到 2 的幂，按二叉树下标存放。
        vector<Node> nodes;
        // 自顶向下求 outside 时的临时节点。
        vector<Node> outside;
        Node unit;
        int factorCount;
        int leafBase;
        int treeHeight;
    };

    // 输入系数数组必须活到调用完成，调用期间不得修改，也不能引用本对象上轮结果。
    // 每个因子回调一次，最后回调全乘积；所有视图只在对应回调期间有效，不返回工作区切片。
    template <typename Callback, typename TotalCallback> static void forEachOutside(Workspace &workspace, span<const Poly> factors, const Callback &callback, const TotalCallback &totalCallback) {
        workspace.factorCount = factors.size;
        workspace.values.data.resize(0);
        workspace.unit.start = 0;
        workspace.unit.coefficients = workspace.values.push_back();
        workspace.values.push_back(workspace.unit.coefficients, 1.0L);
        if (workspace.factorCount == 0) {
            workspace.leafBase = 0;
            workspace.treeHeight = 0;
            workspace.outside.resize(1);
            workspace.outside.data[0] = {0, workspace.unit.coefficients};
            totalCallback(polyOf(workspace, workspace.unit));
            return;
        }
        workspace.leafBase = 1;
        workspace.treeHeight = 0;
        while (workspace.leafBase < workspace.factorCount) {
            workspace.leafBase *= 2;
            ++workspace.treeHeight;
        }
        workspace.nodes.resize(workspace.leafBase);
        workspace.outside.resize(workspace.treeHeight + 1);
        workspace.outside.data[0] = {0, workspace.unit.coefficients};
        // 叶数补到 2 的幂；越界叶子由 unit 作为乘法单位元提供。
        for (int index = workspace.leafBase - 1; index > 0; --index) {
            const Ref left = nodeOf(workspace, index << 1, factors);
            const Ref right = nodeOf(workspace, index << 1 | 1, factors);
            workspace.nodes.data[index] = multiply(workspace, left, right);
        }
        // 每个子树的 outside 等于父 outside 乘兄弟子树；不做多项式除法，零因子也能正确处理。
        visitOutside(workspace, 1, 0, factors, callback);
        totalCallback(workspace.leafBase == 1 ? factors[0] : polyOf(workspace, workspace.nodes.data[1]));
    }

  private:
    // 乘法输入：叶子借用外部系数，内部节点记池内切片；输出追加后再定位指针。
    struct Ref {
        int start;
        int count;
        const long double *external;
        vectorPool<long double>::vector coefficients;
    };

    static Ref refOf(const Workspace::Node &node) {
        Ref ref;
        ref.start = node.start;
        ref.count = (int)node.coefficients.count;
        ref.external = nullptr;
        ref.coefficients = node.coefficients;
        return ref;
    }

    static Poly polyOf(const Workspace &workspace, const Workspace::Node &node) {
        if (node.coefficients.count == 0)
            return Poly{node.start, {}};
        return Poly{node.start, span<const long double>(workspace.values.data.data + node.coefficients.offset, (int)node.coefficients.count)};
    }

    static Ref nodeOf(const Workspace &workspace, int index, span<const Poly> factors) {
        if (index >= workspace.leafBase) {
            const int factorIndex = index - workspace.leafBase;
            if (factorIndex >= workspace.factorCount)
                return refOf(workspace.unit);
            Ref ref;
            ref.start = factors[factorIndex].start;
            ref.count = factors[factorIndex].coefficients.size;
            ref.external = factors[factorIndex].coefficients.data;
            ref.coefficients = {};
            return ref;
        }
        return refOf(workspace.nodes.data[index]);
    }

    static const long double *resolve(const Workspace &workspace, const Ref &ref) {
        return ref.external != nullptr ? ref.external : workspace.values.data.data + ref.coefficients.offset;
    }

    static Workspace::Node multiply(Workspace &workspace, const Ref &left, const Ref &right) {
        Workspace::Node result;
        result.start = left.start + right.start;
        if (left.count == 0 || right.count == 0) {
            result.coefficients = {};
            return result;
        }
        const int size = left.count + right.count - 1;
        result.coefficients = workspace.values.push_back();
        for (int i = 0; i < size; ++i)
            workspace.values.push_back(result.coefficients, 0.0L);
        // 追加可能搬动池，输入指针在追加之后再取。
        const long double *leftData = resolve(workspace, left);
        const long double *rightData = resolve(workspace, right);
        long double *output = workspace.values.data.data + result.coefficients.offset;
        for (int i = 0; i < left.count; ++i)
            for (int j = 0; j < right.count; ++j)
                output[i + j] += leftData[i] * rightData[j];
        return result;
    }

    template <typename Callback> static void visitOutside(Workspace &workspace, int index, int depth, span<const Poly> factors, const Callback &callback) {
        if (index >= workspace.leafBase) {
            const int factorIndex = index - workspace.leafBase;
            if (factorIndex < workspace.factorCount)
                callback(factorIndex, polyOf(workspace, workspace.outside.data[depth]));
            return;
        }
        workspace.outside.data[depth + 1] = multiply(workspace, refOf(workspace.outside.data[depth]), nodeOf(workspace, index << 1 | 1, factors));
        visitOutside(workspace, index << 1, depth + 1, factors, callback);
        workspace.outside.data[depth + 1] = multiply(workspace, refOf(workspace.outside.data[depth]), nodeOf(workspace, index << 1, factors));
        visitOutside(workspace, index << 1 | 1, depth + 1, factors, callback);
    }
};

} // 命名空间 mss
