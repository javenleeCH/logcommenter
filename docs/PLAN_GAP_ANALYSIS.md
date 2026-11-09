# logcommenter 规划 vs 现状差距分析

> 分析对象：飞书文档「logcommenter 产品技术规划文档」（https://my.feishu.cn/docx/BQNIdO5uToEvLJxrPj6cu9rFnNg，revision 4）与仓库内 `docs/PRODUCT_TECH_PLAN.md`，对照 `src/logengine`、`src/clang`、`src/main.cpp`、`tests`、`vscode/`、`.github/workflows`、`tools/`、`AGENTS.md`、`README.md`。
> 性质：只读核对，未修改代码、未执行构建。以下结论均基于已读取的文件内容与 git 历史。

## 结论摘要

两份文档主体同版，但飞书版在 LLVM 版本号上过时（仍写 21.1.4），本地 md 已在提交 `ec44142` 改为 23.1.3；飞书版另有 3 个 html5 可视化块，本地 md 以 mermaid/表格呈现，无对应内容。

实现层面：CLI/插件骨架、偏移索引、TextEdit 协议、声明点插入、`--comment`、同次调用去重、跨文件 friend TextEdit 等主干能力已落地并有测试。主要差距集中在：friend 注入的落地形态与已拍板方案不一致且未真正观测成员；容器等复杂类型仍走地址兜底；插件层“整行注入”不可达、选区不精确（同名变量会被全文件注入）；编译参数缺失时无降级；索引构建把整份 compile_commands 读入内存，与“流式扫描、内存可忽略”的规划冲突，且 1–2s 性能目标未达成/未验证。

文档层面存在多处需修正的事实漂移：飞书 LLVM 版本、AGENTS 的“跨调用幂等未实现”（实际已实现）、README 的测试统计/支持范围/演进方向、规划 JSON 示例的 0-based 列基准与 CLI 参数名。

---

## 一、两份文档关系判断

**结论：主体是同一版规划，但不是严格同一 revision，双方各有“新”处。**

- 正文结构、章节、表格内容、里程碑、风险项与 `docs/PRODUCT_TECH_PLAN.md` 完全一致；飞书端 revision 4。
- **飞书版版本号过时**：非功能需求写 “LLVM 21.1.4 / Clang”，本地 md 已在最近一次提交（`ec44142`）改为 “LLVM 23.1.3 / Clang”，飞书未同步这次修订。
- **飞书版多出 3 个 html5 可视化块**（整体架构、编译参数对比、决策取舍+路线图），本地 md 对应内容是 mermaid 流程图和静态表格，无这些可视化。
- 飞书版无 md 之外的新增文字内容；其余表述与 md 一致。

因此：本地 md 在版本事实上更新，飞书在可视化呈现上更丰富，不能视为同一 revision。

---

## 二、产品能力差距（按重要性排序）

### P1. friend 注入与已拍板方案不一致，且并未真正观测 private 成员（高）

- **规划原意**：§4.1 “在类的真实定义（头文件）处注入 friend **声明**，函数定义放 .cpp 且**非 inline**”；目的是观测 private 成员。
- **当前状态**：不一致 + 部分实现。实现是在类内注入 **inline friend 定义**，函数体只打印 `"Type@地址"`，没有读取任何成员值。
- **证据**：`src/clang/engine_clang.cpp:189-196`（friend 文本生成）、`src/clang/engine_clang.cpp:297-311`（注入判定与落地）。
- **影响**：①与方案文档的 ABI 论证前提（声明进头文件、定义非 inline）不符，README/AGENTS 仍在重复旧拍板说法；②friend 分支观测效果等同地址兜底，未兑现“观测 private 成员”的核心价值；③代码未区分“用户可改类”与“第三方/系统头”，仅排除模板类和已有 `operator<<`，理论上可向第三方头注入。

### P2. 容器/复杂类型未达“类型感知”承诺（高）

