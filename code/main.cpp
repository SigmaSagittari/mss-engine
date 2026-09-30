/*
mss1.5 风格约定

1. 禁用 static_cast。确需显式转换时使用 C 风格的 (typename) XXX；允许正常隐式转换，不为隐式转换额外添加显式转换。仅在比较中允许将 size_t 转为 int，以规避 MinGW -Wall 的符号比较警告。
2. for 循环使用显式类型，常见写法：for (int i = 0; i < (int)XX.size(); ++i) 和 for (int i = (int)XX.size() - 1; i >= 0; --i)。
3. 禁用 auto，仅允许 auto [x, y] = XXX 和 auto function = lambda 两种写法。
4. 单行代码保持单行，不随意换行。
5. 禁用匿名 namespace 和文件内 static 局部变量；不把单个源文件视为特殊单元。
6. 风格审查只检查本次 git diff，不检查未改动代码。
7. 严格贴合上下文风格；默认单文件修改。确需跨文件修改时，先暂停并请求明确批准。
8. 遵守 anti-童子军规则，只处理本次相关问题，不顺手整理无关代码；风格修改尽量删改，除非确有功能新增，净增行数最好为负。
9. 信任已测组件，严禁防御性编程。异常结果视为 Bug；不可达路径直接使用 assert_(false, msg)，不提供回退值；热路径禁用 assert_。
10. 性能问题也视为 Bug。新增任何胶水代码前，先向用户汇报并取得审批。
11. 若怀疑某处有 Bug，仔细研究后确认不是 Bug，应在该处添加注释；若理解函数功能必须先检查其实现，也应添加注释。此类注释不受第 8 条的净增行数要求限制。
12. 注释必须使用中文。
13. 禁用 C++ 的 switch 分支语句，统一使用 if/else。
*/

#include "test/harness.h"
#include <fstream>

#include "ui/ui.h" 

int main() {
    //test::harness();
    // 每次启动都清空本次运行日志；累计日志保留跨运行记录。
    {
        std::ofstream currentRunLog("mss_current_run.log", std::ios::trunc);
    }
    mss::UI::run();
    return 0;
}
