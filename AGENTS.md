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
  并以 `unix-default-debug` preset（Debug）编译，可单步进 clang::tooling 等库函数。门禁（CI/release）
  始终是 release 版 + Release 构建，不受影响。
- 纯逻辑单测（改 `src/logengine/` 后必跑，无 Clang 依赖）：
  `cmake --build build/unix-default --target logcommenter_tests && ./build/unix-default/logcommenter_tests`
- E2E 冒烟（与 CI `ast` job 的四场景一一对应，见 `.github/workflows/ci.yml`）：
  对 `src/sample.cpp` 跑 `--comment` / `--inject --var total` / `--inject --position 10:8` /
  `--inject --var cfg`，compile-commands 用 `build/unix-default/compile_commands.json`。
- 只改 `logengine`（纯逻辑）时用 `-DLOGCOMMENTER_BUILD_AST=OFF` 可跳过 LLVM。
- VSCode 插件薄壳（`vscode/`，TypeScript）：`cd vscode && npm install && npm test`
  （tsc → `dist/`，`node --test dist/test/protocol.test.js`；CI `plugin` job）。
  `src/extension.ts` 只做选区→CLI→applyEdit；`src/protocol.ts` 是协议层（字节列↔UTF-16
  换算 + JSON 归一），不依赖 vscode，改协议/换算必跑单测。CLI 路径解析规则：配置绝对路径
  直接用；相对路径按 workspace 根解析；留空 Windows 回退 `logcommenter.exe`。

## 产品与技术约束（源自 docs/PRODUCT_TECH_PLAN.md，改动不得违背）

- 分层：VSCode 插件薄壳（选区/右键/applyEdit）→ 短生命周期 CLI（起→解析→返回→退出）
  → LogEngine。不 fork clangd、不常驻进程、不与 clangd 抢内存。
- CLI 默认只输出 1-based JSON TextEdit、不回写文件；落地与否由调用方 applyEdit 决定。
  显式传 --write 时 CLI 才直接改写目标文件（含跨文件 friend 头文件），语义与
  applyEdit 完全一致（1-based UTF-8 字节列、插入在 (line,col) 字符之前、CRLF/BOM
  保持、多行文本按目标行尾风格真实换行），实现见 src/logengine/file_apply.{h,cpp}。
- compile_commands 用偏移索引（`.logc-idx`）seek+read 单条取参，绝不整载 1GB；
  索引按 mtime 失效。
- 幂等（产品要求）：`--comment` 天然跳过已注释语句；`--inject` 不得重复注入——
  单次调用内已按 canonical decl 去重；**跨调用**由 `alreadyObserved()`
  （`src/clang/engine_clang.cpp`，插入点后至多两行内匹配该变量的 `[obs]` 标记）
  跳过已注入语句；回环幂等由 `tools/e2e_apply_check.py` 与 `tools/e2e_bgroup.py`
  的 E2E 覆盖（应用 edit 后重新注入不产生新 edit）。
- 对象观测优先级：有 `operator<<` 直接流式；用户可改类走 friend 注入（需用户显式确认，
  改头文件有副作用）；第三方/系统类型回退地址打印。
- 两份规划文档（本地 md 与飞书）均已同步为 LLVM/Clang 23.1.3（预编译静态工具链，find_package 消费）。

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
- Debug 版 asset 体积较大（带 -g 符号 + 源码树，可能 >1GB/平台），只在本地需要步进调试时下载；
  CI/release 一律用 release 版，勿引入 debug 到门禁。
- ci.yml E2E 冒烟的行号（如 `--position 10:8`）与 `src/sample.cpp` 强耦合：
  改 sample.cpp 布局必须同步改 ci.yml 里的行号与注释。
- release 打包从预编译工具链 `.toolchain/lib/clang/*/include` 复制资源头（含 tablegen 产物，完整）。

## VSCode 插件 npm 发布（GitHub Packages，坑）

