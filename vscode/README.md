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
   下载 `logcommenter-<platform>-x86_64.zip`，**保持目录布局解压**（exe 与 `lib/clang/`
   目录同级），例如 Windows：解压到 `D:\Tools\logcommenter\`，得到
   `D:\Tools\logcommenter\logcommenter.exe` + `D:\Tools\logcommenter\lib\clang\...`。
2. 在 VSCode 设置 `logcommenter.cliPath` 填入 exe 绝对路径（见下），或把解压目录加入 PATH。
3. 安装插件本体：`vsce package` 生成 `.vsix` → 扩展面板 `...` → 从 VSIX 安装。

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
| `logcommenter.cliPath` | 空（PATH 查找） | CLI 可执行文件路径：绝对路径直接使用；相对路径按 workspace 根解析；Windows 裸名回退 `logcommenter.exe`。**注意保持 release 解压目录布局：`lib/clang/` 必须与 exe 同级。** |
| `logcommenter.compileCommands` | 空（自动探测） | `compile_commands.json` 绝对路径；留空时从当前文件目录向上逐级探测（结果缓存） |

`settings.json` 示例（Windows，路径写正斜杠或双反斜杠）：

```json
{
  "logcommenter.cliPath": "D:/Tools/logcommenter/logcommenter.exe"
}
```

若 CLI 路径不存在，插件会在命令面板与 Output 频道（LogCommenter）给出中文诊断，
不再透传 `spawn ... ENOENT`。

## 协议与坐标系

- CLI 输出 1-based JSON TextEdit：单文件 `{file, edits}`，跨文件（friend 注入到头文件）
  输出 `[{file, edits}, ...]` 数组。
- 行列：CLI 用 1-based UTF-8 字节列（LibTooling spelling column）；VSCode 用 0-based
  UTF-16 code units。插件在两侧做换算，中文/多字节字符列不会偏移。
- 纯插入（start==end）：text 插入到 (line,col) 字符之前；col=行长+1 为行尾。