- **规划原意**：§3.4 “int 直接打印，string 打印内容，指针 / **容器**按格式表扩展”。
- **当前状态**：部分实现。格式表只覆盖基础类型、`std::string`、`char*`、普通指针；`std::vector` 等容器无 `operator<<`，直接落到 `(ptr)` 地址兜底。
- **证据**：`src/logengine/format_table.cpp:59-123`（可选流式类型）；`src/clang/engine_clang.cpp:286-323`（`operator<<` 实查与地址兜底）。
- **影响**：F1 的“类型感知”退化为“基础类型 + 地址”，容器内容不可观测，与产品定位有差距。

### P3. “选中整行注入”在插件层不可达（高）

- **规划原意**：F1 “选中变量或整行”；§3.4 “选中整行插入该行前”。
- **当前状态**：CLI 有兜底、插件未接通。整行兜底仅在 `--var` 为空且无其他 edit 时触发；但插件始终传 `--var`。选中整行时 `name` 是整行文本，`varDecl(hasName(...))` 匹配为空，兜底不触发，最终报 “no matching target found”。
- **证据**：`src/clang/engine_clang.cpp:534-555`（行标记兜底）；`vscode/src/extension.ts:199-210`（插件始终传 `--var`）。
- **影响**：F1 的整行场景在 VSCode 中不可用，用户只能在标识符上右键。

### P4. 选区定位不精确，可能全文件同名变量误注入（高）

- **规划原意**：§3.4 “选区映射到 AST 节点”。
- **当前状态**：不一致。CLI 在 `--var` 非空时只挂 `hasName` matcher，`--position` 仅在 `else if` 分支生效；插件却同时传 `--var` 与 `--position`。若同一文件多个函数各有同名变量（如多个 `i`），会全部注入。
- **证据**：`src/clang/engine_clang.cpp:458-466`（matcher 分支）；`vscode/src/extension.ts:202-208`（同时传参与位置）。
- **影响**：右键一次可能产生多处 edit，“选区”语义退化为“按名字全文件匹配”。

### P5. 编译参数缺失/过期时无降级（中高）

- **规划原意**：§6.1 “降级为仅插入模板日志并提示，不中断操作”。
- **当前状态**：未实现。查不到 compile_commands 条目直接报错并退出 1。
- **证据**：`src/clang/engine_clang.cpp:906-910`（条目缺失报错）；`src/main.cpp:74-79`（错误返回）。
- **影响**：新文件、未进 compile_commands 的临时文件无法使用，与“不中断操作”的容错目标相反。

### P6. 1GB 索引构建“整载”文件，与规划的流式扫描冲突；性能目标未达成（高）

- **规划原意**：§3.2 “一次性**流式扫描** 1GB（只记录偏移，内存可忽略）”；§2.2 右键 ≤1–2 秒、不扫描 1GB、不常驻额外内存。
- **当前状态**：与规划不一致。`build()` 先把整份 `compile_commands.json` 读进 `std::string` 再扫描；查询本身确实是 seek+read（这一点符合规划）。首次建索引会有文件级内存峰值。
- **证据**：`src/logengine/compile_index.cpp:270-288`（构建流程）；`src/logengine/compile_index.cpp:172-188`（整文件读入）；`README.md:88`（实测重模板项目暖跑 2.7s、冷 8.3s）。
- **影响**：首次运行内存峰值与文件大小同量级，可能与 clangd 抢内存；规划自称“内存可忽略”不成立；1–2s NFR 未覆盖，仓库无性能/大文件基准门禁。

### P7. 超大文件解析无降级提示（中）

- **规划原意**：§6.1 “对超大文件降级提示”。
- **当前状态**：未实现。只有 PCH 加速路径，无文件大小/耗时阈值判断与提示。
- **证据**：`src/clang/engine_clang.cpp:911-931`（PCH 构建/装载路径）。
- **影响**：冷启动或 PCH 未命中时用户无预期，短进程可能长时间无响应直至插件超时（20s）。

### P8. `--comment` 是单文件处理，不是项目级批量（中）

