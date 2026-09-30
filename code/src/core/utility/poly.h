#pragma once

#include <functional>
#include <vector>

namespace mss {

// Poly 借用连续系数数组；coefficients[i] 对应 start+i 次，空数组表示零多项式。
struct Poly {
    int start;
    const std::vector<long double> &coefficients;

    Poly(int start, const std::vector<long double> &coefficients) : start(start), coefficients(coefficients) {}

    // 将两个多项式卷积到调用方缓冲区；输出 vector 不得与任一输入系数 vector 别名，返回值借用输出。
    static Poly multiply(const Poly &left, const Poly &right, std::vector<long double> &coefficients);
};

// 可复用的多项式乘积树；空因子集合的乘积为 1，逐项排除结果通过回调交付。
class PolyProductTree {
  public:
    // 输入系数 vector 至少活到调用完成，调用期间不得修改，且不能引用本对象上轮结果；单因子时总乘积继续借用输入。
    // 非空输入要求有效回调；每个因子回调一次，排除乘积只在回调期间有效。返回的总乘积视图下次调用后失效。
    // 同一对象不得并发或重入调用。
    Poly forEachOutside(const std::vector<Poly> &factors, const std::function<void(int, const Poly &)> &callback);

  private:
    struct Node {
        int start = 0;
        std::vector<long double> coefficients;
    };

    Poly node(int index, const std::vector<Poly> &factors) const;
    void visitOutside(int index, int depth, const std::vector<Poly> &factors, const std::function<void(int, const Poly &)> &callback);

    int factorCount_ = 0;
    int leafBase_ = 0;
    int treeHeight_ = 0;
    std::vector<Node> productTree_;
    std::vector<Node> outsideScratch_;
};

}
