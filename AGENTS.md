# AGENTS.md

面向 coding agent 的仓库工作须知：构建/测试命令、关键机制、历史踩坑。

## 构建与测试

- 完整构建（消费预编译 LLVM/Clang 23 静态工具链，不再从源码构建 LLVM）：
  Linux/macOS：`./build.sh`（release，默认；`./build.sh debug` 用 Debug 版）；
  Windows（MSVC，需 vcvarsall/msvc-dev-cmd 环境，含 Git bash）：`build.bat` / `build.bat debug`。
  预编译工具链下载到 `.toolchain/`（release）或 `.toolchain-debug/`（debug，带 -g 符号）；
  本机已装 LLVM 时直接 export `LLVM_DIR`/`Clang_DIR` 跳过下载。
- 预编译工具链由 `.github/workflows/toolchain.yml` **手动 dispatch** 构建（`build_type` 选
  `release`/`debug`，仅在 LLVM 版本/构建类型变化时跑一次），产物上传到 `toolchain-llvm-<ver>`
  release asset（release 无后缀，debug 带 `-debug` 后缀），CI/release 经 `tools/fetch_llvm.sh`
  只下载 **release** 版复用。
- Debug 版 LLVM 仅用于本地步进调试 LLVM 内部：`./build.sh debug` 会下载 `.toolchain-debug`
  并以 `unix-default-debug` preset（`-DCMAKE_BUILD_TYPE=Debug`）编译，可单步进 clang::tooling
  等库函数。门禁（CI/release）始终是 release 版 + Release 构建，不受影响。
- 纯逻辑单测（改 `src/logengine/` 后必跑，无 Clang 依赖）：
  `cmake --build build/unix-default --target logcommenter_tests && ./build/unix-default/logcommenter_tests`
- E2E 冒烟（与 CI `ast` job 的四场景一一对应，见 `.github/workflows/ci.yml`）：
  对 `src/sample.cpp` 跑 `--comment` / `--inject --var total` / `--inject --position 10:8` /
  `--inject --var cfg`，compile-commands 用 `build/unix-default/compile_commands.json`。
- 只改 `logengine`（纯逻辑）时用 `-DLOGCOMMENTER_BUILD_AST=OFF` 可跳过 LLVM。

## 产品与技术约束（源自 docs/PRODUCT_TECH_PLAN.md，改动不得违背）

- 分层：VSCode 插件薄壳（选区/右键/applyEdit）→ 短生命周期 CLI（起→解析→返回→退出）
  → LogEngine。不 fork clangd、不常驻进程、不与 clangd 抢内存。
- CLI 只输出 1-based JSON TextEdit、**不回写文件**；落地与否由调用方 applyEdit 决定。
- compile_commands 用偏移索引（`.logc-idx`）seek+read 单条取参，绝不整载 1GB；
  索引按 mtime 失效。
- 幂等（产品要求）：`--comment` 天然跳过已注释语句；`--inject` 不得重复注入——
  单次调用内已按 canonical decl 去重；跨调用检测已有 `[obs]` 行**尚未实现**，属产品待办。
- 对象观测优先级：有 `operator<<` 直接流式；用户可改类走 friend 注入（需用户显式确认，
  改头文件有副作用）；第三方/系统类型回退地址打印。
- 方案文档中 "LLVM 21.1.4" 已过时：仓库现用 LLVM/Clang 23.1.3（预编译静态工具链，find_package 消费）。

## clang resource-dir（核心坑，曾造成连续十余次 CI 失败）

- `-resource-dir` 必须指向**直接包含 `include/` 的版本目录**（标准布局 `lib/clang/<ver>`），
  不能只给 exe 所在目录；clang 只从 `<resource-dir>/include` 找 builtin 头（stddef.h 等），
  `-isystem` 对它们无效。
- 版本目录命名有两套：打包/预编译工具链用完整版本（`23.1.3`）；LLVM 构建树用仅 major（`23`，
  由 `get_clang_resource_dir` 决定）。`findResourceDir`（`src/clang/engine_clang.cpp`）按
  `<exeDir>/lib/clang/<ver>` → 编译期 bake 的 `LOGC_LLVM_BIN_DIR`（默认 "."）→ `<exeDir>` 兜底探测，
  优先精确匹配 `CLANG_VERSION_STRING`，否则接受任何含 `include/stddef.h` 的版本目录。
- 预编译工具链的内置头已随 `cmake --install` 安装到 `.toolchain/lib/clang/<ver>/include`；
  CI/打包时从该目录复制版本目录到 exe 旁即可（不再依赖 `clang-resource-headers` 构建 target）。
- Windows 本机（MSVC 工具链）自带 `stddef.h`，会掩盖 resource-dir 缺失；Linux CI 才是真实检验。
  本地验证打包布局时，把头按 `package/lib/clang/<ver>/include` 放到 exe 旁再跑。

## CI / release 缓存策略（勿破坏）

- LLVM 不再在 CI 现场编译：`toolchain.yml` 预编译成 release asset（release + debug 两版），
  ci/release 经 `fetch_llvm.sh` 下载 **release** 版复用。没有 LLVM 源码构建，也就**不需要
  ccache / LLVM 源码缓存**（项目自身源码很小，编译很快）。
- `fetch_llvm.sh` 按平台+构建类型选 asset（`llvm-clang-<plat>-x86_64-<ver>[-debug].zip`），
  已存在则跳过；代理/自签证书环境用 `LOGC_DISABLE_SSL=1`（curl -k）。
- Debug 版 asset 体积较大（带 -g 符号，可能 >1GB/平台），只在本地需要步进调试时下载；
  CI/release 一律用 release 版，勿引入 debug 到门禁。
- ci.yml E2E 冒烟的行号（如 `--position 10:8`）与 `src/sample.cpp` 强耦合：
  改 sample.cpp 布局必须同步改 ci.yml 里的行号与注释。
- release 打包从预编译工具链 `.toolchain/lib/clang/*/include` 复制资源头（含 tablegen 产物，完整）。

## 调试

- `LOGCOMMENTER_DEBUG=1`：打印 resource-dir 解析结果与内置头命中，并让 clang 打印 include
  搜索路径（`-v`）。
- 想单步进入 LLVM/Clang 库内部：`./build.sh debug`（或手动下载 `.toolchain-debug` 并用
  `unix-default-debug` / `windows-msvc-debug` preset 以 Debug 编译）。普通断点只在自己代码里时，
  release 版即可。
- compile_commands 索引缓存 `<db>.logc-idx` 按 mtime 失效；改了 compile_commands 后行为异常先删它。
- `--inject --position` 模式同时挂 `varDecl` 与 `declRefExpr` 两个 matcher：命中哪个就按
  哪个自身的源码范围过滤位置，再按 canonical decl 去重。顺序不可反——先去重会让
  "位置在引用处"的命中被声明命中提前占用 seen_ 而整体落空。
- 注入插入点 = 声明语句（DeclStmt，含 `;`）之后；for/if/switch/while 头部声明插到
  复合语句体开头，体无大括号则放弃。TextEdit 语义：纯插入在 (line,col) 字符之前
  （1-based，col=行长+1 为行尾）。**改插入逻辑后必须跑 `tools/e2e_apply_check.py`**
  （应用 edit 后源码仍可解析 + 幂等）——CI 只 grep JSON 文本时，插入点落在 token
  中间的缺陷曾漏网（"42" 被劈成两半）。
- 多行 `--comment`：流式日志逐层上溯取整条 operator<< 链，语句覆盖的每一行行首
  都插 `// `；局限：跨行字符串字面量内部也会被插注释（罕见，接受）。
