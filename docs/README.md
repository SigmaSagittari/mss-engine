# 文档索引

仓库入口是 `AGENTS.md`：范围、优先级、必读清单和授权都在那里，本页不重复。

本页只说明文档结构，也就是每条规则归谁管。

| 内容 | 归属文档 |
| --- | --- |
| 范围、优先级、必读清单、授权、分支与提交 | `AGENTS.md`（`../AGENTS.md`） |
| 工作方法、审查、可靠性判断、回复格式 | `CODE_REVIEW_PROMPT.md`（`prompts/CODE_REVIEW_PROMPT.md`） |
| C++ 写法、结构体初始化、算法模块五类结构、嵌套分类、文件组织与接口形式 | `style.md`（`prompts/style.md`） |
| 实现中的可靠性与性能、领域与接口职责、UI / JS | `implementation.md`（`prompts/implementation.md`） |
| 测试 | `test.md`（`prompts/test.md`） |
| 运行、超时、benchmark、下载 | `operations.md`（`prompts/operations.md`） |
| 待办 | `TODO.md`（`../TODO.md`） |

## 写作约定

- 一条规则只写在它的归属文档里，其他文档只引用。引用与归属文档不一致时，以归属文档为准，并把引用改回去。
- 规则文档不写「当前已落地 / 尚未落地」的模块清单。模块状态以源码目录为准。
- 修改规则后，检查本页的归属表和其他文档里的引用是否仍然成立。
