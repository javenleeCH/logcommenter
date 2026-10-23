# LogCommenter for VSCode

C++ 源码日志工具薄壳：右键一键 **注释日志**（`pengtest` 标记）/ **注入类型感知观测日志**。
语义决策全部在短生命周期 CLI（LogEngine，基于 Clang/LibTooling）内完成，本插件只做
选区 → 调进程 → applyEdit 落地，不常驻、不解析源码、不与 clangd 抢内存。

## 构建（TypeScript）

```bash
cd vscode
npm install          # devDeps: typescript / @types/node / @types/vscode
npm run build        # tsc → dist/（package.json main 指向 dist/src/extension.js）
npm test             # build + node --test dist/test/protocol.test.js
```

源码在 `src/`（`extension.ts` 薄壳主逻辑 + `protocol.ts` 协议层），单测在 `test/`。
`dist/` 为编译产物（gitignore），发布/CI 时构建。

## 安装

1. 先安装 CLI：从 [GitHub Releases](https://github.com/javenleeCH/logcommenter/releases/latest)
   下载 `logcommenter-<platform>-x86_64.zip`，解压后把 `logcommenter(.exe)` 加入 PATH
   （或配置 `logcommenter.cliPath` 指向绝对路径）。
2. 安装插件本体：
   - 源码方式：`npm install -g @vscode/vsce && vsce package` 生成 `.vsix`，
     VSCode 扩展面板 → `...` → 从 VSIX 安装。
   - 或先把 `npm run build` 的产物 + `package.json` 拷到 `~/.vscode/extensions/logcommenter/`。

## 使用

打开任意 `.cpp/.c/.cu` 文件：

| 右键命令 | 行为 |
|---|---|
| **注入观测日志** | 选中变量（或光标置于变量上）→ 在声明后插入 `std::cout << "[obs] name=" << name << std::endl;`。对象不可直接 `<<` 时自动降级为打印地址。 |
| **注入观测日志（Friend 注入）** | 同上，但对象不可流式且类型为非模板完整类时，向类定义处注入 `friend operator<<`（可能修改头文件，执行前二次确认）。 |
| **注释日志** | 注释当前文件中带 `pengtest` 标记的日志行（可逆）。 |

重复调用幂等：已注入的观测行（`[obs] var=` marker）与已注释语句自动跳过。

## 配置

| 配置项 | 默认 | 说明 |
|---|---|---|
| `logcommenter.cliPath` | 空（PATH 查找） | CLI 可执行文件绝对路径 |
| `logcommenter.compileCommands` | 空（自动探测） | `compile_commands.json` 绝对路径；留空时从当前文件目录向上逐级探测（结果缓存） |

## 协议与坐标系

- CLI 输出 1-based JSON TextEdit：单文件 `{file, edits}`，跨文件（friend 注入到头文件）
  输出 `[{file, edits}, ...]` 数组。
- 行列：CLI 用 1-based UTF-8 字节列（LibTooling spelling column）；VSCode 用 0-based
  UTF-16 code units。插件在两侧做换算，中文/多字节字符列不会偏移。
- 纯插入（start==end）：text 插入到 (line,col) 字符之前；col=行长+1 为行尾。
