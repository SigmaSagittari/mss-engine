#include "core/utility/poly.h"

namespace mss {

Poly Poly::multiply(const Poly &left, const Poly &right, std::vector<long double> &coefficients) {
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

Poly PolyProductTree::forEachOutside(const std::vector<Poly> &factors, const std::function<void(int, const Poly &)> &callback) {
    factorCount_ = factors.size();
    if (factorCount_ == 0) {
        leafBase_ = 0;
        treeHeight_ = 0;
        if (outsideScratch_.empty())
            outsideScratch_.resize(1);
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
        productTree_.resize(leafBase_);
    if ((int)outsideScratch_.size() < treeHeight_ + 1)
        outsideScratch_.resize(treeHeight_ + 1);
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

Poly PolyProductTree::node(int index, const std::vector<Poly> &factors) const {
    // 叶子借用输入系数；填充叶子是单位元；非叶子读取已构造的乘积节点。
    if (index >= leafBase_) {
        const int factorIndex = index - leafBase_;
        if (factorIndex >= factorCount_)
            return Poly(0, outsideScratch_[0].coefficients);
        return factors[factorIndex];
    }
    return Poly(productTree_[index].start, productTree_[index].coefficients);
}

void PolyProductTree::visitOutside(int index, int depth, const std::vector<Poly> &factors, const std::function<void(int, const Poly &)> &callback) {
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

}
