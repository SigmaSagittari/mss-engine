# 待办


- [ ] Search 在算力充足时扩大候选覆盖，再重新搜索此前遗漏候选的浅层路线。当前 `(6,3,1)` 固定宽度属于性能取舍；候选窗口随盘面最安全格变化，可能漏掉极端局面的好招，单纯加深不能补回。未来需协调广度与深度，充分利用追加算力。

- [ ] 实装 `LogicComponent`，按以下步骤逐步完成：
  - 放在 `code/algo/winrate_engine/general/logic_component.h/.cpp`；只提供 `analyze`，每次全量重算，省略 `update/Delta`；Workspace 和已有 Shape/Distribution 池继续复用。
  - 分块沿用 H/T 八邻接闭包和 Structure 实例整块闭包，记录各块的 Structure 实例 ID 与非前沿 T 格。
  - Result 按“全盘结果 → 逻辑块 → 雷数档”组织；每档保存 `mines`、`long double chance`、局部布局数 `ways` 及暴力求解结果或猜测评分。复用 `Probability::analyzeLogicComponents` 取得全盘约束下的档概率和局部布局数。
  - 提取每块的独立分析盘面，保留候选格、相关数字、周围已知雷及相对坐标，建立局部 CellId 到原盘 CellId 的映射；保留已知雷数为 f 时，总雷数设为 k+f。背景使用 ForcedSafe 的方案须验证约束等价性，必安检测和评分仅面向本块真实候选格。
  - 固定 k 后重建 Basic、Structure、Probability 和模板 LTR；先识别该档的条件必安格与完成态，再对需要猜测的档按局部 `ways` 决定是否用暴力求解，其他猜测评分待定。明确各档结果如何保存必安格及完成态。
  - 核验单档、多档、纯 T 块、固定 k 后出现必安、剩余格全雷及 CellId 映射；保留各档原始结果，跨档汇总与总推荐另行研究，不直接照搬 1.4 按已知 k 求解后加权的 Recommendation。

部分条目沿用 1.5 时期的草案思路；旧的全局 Config 传参方式、测试拆成 cpp、首步处理及 Game API 草图已被后续约定替代，以当前源码、用户明确约定及 [共享编码与审查 Prompt](docs/prompts/CODE_REVIEW_PROMPT.md) 为准，不照搬过时草图。