- GitHub Packages 只接受 `@owner/` 作用域包名：unscoped `npm publish` 一律 E404。
  npm 的 `--scope` 只影响 install 的 registry 解析，**不会改发布名**；而 VSCE 不支持
  scoped name，所以 `publish-npm.yml` 在发布步内临时 `npm pkg set name='@javenleech/logcommenter'`。
- 插件版本以 release tag 为唯一来源（workflow 内 `npm pkg set version` 同步），
  `vscode/package.json` 的 version 字段可能滞后，勿拿它判断 release 版本。
- npm publish 步骤曾用 `continue-on-error: true` 兜底，E404 失败被掩盖成绿门禁——已移除。
  发布步排在 VSIX attach 之后：npm 失败只红 job，不影响 VSIX 挂到 release。
  重跑时 `npm view` 预检已存在版本则跳过（幂等）；token 失效仍会红，不被跳过掩盖。
- 打包白名单统一用 `package.json` 的 `"files": ["dist"]`（npm 与 vsce 3.x 双端生效）：
  `dist/` 被 gitignore，npm pack 默认排除它，没有白名单发布的包里就没有 `main` 指向的
  编译产物；`.npmignore` 也解不掉（npm 会合并 `.npmignore` 与 `.gitignore` 规则）。
  **vsce 报错拒绝 `.vscodeignore` 与 `"files"` 并存**，故 `.vscodeignore` 已删，勿加回。

## 调试

- `LOGCOMMENTER_DEBUG=1`：打印 resource-dir 解析结果与内置头命中，并让 clang 打印 include
  搜索路径（`-v`）。
- **PCH 前置头缓存**（解析加速，8s 级重模板头 → 3s 级）：首次运行后台 detach
  自调（`--logc-internal-pch` 隐藏旗标）构建，之后命中 `<db>.logc-pch/<hash>/`
  的 `preamble.hpp.pch`；哈希 = clang 版本 + resource-dir + 编译参数原文 +
  指令层 preamble（`src/logengine/preamble.{h,cpp}`：保留全部指令/注释行、
  代码行置空保行号——指令层决定预处理状态，与全量解析等价）。装载走
  `-Xclang -include-pch`（cl/gcc 驱动模式通用）。三个坑：
  1. 进程内 ToolInvocation **不走**驱动的 job 派生：/Fp、-o、-x c++-header
     全被忽略（ProgramAction 停在编译语义、输出名被派生成 obj 名）——
     PCH 输出必须用自定义 action 在 BeginSourceFileAction 里钉死
     FrontendOpts.OutputFile/ProgramAction（见 PchBuildAction）。
  2. prune 陈旧哈希目录的比较必须走 fs::path（directory_iterator 产生反斜杠，
     我们拼接的是正斜杠，字符串永不相等会把当前目录也删掉）。
  3. 后台构建诊断必须 IgnoringDiagConsumer 静默（Unix fork 子进程共享父进程
     的流）；失败经 error marker 文件沉淀防反复 spawn。PCH 装载失败自动弃
     缓存裸解析重试（头文件被改等）。删除 `.logc-pch` 即强制重建；
     `LOGC_DISABLE_PCH=1` 可整体跳过（测试隔离用，e2e 脚本的 run 均已带）。
- CLI stderr 协议只承载自身 error：`EngineAction::BeginSourceFileAction` 里
  `CI.getDiagnostics().setIgnoreAllWarnings(true)`（LLVM 23 的 -w 等价物，仅
  Warning 级；Error/Fatal 保留——解析失败必须能诊断），clang 对被分析源码的
  -Wdeprecated/-Wunused 等噪声不进 stderr。LLVM 23 无 setSuppressAllWarnings。
- 想单步进入 LLVM/Clang 库内部：`./build.sh debug`（或手动下载 `.toolchain-debug` 并以
  `unix-default-debug` / `windows-msvc-debug` preset 以 Debug 编译）。普通断点只在自己代码里时，release 版即可。
