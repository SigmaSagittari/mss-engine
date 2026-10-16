#pragma once

#include <utility>
#include <vector>

namespace mss {

// Poly 借用连续系数数组；coefficients[i] 对应 start+i 次，空数组表示零多项式。
struct Poly {
    int start;
    const std::vector<long double> &coefficients;

    Poly(int start, const std::vector<long double> &coefficients) : start(start), coefficients(coefficients) {}

    // 将两个多项式卷积到调用方缓冲区；输出 vector 不得与任一输入系数 vector 别名，返回值借用输出。
    static Poly multiply(const Poly &left, const Poly &right, std::vector<long double> &coefficients) {
        const int start = left.start + right.start;
        if (left.coefficients.empty() || right.coefficients.empty()) {
            coefficients.clear();
            return Poly(start, coefficients);
        }
        const int size = left.coefficients.size() + right.coefficients.size() - 1;
        coefficients.assign(size, 0.0L);
        for (int i = 0; i < (int)left.coefficients.size(); ++i)
            for (int j = 0; j < (int)right.coefficients.size(); ++j)
                coefficients[i + j] += left.coefficients[i] * right.coefficients[j];
        return Poly(start, coefficients);
    }
};

// 可复用的多项式乘积树；空因子集合的乘积为 1，逐项排除结果通过回调交付。
class PolyProductTree {
  public:
    PolyProductTree() : factorCount_(0), leafBase_(0), treeHeight_(0) {}

    // 输入系数 vector 至少活到调用完成，调用期间不得修改，且不能引用本对象上轮结果；单因子时总乘积继续借用输入。
    // 非空输入要求有效回调；每个因子回调一次，排除乘积只在回调期间有效。返回的总乘积视图下次调用后失效。
    // 同一对象不得并发或重入调用。
    template <typename Callback> Poly forEachOutside(const std::vector<Poly> &factors, const Callback &callback) {
        factorCount_ = factors.size();
        if (factorCount_ == 0) {
            leafBase_ = 0;
            treeHeight_ = 0;
            if (outsideScratch_.empty())
                outsideScratch_.resize(1, Node{0, {}});
            outsideScratch_[0].start = 0;
            outsideScratch_[0].coefficients.assign(1, 1.0L);
            return Poly(0, outsideScratch_[0].coefficients);
        }
        leafBase_ = 1;
        treeHeight_ = 0;
        while (leafBase_ < factorCount_) {
            leafBase_ *= 2;
            ++treeHeight_;
        }
        // 工作区只增不减；因子变少或暂时为空时保留系数容量供后续调用复用。
        if ((int)productTree_.size() < leafBase_)
            productTree_.resize(leafBase_, Node{0, {}});
        if ((int)outsideScratch_.size() < treeHeight_ + 1)
            outsideScratch_.resize(treeHeight_ + 1, Node{0, {}});
        outsideScratch_[0].start = 0;
        outsideScratch_[0].coefficients.assign(1, 1.0L);
        // 叶数补到 2 的幂；越界叶子由 node() 作为乘法单位元提供。
        for (int i = leafBase_ - 1; i > 0; --i)
            productTree_[i].start = Poly::multiply(node(i << 1, factors), node(i << 1 | 1, factors), productTree_[i].coefficients).start;
        // 每个子树的 outside 等于父 outside 乘兄弟子树；不做多项式除法，零因子也能正确处理。
        visitOutside(1, 0, factors, callback);
        if (leafBase_ == 1)
            return factors[0];
        return Poly(productTree_[1].start, productTree_[1].coefficients);
    }

  private:
    struct Node {
        int start;
        std::vector<long double> coefficients;

        // 新增存储槽以空系数缓冲创建；实际多项式由本轮计算覆盖。
        Node(int start, std::vector<long double> coefficients) : start(start), coefficients(std::move(coefficients)) {}
    };

    Poly node(int index, const std::vector<Poly> &factors) const {
        // 叶子借用输入系数；填充叶子是单位元；非叶子读取已构造的乘积节点。
        if (index >= leafBase_) {
            const int factorIndex = index - leafBase_;
            if (factorIndex >= factorCount_)
                return Poly(0, outsideScratch_[0].coefficients);
            return factors[factorIndex];
        }
        return Poly(productTree_[index].start, productTree_[index].coefficients);
    }

    template <typename Callback> void visitOutside(int index, int depth, const std::vector<Poly> &factors, const Callback &callback) {
        if (index >= leafBase_) {
            const int factorIndex = index - leafBase_;
            if (factorIndex < factorCount_) {
                const Poly outside(outsideScratch_[depth].start, outsideScratch_[depth].coefficients);
                callback(factorIndex, outside);
            }
            return;
        }
        const Poly outside(outsideScratch_[depth].start, outsideScratch_[depth].coefficients);
        outsideScratch_[depth + 1].start = Poly::multiply(outside, node(index << 1 | 1, factors), outsideScratch_[depth + 1].coefficients).start;
        visitOutside(index << 1, depth + 1, factors, callback);
        outsideScratch_[depth + 1].start = Poly::multiply(outside, node(index << 1, factors), outsideScratch_[depth + 1].coefficients).start;
        visitOutside(index << 1 | 1, depth + 1, factors, callback);
    }

    int factorCount_;
    int leafBase_;
    int treeHeight_;
    std::vector<Node> productTree_;
    std::vector<Node> outsideScratch_;
};

}
