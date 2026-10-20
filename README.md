# logcommenter — 日志自动注释器

基于 **Clang LibTooling**（Clang AST 改写）的源码级日志清理工具：自动查找源码中**含有指定标记字符串的日志语句**，并在其前面插入 `//` 注释掉，用于批量禁用不需要的调试日志。

## 它解决什么问题

开发时经常临时往代码里加日志（`std::cout << "..."`、自定义的 `log(...)` 调用），上线或演示前想批量禁用。逐个手删慢、容易漏，删了再调试还得补回来。

logcommenter 的做法：

1. 在要清理的日志里放一个**私有标记字符串**（默认 `pengtest`）；
2. 跑一次工具，把所有含该标记的日志语句**自动注释掉**；
3. 需要恢复时撤掉注释即可——用的是注释而非删除，操作可逆。

## 工作原理

基于 Clang LibTooling 的 `MatchFinder` + `Rewriter`：

- 匹配器命中两类节点：
  - **函数调用**：参数中含字符串字面量且命中标记，如 `log("pengtest ...")`；
  - **流式输出**：`cout / cerr / clog` 上的 `operator<<` 且命中标记，如 `std::cout << "pengtest ..."`；
- `Rewriter` 在命中语句前插入 `// `，把该日志静音；
- 已注释的语句、系统头文件内的命中会自动跳过，不会重复注释；
- 通过 `ASTFrontendAction` 处理完所有源文件后，原地写回改动。

## 构建

项目用 [CPM](https://github.com/cpm-cmake/CPM.cmake) 自动拉取 `llvm-project` 源码（当前锁定 `llvmorg-21.1.4`）并现场编译 Clang/LLVM 库。

```bash
mkdir build && cd build
cmake ..
make
```

> 注意：首次构建会完整编译 LLVM + Clang，耗时较长。Windows 下可使用仓库内置的 `CMakePresets.json`（VS2022 + Ninja + ccache）预设。

## 用法

```bash
logcommenter -p <compile_commands.json 所在目录> <待处理的源文件...>
```

`-p` 指向 CMake 生成的 `compile_commands.json` 所在目录（工具据此获取每个文件的编译参数），后面列出要处理的源文件。示例：

```bash
./out/build/VS/logcommenter.exe -p ./out/build/VS ./src/sample.cpp
```

工具会**原地改写**源文件。`src/sample.cpp` 内置了一个含 `pengtest` 标记的样例，可直接用作验证对象。

## 标记字符串

默认标记为 `pengtest`，在 `src/main.cpp` 的 `isTargetString()` 中修改成你自己的标记即可：

```cpp
bool isTargetString(const clang::StringLiteral *Str) {
  StringRef Content = Str->getString();
  return Content.find("pengtest") != StringRef::npos;  // 换成你的标记
}
```

## 目录结构

```
logcommenter/
├── CMakeLists.txt      # CMake 构建配置（CPM 拉取 llvm-project）
├── CMakePresets.json   # VS2022 + Ninja 预设
├── cmake/
│   └── CPM.cmake       # CPM 依赖管理
├── src/
│   ├── main.cpp        # logcommenter 主程序（AST 匹配 + 改写）
│   └── sample.cpp      # 待处理的样例源码（含 pengtest 标记）
└── .vscode/            # VS Code 调试/编译配置
```

## 当前限制与演进方向

- **是"注释掉"而非物理删除**：日志以 `// ` 形式保留在源码里，保证可逆；
- **匹配基于字符串字面量**，不做更深的语义判断。演进方向：按表达式 / 宏 / 日志级别做更精确的语义匹配，或支持 `#ifdef LOG_DISABLED` 式的条件编译静音。