- **规划原意**：F2 “按标记**批量**注释（沿用现有能力）”。
- **当前状态**：部分实现。CLI 强制 `--file`，一次只处理一个文件，无目录遍历。
- **证据**：`src/main.cpp:63-67`（文件必填）。
- **影响**：“上线前全仓清理”需逐文件或外部脚本驱动，与“批量”预期有落差。

### P9. 无显式回退机制（中）

- **规划原意**：M2 “applyEdit 增量合并；**回退机制**”。
- **当前状态**：未实现。插件只有错误提示/状态栏消息，没有撤销或回退命令，依赖 VSCode 原生 undo。
- **证据**：`vscode/src/extension.ts:185-247`（注入/注释命令与提示）。
- **影响**：friend 注入会改头文件，误操作后没有工具级的一键还原入口，风险偏高。

### P10. 地址兜底不带类型名（低）

- **规划原意**：§4.1 “回退为**地址 + 类型名**”。
- **当前状态**：不一致。回退文本只有 `(const void*)&var`，无类型名。
- **证据**：`src/clang/engine_clang.cpp:318-323`（兜底日志文本）。
- **影响**：多对象场景日志难以辨认类型；与文档描述不符。

### P11. 未决事项仍未决：标记字符串与观测模板不可配置（低中）

- **规划原意**：§6.2 “标记字符串策略（默认 pengtest）是否需要配置化”“观测日志的插入格式规范与可配置模板”。
- **当前状态**：未实现。`pengtest` 硬编码，观测语句模板硬编码，插件配置项中无对应开关。
- **证据**：`src/clang/engine_clang.cpp:126-128`（标记硬编码）；`src/clang/engine_clang.cpp:312-323`（日志模板硬编码）；`vscode/package.json:59-73`（配置项清单）。
- **影响**：换标记或改日志格式必须改代码/重编译。

### P12. CLI 协议示例与实现不一致（低）

- **规划原意**：§3.3 示例使用 `--compile-dir out/build/VS`，JSON 示例用 `col: 0`（0-based）。
- **当前状态**：实现是 `--compile-commands <path>`；列基准为 1-based（`col ≥ 1`，`col = 行长 + 1` 为行尾）。
- **证据**：`src/main.cpp:17-33`（用法说明）；`src/logengine/text_edit.h:8-17`（列基准语义）。
- **影响**：按文档示例接入会参数名不匹配、列基准错位。

### P13. `--write` 属规划外扩展（低）

- **规划原意**：§3.3 关键约束是“输出增量 TextEdit 而非整文件写盘”。
- **当前状态**：默认遵守；新增显式 `--write` 时直接改写文件（含跨文件 friend 头文件）。
- **证据**：`src/main.cpp:80-88`（写盘分支）。
- **影响**：默认路径安全；但方案文档未记录该扩展，读者会误以为不存在写盘能力。

> **已对齐项（供参考）**：偏移索引 seek+read 单条、mtime/size 失效、短生命周期 CLI、单文件 AST 解析、JSON TextEdit + 插件 applyEdit、声明后/for-if 体首插入、`--comment` 可逆、同次调用 canonical decl 去重、跨文件 friend TextEdit 协议，均有实现与测试。

---

## 三、工程质量 / 发布差距

### E1. 索引与大文件性能无测试门禁

单测只覆盖小样本索引与缓存往返，没有 1GB 级或耗时基准；CI 也无性能断言。P6 的“整载”问题因此长期不可见。
证据：`tests/test_compile_index.cpp:51-300`。

### E2. B 组 E2E 仅 Linux 跑，Windows 覆盖偏弱

friend 注入、模板类跳过、头文件噪声、PCH 等价性等 B 组只在 Ubuntu 执行；Windows 只跑 A 组（comment/inject var/position/对象兜底）。跨平台行为差异（尤其 MSVC 驱动参数清洗、resource-dir）缺少系统级验证。
证据：`.github/workflows/ci.yml:169-173`。

### E3. 插件仅协议层有单测

CI 只跑 `protocol.test.js`；`extension.ts` 的 CLI 路径解析、compile_commands 探测、applyEdits 字节列换算无单测。
证据：`.github/workflows/ci.yml:61-63`。

