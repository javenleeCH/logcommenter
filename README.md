# logcommenter

基于 **Clang / LibTooling** 的 C++ 源码日志工具：既能**一键注释**带标记（`pengtest`）的日志语句，也能对**选中变量注入类型感知的观测日志**。CLI 输出 **1-based JSON TextEdit**，天然对接 VSCode 插件做源码改写。

![CI](https://github.com/javenleeCH/logcommenter/workflows/CI/badge.svg)
[![Release](https://img.shields.io/github/v/release/javenleeCH/logcommenter)](https://github.com/javenleeCH/logcommenter/releases)

## 它解决什么问题

开发时经常临时加日志，上线前要批量清掉；排查时又想在某个变量/某一行就地加观测日志、看完再撤。logcommenter 把这两件事做成可逆的、语义感知的源码改写：

1. **`--comment`**：把源码中带 `pengtest` 标记的日志语句（函数调用 + `cout/cerr/clog` 流）自动注释掉——是注释不是删除，可逆。
2. **`--inject`**：对选中变量注入一行观测日志，自动生成类型感知的打印表达式。

关键设计：**CLI 只做纯分析、输出 JSON TextEdit，不回写文件**。要不要落地由调用方（VSCode 插件薄壳 / 脚本）用 `applyEdit` 决定——分析工具与编辑器解耦。

## 最新 CLI

```
用法:
  logcommenter --inject --file <path> --var <name> [--position line:col]
      --compile-commands <compile_commands.json>
  logcommenter --inject --file <path> --position <line:col>
      --compile-commands <compile_commands.json>
  logcommenter --comment --file <path>
      --compile-commands <compile_commands.json>

选项:
  --inject / --comment        动作：注入观测日志 / 注释标记(pengtest)日志
  --file PATH                 待处理源文件
  --var NAME                  注入：按变量名定位观测对象
  --position LINE:COL         注入：按光标位置定位（1-based，命中声明处或引用处）
  --compile-commands PATH     compile_commands.json 路径
  --help / -h                 帮助
```

`--inject` 用 **`--var`（按名字）** 或 **`--position`（按光标位置）** 二选一定位。输出示例：

```json
{ "file": "src/sample.cpp",
  "edits": [ { "range": { "start": {"line":10,"col":18}, "end": {"line":10,"col":18} },
               "text": "\n    std::cout << \"[obs] total=\" << total << std::endl;" } ] }
```

TextEdit 语义：`start == end` 为纯插入，文本插到 `(line,col)` 字符**之前**（1-based，`col=行长+1` 即行尾）。注入点为声明语句（含 `;`）之后，应用后即是合法 C++。

### 观测能力

- **基本类型**（`int/float/double/bool/char*/std::string/指针` 等）：直接 `<<` 打印值。
- **对象 / 复杂类型**（不可直接 `<<`）：自动兜底打印地址 `(const void*)&var`，保证有观测输出。
- **`--position` 命中声明处或引用处**：光标落在变量 token 上即可，不必输入名字——VSCode 右键"加观测日志"的核心场景。
- **插入位置与幂等**：观测语句插在声明语句（含 `;`）之后；`for/if/switch` 头部声明的观测插到循环/分支体开头；同一变量重复注入自动跳过，不叠加。
- **范围**：当前仅支持函数局部变量；全局/类成员观测（friend 注入）与形参观测见演进方向。

## 架构

```
src/
├── logengine/              # 纯逻辑层，零 Clang 依赖，本机可编译单测
│   ├── format_table.*      # QualType → 观测表达式（流式 / 地址兜底）
│   ├── text_edit.*         # 1-based TextEdit 结构 + JSON 序列化
│   ├── compile_index.*     # 偏移索引 + 持久化缓存（.logc-idx）
│   └── args.*              # CLI 参数解析（--var / --position）
├── clang/
│   └── engine_clang.*      # LibTooling AST 适配层：注释/注入 matcher
├── main.cpp                # CLI 入口
└── sample.cpp              # E2E 样例（含 pengtest 日志 + 对象/基本类型变量）
```

### 1GB compile_commands 秒级定位

大型 monorepo 的 `compile_commands.json` 可达 1GB。logcommenter **不整载**它：

- 构建时**单次流式扫描**，生成 `file → (offset, length)` 索引，持久化为 `.logc-idx`（毫秒级）；
- 查询时按索引 `seek + read` **单条**编译参数（O(1)），再交给 Clang 解析该文件；
- `.logc-idx` 带 mtime 失效：`compile_commands.json` 未变则跳过重扫。

## 构建

依赖 **LLVM/Clang 23.1.3**。默认用 [CPM](https://github.com/cpm-cmake/CPM.cmake) 拉取 `llvm-project` 源码并现场静态编译（Linux / Windows 均可，CI 已全绿）。

```bash
# 纯逻辑 core（无 Clang 依赖，快，必绿）
cmake -S . -B build -DLOGCOMMENTER_BUILD_AST=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target logcommenter_tests -j$(nproc)
./build/logcommenter_tests

# 完整工具（AST 引擎 + CLI）
cmake -S . -B build -DLOGCOMMENTER_BUILD_AST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target logcommenter -j$(nproc)
./build/logcommenter --inject --file src/sample.cpp --var total \
  --compile-commands build/compile_commands.json

# 代理 / 自签证书环境下拉不动 GitHub 时，可单独关掉对应下载的 TLS 校验：
cmake -S . -B build -DLOGC_DISABLE_SSL_CPM=ON -DLOGC_DISABLE_SSL_LLVM=ON

# 指定编译器（Windows 上同时装了 cl.exe 和 clang-cl.exe 时很有用）：
cmake -S . -B build -DLOGC_COMPILER=msvc        # MSVC cl.exe（需在 vcvarsall/msvc-dev-cmd 环境）
cmake -S . -B build -DLOGC_COMPILER=clang-cl    # LLVM clang-cl.exe（同样需 MSVC 头文件环境）
cmake -S . -B build -DLOGC_COMPILER=clang       # Linux/macOS 的 clang / clang++
```

构建选项：

| 选项 | 说明 |
|---|---|
| `LOGCOMMENTER_BUILD_AST` | ON=构建 AST 引擎+CLI（需 Clang）；OFF=仅纯逻辑 core |
| `LOGC_COMPILER` | 指定编译器：`auto`(默认)/`msvc`(cl.exe)/`clang-cl`(clang-cl.exe)/`clang`/`gcc`；msvc、clang-cl 需在 MSVC 开发环境中运行 |
| `LOGC_DISABLE_SSL_CPM` | ON=下载 CPM.cmake 时禁用 TLS 校验（代理/自签证书环境） |
| `LOGC_DISABLE_SSL_LLVM` | ON=下载 llvm-project 时禁用 TLS 校验 |
| `CMAKE_CXX_COMPILER_LAUNCHER=ccache` | 启用 ccache 编译缓存 |

> 首次构建会完整编译 LLVM+Clang，耗时较长（CI 用 ccache + LLVM 源码缓存加速后续）。

## 测试 / CI

- **单测**（`logcommenter_tests`）：23 cases / 74 checks，覆盖格式表、TextEdit、偏移索引、CLI 参数。
- **GitHub Actions**（`.github/workflows/ci.yml`）：
  - `core` job：纯逻辑，零 Clang 依赖，快、必绿；
  - `ast` job：CPM 现场构建 LLVM 23 + ccache + E2E smoke——覆盖 comment / inject(var) / inject(position) / 注释/注入各自的多行与幂等语义，并包含"应用 TextEdit 后源码仍可解析 + 重复注入幂等"的落盘校验（`tools/e2e_apply_check.py`）。

## Release

发布流程见 `.github/workflows/release.yml`：**Linux x64 / Windows x64** 现场编译 LLVM 静态链接，产出带平台前缀的 zip 并附加到 release tag。`--inject --position` 等最新能力以 release 二进制为准。

> macOS 暂不支持（现场编译 LLVM 冷编耗时过长），后续需要时可在 release.yml 补一行 matrix 加回。

## 演进方向

- **VSCode 插件薄壳**：右键"加观测日志" → `child_process` 调 CLI → `applyEdit`（CLI 的 JSON TextEdit 输出即为对接协议）。
- **私有成员观测**：已拍板 **friend 注入**方案（声明进头文件、定义放 .cpp 非 inline，不改类布局/ABI）。
- **对象成员级观测**：从"打印地址"升级为调用 `operator<<` / `toString()` 的语义打印。
