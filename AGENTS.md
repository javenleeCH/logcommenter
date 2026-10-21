# AGENTS.md

面向 coding agent 的仓库工作须知：构建/测试命令、关键机制、历史踩坑。

## 构建与测试

- 完整构建（CPM 现场编译 LLVM/Clang 23，首次较久，之后增量秒级）：
  `cmake --preset ninja && cmake --build --preset ninja-debug`，产物在 `out/build/ninja/`。
  本机 CPM 源码缓存在 `D:/CodeSpace/cdir/cpm_cache/llvm_project/`。
- 纯逻辑单测（改 `src/logengine/` 后必跑，无 Clang 依赖）：
  `cmake --build out/build/ninja --target logcommenter_tests && ./out/build/ninja/logcommenter_tests`
- E2E 冒烟（与 CI `ast` job 的四场景一一对应，见 `.github/workflows/ci.yml`）：
  对 `src/sample.cpp` 跑 `--comment` / `--inject --var total` / `--inject --position 10:8` /
  `--inject --var cfg`，compile-commands 用 `out/build/ninja/compile_commands.json`。
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
- 方案文档中 "LLVM 21.1.4" 已过时：仓库现用 LLVM/Clang 23.1.3（CPM 现场编译）。

## clang resource-dir（核心坑，曾造成连续十余次 CI 失败）

- `-resource-dir` 必须指向**直接包含 `include/` 的版本目录**（标准布局 `lib/clang/<ver>`），
  不能只给 exe 所在目录；clang 只从 `<resource-dir>/include` 找 builtin 头（stddef.h 等），
  `-isystem` 对它们无效。
- 版本目录命名有两套：打包脚本/CI 复制用完整版本（`23.1.3`）；LLVM 构建树用仅 major（`23`，
  由 `get_clang_resource_dir` 决定）。`findResourceDir`（`src/clang/engine_clang.cpp`）按
  `<exeDir>/lib/clang/<ver>` → 编译期 bake 的构建树（`LOGC_LLVM_BIN_DIR`）→ `<exeDir>` 兜底探测，
  优先精确匹配 `CLANG_VERSION_STRING`，否则接受任何含 `include/stddef.h` 的版本目录。
- LLVM 23 中内置头由 **`clang-resource-headers`** target 生成（INTERFACE target，产物在
  `<LLVM_BINARY_DIR>/lib/clang/<major>/include`）；**`clang-headers` 只是 install 组件别名，
  不产出文件**——依赖挂错 target 构建树就没有内置头。
- Windows 本机（MSVC 工具链）自带 `stddef.h`，会掩盖 resource-dir 缺失；Linux CI 才是真实检验。
  本地验证打包布局时，把头按 `package/lib/clang/<ver>/include` 放到 exe 旁再跑。

## CI / release 缓存策略（勿破坏）

- ccache key 固定按 LLVM 版本（`ccache-...-llvm-23.1.3`），**不要**改成 `hashFiles(CMakeLists.txt)`；
  ccache 内容寻址、跨构建累积命中，改 CMake 不应整份失效。
- CPM 的 LLVM 源码缓存必须同时含 `build/_deps/llvm_project-src` 与 `-subbuild`（ExternalProject
  stamp）；只缓存 src 时每次 configure 仍会重下 1.4GB zip。
- ci.yml E2E 冒烟的行号（如 `--position 10:8`）与 `src/sample.cpp` 强耦合：
  改 sample.cpp 布局必须同步改 ci.yml 里的行号与注释。
- release 打包优先复制构建树 `llvm_project-build/lib/clang/*/include`（含 tablegen 产物，完整），
  回退源码 `clang/lib/Headers/*`；两套布局运行时探测都认。

## 调试

- `LOGCOMMENTER_DEBUG=1`：打印 resource-dir 解析结果与内置头命中，并让 clang 打印 include
  搜索路径（`-v`）。
- compile_commands 索引缓存 `<db>.logc-idx` 按 mtime 失效；改了 compile_commands 后行为异常先删它。
- `--inject --position` 模式同时挂 `varDecl` 与 `declRefExpr` 两个 matcher 且都按声明范围过滤，
  已按 canonical decl 去重；若要支持"光标落在引用处"，需让 ref 命中按引用自身范围判定（未实现）。
