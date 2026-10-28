# 仓库 Agent 入口

本仓库当前只维护 [code](code/)。mss1.5（Git 标签 `v1.5-archive`）已废弃：不要新增功能、不要重构、不要把它当作规范或实现参考；只有用户明确要求处理 1.5 的历史问题时才动它。[TODO.md](TODO.md) 和旧注释里的 1.5 草案、快照、待办不是 0.16 的授权或规范。

本项目是 C 风格的 C++ 高性能计算库：精简、直接、性能优先。不为使用 C++ 特性增加抽象、包装、胶水层或重复状态；具体禁用项和例外流程见 [implementation.md](docs/prompts/implementation.md)。

## 优先级

1. 用户当前明确要求。
2. 本文件的作用域和授权规则。
3. [CODE_REVIEW_PROMPT.md](docs/prompts/CODE_REVIEW_PROMPT.md)。
4. 任务对应的子文档（implementation / test / probability_engine / operations）。
5. 现有代码和相邻实现：只作为风格与 API 证据，不能覆盖明确禁令。
6. 架构草案、TODO、旧注释、旧批准：不是当前授权。

文档之间冲突时，指出冲突并问用户；不要自行调和成第三套规则。每条规则只写在它的归属文档里，其他文档只引用、不复制；引用与归属文档不一致时，以归属文档为准，并把引用改回去。归属见 [docs/README.md](docs/README.md)。

## 任务开始时读取

- 写代码或审查代码前，完整阅读 [CODE_REVIEW_PROMPT.md](docs/prompts/CODE_REVIEW_PROMPT.md)。
- 修改实现代码：再完整阅读 [implementation.md](docs/prompts/implementation.md)。
- 新增或修改测试：再完整阅读 [implementation.md](docs/prompts/implementation.md) 和 [test.md](docs/prompts/test.md)。
- 修改 `probability_engine`：再完整阅读 [probability_engine.md](docs/prompts/probability_engine.md)。
- 运行程序、测试、benchmark、下载工具或需要用户手动操作：再完整阅读 [operations.md](docs/prompts/operations.md)。
- 不要加载与当前任务无关的子文档，也不要把整片无关源码灌入上下文。

## 授权与范围

- 只改完成任务所需的最小文件集；不顺手清理、重排、格式化或重构无关代码。保留用户已有修改。
- 任务所需的临时文件可以自由新增、修改，无需另行确认；临时测试的入口、目录和构建方式可使用现有工具自行确定。临时测试必须在专用临时目录中运行，并显式将进程工作目录设为该目录；临时驱动、可执行文件、CSV、日志等产物均留在该目录，禁止写入源码目录，结束后自行删除本次生成的临时文件。
- `main` 上的跨文件修改，先列出文件和原因并取得用户同意；非 `main` 分支按当前任务需要修改，不需要事前审核。
- `Config`/`Workspace` 修改：`main` 上先取得用户授权；非 `main` 分支不需要。
- `probability_engine` 修改：任何分支都先取得用户授权；用户当前任务明确点名即视为授权，同一任务内不重复确认。
- 提交：非 `main` 分支的本地提交不需要额外审核；`main` 上的提交、合并主线、上传远端、创建 tag 或改写 `main` 历史，都必须用户明确同意。
- 纯审查任务只读：不改文件、不运行程序或测试；需要运行时先问用户。
- 架构草案、TODO、旧任务的批准或自己对用户意图的猜测，都不是当前实现授权。用户明确要求架构方案时，方案要落到真实文件和 API，并说明它只是方案；实现仍要另行授权。

本文件和子文档里的条目都是硬约束，没有"建议"级别。

[main.cpp](code/main.cpp) 是程序入口，不是规范入口；需要核实时再读它。