### E4. README 多处与现状脱节

- 测试统计仍写 “23 cases / 74 checks”，实际约有 74 个 `TEST_CASE`，checks 数也已变化：`README.md:142`。
- “范围：当前仅支持函数局部变量；全局/类成员观测（friend 注入）与形参观测见演进方向”与实现不符：代码已支持形参、position 模式下全局变量、`--friend`：`README.md:53`。
- “演进方向”仍把 VSCode 插件列为未来项，实际 `vscode/` 已实现并发布：`README.md:156-157`。

### E5. macOS 只覆盖纯逻辑，无 release

`core` job 含 macOS，但 release 矩阵只有 Linux/Windows。这与规划“Windows + Linux”不冲突，属已知限制，但用户侧容易误解为全平台可用。
证据：`.github/workflows/release.yml:21-33`；`README.md:152`。

### E6. 版本源漂移

插件包版本以 tag 为唯一来源，而 `vscode/package.json` 的 `0.3.0` 可能滞后于最新 release；AGENTS 已记录该坑，仍属发布期易误读点。
证据：`vscode/package.json:5`。

### E7. E2E 与 sample.cpp 行号强耦合

CI 的 `--position 10:8` / `14:12` 与 `src/sample.cpp` 布局绑定；改样例行会连带红 CI，脆弱但可接受。
证据：`src/sample.cpp:10-14`。

---

## 四、文档与代码 / AGENTS 的明确冲突

| 编号 | 冲突点 | 文档说法 | 代码现状 | 证据 |
|---|---|---|---|---|
| C1 | LLVM 版本 | 飞书 “21.1.4” | 本地 md/CMake/toolchain “23.1.3” | `docs/PRODUCT_TECH_PLAN.md`；提交 `ec44142` |
| C2 | friend 方案 | 规划 §4.1、README、AGENTS 称“声明进头文件、定义放 .cpp 非 inline” | 类内 inline friend 定义 | `src/clang/engine_clang.cpp:189-196` |
| C3 | 索引构建方式 | 规划 §3.2 “流式扫描、内存可忽略” | 整载文件后扫描 | `src/logengine/compile_index.cpp:270-288` |
| C4 | 幂等状态 | AGENTS 写“跨调用检测已有 `[obs]` 行尚未实现，属产品待办” | 已实现 `alreadyObserved()`，并有回环幂等 E2E | `src/clang/engine_clang.cpp:341-343`、`394-403`；`tools/e2e_apply_check.py:59-62`；`tools/e2e_bgroup.py:122-132` |
| C5 | JSON 列基准 | 规划示例 `col: 0` | 1-based（`col ≥ 1`） | `src/logengine/text_edit.h:8-17` |
| C6 | CLI 参数名 | 规划 `--compile-dir` | 实现 `--compile-commands` | `src/main.cpp:17-33` |
| C7 | 地址兜底 | 规划“地址 + 类型名” | 仅地址 | `src/clang/engine_clang.cpp:318-323` |
| C8 | 索引失效方式 | 规划“mtime / hash” | “mtime + size 双检” | `src/logengine/compile_index.cpp:459-466` |

补充：README 的测试统计/支持范围/演进方向过期（见 E4），亦属文档与代码冲突。

---

## 五、优先处理顺序

1. **修正文档事实**：飞书 LLVM 版本、AGENTS 幂等条目、README 测试统计/范围/演进方向。
2. **对齐 friend 方案与实现**：决定是改实现还是改文档，否则这是“已拍板方案未被遵守”的信任问题。
3. **评估索引构建整载 1GB 的内存风险（P6）**：与产品核心卖点“不扫描 1GB / 不与 clangd 抢内存”直接冲突的最大工程风险。
4. **明确“整行注入”和“选区精确性”的插件层行为（P3/P4）**：否则 F1 的验收标准在真实使用中不成立。
5. **补齐容错与降级（P5/P7）**，并为索引/性能补基准门禁（E1/E2）。