- debug asset 内附带完整 LLVM 源码树 `.toolchain-debug/llvm-src`（调试符号记录的是编译机
  绝对路径）。要定位到源码行，做源码映射：
  - gdb：`set substitute-path <编译机源码根> <本地 llvm-src 路径>`
  - lldb：`settings set target.source-map <编译机源码根> <本地 llvm-src 路径>`
  编译机源码根即 llvm-project 解压根：Linux 为 `/home/runner/work/<repo>/<repo>/llvm-project`，
  Windows 为 `D:\a\<repo>\<repo>\llvm-project`；本地路径为 `.toolchain-debug/llvm-src`。
- compile_commands 索引缓存 `<db>.logc-idx`（v3）按 mtime+size 双检失效：mtime 只有秒级
  精度，同秒重写 db 靠 size 变化兜住；键在 saveCache 时必须 JSON 转义——v2 未转义时
  Windows 反斜杠路径经缓存往返被 loadCache 的转义解析吞掉 `\`，第二次查找必失败
  （B-group E2E 在 Windows 从未绿过即此因）。改了 compile_commands 后行为异常仍可先删它。
- compile_commands 路径匹配：比较必须走 `normalizeCompare`（统一斜杠 + Windows 盘符/路径段
  小写折叠，Linux 保持大小写敏感）。VSCode `document.uri.fsPath` 返回小写盘符 `d:\...`，
  而条目常存大写 `D:/...`；`_fullpath`/GetFullPathName 保留盘符原始大小写、不折叠，只统一
  斜杠字符串比较仍不等 → 插件报 "file not found in compile_commands"。命令行传相对路径能
  命中只是走了绝对化兜底，不代表匹配层没问题。
- `--inject --position` 模式同时挂 `varDecl` 与 `declRefExpr` 两个 matcher：命中哪个就按
  哪个自身的源码范围过滤位置，再按 canonical decl 去重。顺序不可反——先去重会让
  "位置在引用处"的命中被声明命中提前占用 seen_ 而整体落空。
- **主文件收敛（P0）**：`--var`/`--position`/`--comment` 命中头文件同名声明（行号与主
  文件撞号）或宏展开合成位置（filename 空、范围跨数百行）时，(line,col) 不属于主
  文件却被当主文件行号输出——真实 monorepo 大头文件（vulkan/glm/STL）必炸。
  `handleInject`/`handleComment` 先过 `SM.getFileID(SM.getFileLoc(...)) ==
  SM.getMainFileID()` 过滤，`EndSourceFileAction` 的 inserts 循环再兜底滤一道
  （协议不变量：非 friend edit 必须在主文件内）。
- **函数指针 typedef 原型形参崩溃**：C 头文件大量 `typedef Ret (*PFN_x)(T arg)` 的
  形参不属于任何 FunctionDecl，`getParentFunctionOrMethod()` 返回 null，
  `dyn_cast<FunctionDecl>(null DeclContext*)` 直接解引用崩溃（0xC0000005）。
  必须 `dyn_cast_or_null`。回归：`tests/e2e/noise_sample.cpp`（e2e_bgroup G 组）。
- 注入插入点 = 声明语句（DeclStmt，含 `;`）之后；for/if/switch/while 头部声明插到
  复合语句体开头，体无大括号则放弃。TextEdit 语义：纯插入在 (line,col) 字符之前
  （1-based，col=行长+1 为行尾）。**改插入逻辑后必须跑 `tools/e2e_apply_check.py`**
  （应用 edit 后源码仍可解析 + 幂等）——CI 只 grep JSON 文本时，插入点落在 token
  中间的缺陷曾漏网（"42" 被劈成两半）。
- 多行 `--comment`：流式日志逐层上溯取整条 operator<< 链，语句覆盖的每一行行首
  都插 `// `；局限：跨行字符串字面量内部也会被插注释（罕见，接受）。
