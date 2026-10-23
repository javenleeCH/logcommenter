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

依赖 **LLVM/Clang 23.1.3**。预编译的静态 LLVM/Clang 工具链作为 release asset 提供（见 `toolchain.yml`），`tools/fetch_llvm.sh` 下载后用现代 CMake `find_package` 消费，**不再每次从源码编译 LLVM**。最简单的方式是直接跑构建脚本；也可手动按 preset 配置。

```bash
# Linux/macOS：一键脚本（自动 fetch 预编译 LLVM + build + test）
./build.sh            # Release（默认）
./build.sh debug      # Debug：下载带 -g 的 Debug LLVM，可单步进入 LLVM/Clang 库内部

# Windows：build.bat（windows-msvc，需在 vcvarsall/msvc-dev-cmd 环境，含 Git bash）
build.bat             # Release
build.bat debug       # Debug

# 手动步骤（以 Linux、release 为例）：
bash tools/fetch_llvm.sh                 # 下载并解压到 .toolchain
cmake --preset unix-default \
  -DLLVM_DIR="$PWD/.toolchain/lib/cmake/llvm" \
  -DClang_DIR="$PWD/.toolchain/lib/cmake/clang"
cmake --build build/unix-default --target logcommenter -j$(nproc)

# 手动下载 Debug 版 LLVM（到 .toolchain-debug，含 -g 符号 + LLVM 源码树）
bash tools/fetch_llvm.sh "" "" debug
# 或 LOGC_LLVM_BUILD_TYPE=debug bash tools/fetch_llvm.sh
# debug asset 内含 LLVM 源码树（.toolchain-debug/llvm-src），供调试器源码映射：
#   gdb :  set substitute-path <编译机源码根> /path/to/.toolchain-debug/llvm-src
#   lldb: settings set target.source-map <编译机源码根> /path/to/.toolchain-debug/llvm-src

# 本机已安装 LLVM 时，直接 export LLVM_DIR/Clang_DIR 即可，无需下载
# 纯逻辑 core（无 Clang 依赖，快，必绿）：跳过 LLVM，只构建单测
cmake --preset unix-default -DLOGCOMMENTER_BUILD_AST=OFF
cmake --build build/unix-default --target logcommenter_tests -j$(nproc)
./build/unix-default/logcommenter_tests

# 代理 / 自签证书环境下拉不动 release asset 时，让 fetch 脚本跳过 TLS 校验：
LOGC_DISABLE_SSL=1 bash tools/fetch_llvm.sh
```

构建选项：

| 选项 | 说明 |
|---|---|
| `LOGCOMMENTER_BUILD_AST` | ON=构建 AST 引擎+CLI（需 LLVM/Clang）；OFF=仅纯逻辑 core |
| `LLVM_DIR` / `Clang_DIR` | 指向预编译/本机 LLVM 的 CMake config（`lib/cmake/llvm`、`lib/cmake/clang`） |
| `LOGC_DISABLE_SSL`（环境变量） | =1 时 fetch 脚本下载 asset 跳过 TLS 校验（代理/自签证书环境） |
| `LOGC_LLVM_BUILD_TYPE`（环境变量） | `debug` 时 fetch 脚本下载 Debug 版 LLVM 到 `.toolchain-debug`（带 -g 符号 + 源码树） |

> 预编译工具链由 `toolchain.yml` 手动构建（`build_type` 选 `release`/`debug`，各构建一次），
> 上传到 `toolchain-llvm-<ver>` release asset，CI / release 只消费 **release** 版（门禁稳定）；
> **debug** 版仅供本地步进调试 LLVM 内部，不会被门禁使用。

## 测试 / CI

- **单测**（`logcommenter_tests`）：23 cases / 74 checks，覆盖格式表、TextEdit、偏移索引、CLI 参数。
- **GitHub Actions**（`.github/workflows/ci.yml`）：
  - `core` job：纯逻辑，零 Clang 依赖，三平台（Ubuntu/Windows/macOS）快、必绿；
  - `ast` job：下载预编译 LLVM 静态工具链（release）+ `find_package` 构建 + E2E smoke——覆盖 comment / inject(var) / inject(position) / 注释/注入各自的多行与幂等语义，并包含"应用 TextEdit 后源码仍可解析 + 重复注入幂等"的落盘校验（`tools/e2e_apply_check.py`）。
- **工具链构建**（`.github/workflows/toolchain.yml`，手动触发，`build_type` 可选 release/debug）：把静态 LLVM/Clang 构建安装并打包，上传到 `toolchain-llvm-<ver>` release asset。

## Release

发布流程见 `.github/workflows/release.yml`：**Linux x64 / Windows x64** 下载预编译 LLVM（release）静态工具链、构建 logcommenter，产出带平台前缀的 zip 并附加到 release tag。`--inject --position` 等最新能力以 release 二进制为准。

> macOS 暂不支持（现场编译 LLVM 冷编耗时过长），后续需要时可在 release.yml 补一行 matrix 加回。

## 演进方向

- **VSCode 插件薄壳**：右键"加观测日志" → `child_process` 调 CLI → `applyEdit`（CLI 的 JSON TextEdit 输出即为对接协议）。
- **私有成员观测**：已拍板 **friend 注入**方案（声明进头文件、定义放 .cpp 非 inline，不改类布局/ABI）。
- **对象成员级观测**：从"打印地址"升级为调用 `operator<<` / `toString()` 的语义打印。
