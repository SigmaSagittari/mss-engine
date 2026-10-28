# 算法引擎 API Prompt（mss0.16）

本文件只适用于 [code/algo/probability_engine/](../../code/algo/probability_engine/) 和算法模块接口；其他任务不要加载本文件。必读清单见 [AGENTS.md](../../AGENTS.md)，规则归属见 [docs/README.md](../README.md)。`mss1.5` 已废弃，不作为参考。

## 当前范围

以 `algo/probability_engine/` 下实际存在的目录和文件为准，本文件不维护模块清单。新增模块沿用下面的外层类型和接口形状，不要照搬 1.5 的文件树或 API。

## 模块形状

典型模块用一个外层类型（如 `ObservedBoard`）承载该模块的 API；无状态算法通过该类型的公开函数调用。只在实际需要时定义内层 `Result`、`Workspace`、`Pool` 和 `Delta`，由模块 API 通过领域类型交互，不再叠第二层 facade。

- `Result` 保存调用返回后还需要的数据；不能再保存一份已有对象已拥有的相同状态。
- `Pool` 在确需 intern/共享存储、稳定 ID 或引用跨调用复用时才存在。`Result` 中的 ID、引用、指针/长度要说明依赖哪个 `Pool`，以及 `Pool` 写入/清空后的失效规则。
- `Workspace` 明确由调用方持有并通过引用传入，复用临时内存；说明串行复用、并发/重入限制。临时缓存不要藏在全局变量或不透明 wrapper 里。
- `Delta` 仅用于算法天然支持的增量更新和正确逆序撤回。若正确回滚很难、当前实现不具备增量语义，直接省略；不为“长得像 probability”伪造 `Delta`。
- API 直接传实际状态、结果、池和必要工作区；不要再包一层重复的 Board/State/Result DTO，也不要复制游戏规则或把路由、HTTP 字符串塞进引擎。
- 上层（Game/UI/调度层）负责策略选择和输入解析；`probability_engine` 只提供概率、结构、枚举等基础能力，不依赖 UI、HTTP 或主聚合配置。
- `std::optional`/`std::span` 按 [implementation.md](implementation.md) 处理：默认不用；确有必要时先说明数据布局、生命周期和成本，取得用户同意后再用。
- 新普通算法模块沿用上述外层类型和同名 `.h/.cpp` 声明/实现边界。头文件里的非模板实现必须有模块现状或必要内联依据；不要用 `inline` 掩盖文件职责。

## Config 与 Workspace 的归属

归属、层级和 API 参数只按 [implementation.md](implementation.md)「Config 与 Workspace」执行；本目录不另立规则。

## 接口契约

- 头文件在声明附近写可执行的契约：输入来自同一状态、`CellId` 如何编码、引用/指针长度/池 ID 的有效期、`Delta` 的撤回顺序、`Workspace` 是否可复用或并发。
- `ObservedBoard` 当前用行优先 `CellId = int`，尺寸创建后固定；`Result` 构造时提供实际雷数和全 `Hidden` 盘面；`Delta` 记录单格前后状态，`Pool` 保存 Delta 记录，`Batch` 是池内切片。新增模块沿用这种“外层类型 + 内层 Result/Pool/Delta/Workspace”的形状，不要另起一套。
- 不要在 `probability_engine` 中解析 UI 文本、HTTP 字段或表单；这些属于上层边界。
- 算法模块不反向依赖主配置、主工作区或上层调用者。需要下层能力时，通过头文件依赖下层模块自己的类型。
