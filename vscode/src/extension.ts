// LogCommenter VSCode 插件薄壳（M2，TypeScript）
// 职责：拿选区/光标 → 调短生命周期 CLI → 解析 TextEdit JSON → applyEdit 落地。
// 不解析源码、不常驻进程；所有语义决策在 CLI（LogEngine）内。
//
// 协议（CLI 输出，见 src/logengine/text_edit.{h,cpp}）：
//   单文件：{ "file": "a.cpp", "edits": [...] }
//   跨文件：[ { "file": "a.cpp", "edits": [...] }, { "file": "a.h", "edits": [...] } ]
//   line/col 均 1-based；start==end 为纯插入，text 插入到 (line,col) 字符之前；
//   col = 行长+1 表示行尾。col 是 UTF-8 字节列（LibTooling spelling column）。
// 本插件负责：1-based → 0-based；字节列 ↔ UTF-16 字符列（VSCode 用 UTF-16 code units）。

import * as vscode from 'vscode';
import { execFile } from 'child_process';
import * as path from 'path';
import * as fs from 'fs';
import { byteColToUtf16, utf16ColToByte, normalizeGroups, EditGroup } from './protocol';

const CLI_TIMEOUT_MS = 20000;

let channel: vscode.OutputChannel | undefined;
let _context: vscode.ExtensionContext | undefined;

export function activate(context: vscode.ExtensionContext): void {
  _context = context;
  channel = vscode.window.createOutputChannel('LogCommenter');
  context.subscriptions.push(
    vscode.commands.registerCommand('logcommenter.inject', () => inject(false)),
    vscode.commands.registerCommand('logcommenter.injectFriend', () => inject(true)),
    vscode.commands.registerCommand('logcommenter.comment', () => comment()),
    channel
  );
}

export function deactivate(): void {
  // 薄壳无常驻资源
}

// ---------------------------------------------------------------------------
// 定位
// ---------------------------------------------------------------------------

function wordAt(editor: vscode.TextEditor, pos: vscode.Position): string {
  const range = editor.document.getWordRangeAtPosition(pos);
  if (!range) return '';
  return editor.document.getText(range).trim();
}

interface Target {
  name: string;
  pos: vscode.Position;
}

// 取注入目标：变量名 + 光标位置（VSCode Position）
function resolveTarget(editor: vscode.TextEditor): Target {
  const sel = editor.selection;
  if (!sel.isEmpty && sel.start.line === sel.end.line) {
    const name = editor.document.getText(sel).trim();
    return { name, pos: sel.start };
  }
  const pos = sel.isEmpty ? sel.active : sel.start;
  return { name: wordAt(editor, pos), pos };
}

// ---------------------------------------------------------------------------
// CLI 执行与路径定位
// ---------------------------------------------------------------------------

interface CliResolved {
  // 实际交给 execFile 的命令（绝对路径或 PATH 裸名）
  bin: string;
  // 配置显式指定的解析后绝对路径；空 = 走 PATH fallback
  configuredPath?: string;
}

function resolveCli(rootDir: string | undefined): CliResolved {
  const cfg = vscode.workspace.getConfiguration('logcommenter');
  let p = (cfg.get<string>('cliPath', '') || '').trim();
  if (!p) {
    // Windows 下 CreateProcess 不展开 PATHEXT，裸名直接给 .exe；其他平台给可执行名
    const fallback = process.platform === 'win32' ? 'logcommenter.exe' : 'logcommenter';
    return { bin: fallback };
  }
  let abs = p;
  // 相对路径按 workspace 根解析（execFile 不带 shell，否则按 VSCode 进程 cwd 查找，易 ENOENT）
  if (!path.isAbsolute(abs) && rootDir) abs = path.resolve(rootDir, abs);
  return { bin: abs, configuredPath: abs };
}

function rootDir(): string | undefined {
  return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

async function resolveCompileCommands(
  context: vscode.ExtensionContext, startDir: string): Promise<string> {
  const cfg = vscode.workspace.getConfiguration('logcommenter');
  const p = cfg.get<string>('compileCommands', '');
  if (p) return p;
  // 向上探测 + workspaceState 缓存（同一目录只探一次）
  const cacheKey = 'cc:' + startDir;
  const cached = context.workspaceState.get<string>(cacheKey);
  if (cached && fs.existsSync(cached)) return cached;
  let dir = startDir;
  for (;;) {
    const cand = path.join(dir, 'compile_commands.json');
    if (fs.existsSync(cand)) {
      void context.workspaceState.update(cacheKey, cand);
      return cand;
    }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  throw new Error(
    '未找到 compile_commands.json（自 ' + startDir +
    ' 向上探测；可设置 logcommenter.compileCommands 指定）');
}

function runCli(args: string[]): Promise<string> {
  return new Promise<string>((resolve, reject) => {
    const { bin, configuredPath } = resolveCli(rootDir());
    // 配置了具体路径：前置存在性检查，避免 spawn ENOENT 的英文晦涩报错
    if (configuredPath && !fs.existsSync(configuredPath)) {
      return reject(new Error(
        '配置的 CLI 不存在：' + configuredPath +
        '。请检查设置 logcommenter.cliPath（填 logcommenter.exe 的绝对路径）；' +
        '留空则从 PATH 查找 logcommenter.exe。'));
    }
    channel?.appendLine('$ ' + bin + ' ' + args.join(' '));
    execFile(bin, args, {
      timeout: CLI_TIMEOUT_MS,
      maxBuffer: 32 * 1024 * 1024,
      windowsHide: true
    }, (err, stdout, stderr) => {
      if (err) {
        if ((err as { killed?: boolean }).killed) {
          return reject(new Error('CLI 超时（' + CLI_TIMEOUT_MS / 1000 + 's）'));
        }
        const code = (err as NodeJS.ErrnoException).code;
        if (code === 'ENOENT') {
          return reject(new Error(
            '无法启动 CLI：' + bin + '（文件不存在或不在 PATH）。' +
            '请在设置 logcommenter.cliPath 中填入 logcommenter.exe 的绝对路径；' +
            'release 解压时须保持 lib/clang 目录与 exe 同级。'));
        }
        const msg = (stderr || '').trim();
        return reject(new Error(msg ? msg.split('\n')[0] : err.message));
      }
      resolve(stdout);
    });
  });
}

// ---------------------------------------------------------------------------
// TextEdit JSON → VSCode WorkspaceEdit
// ---------------------------------------------------------------------------

async function applyEdits(groups: EditGroup[]): Promise<{ ok: boolean; count: number }> {
  const we = new vscode.WorkspaceEdit();
  let count = 0;
  for (const g of groups) {
    const uri = vscode.Uri.file(g.file);
    const doc = await vscode.workspace.openTextDocument(uri);
    for (const e of g.edits || []) {
      const r = e.range;
      const sLine = r.start.line - 1;
      const eLine = r.end.line - 1;
      const startChar = byteColToUtf16(doc.lineAt(sLine).text, r.start.col);
      if (r.start.line === r.end.line && r.start.col === r.end.col) {
        we.insert(uri, new vscode.Position(sLine, startChar), e.text);
      } else {
        const endChar = byteColToUtf16(doc.lineAt(eLine).text, r.end.col);
        we.replace(uri, new vscode.Range(sLine, startChar, eLine, endChar), e.text);
      }
      count++;
    }
  }
  const ok = await vscode.workspace.applyEdit(we);
  return { ok, count };
}

// ---------------------------------------------------------------------------
// 命令
// ---------------------------------------------------------------------------

async function inject(withFriend: boolean): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return void vscode.window.showErrorMessage('LogCommenter: 请先打开文件');
  const { name, pos } = resolveTarget(editor);
  if (!name) {
    return void vscode.window.showErrorMessage(
      'LogCommenter: 未选中变量，或光标不在标识符上（C++ 标识符仅含字母/数字/下划线）');
  }
  if (withFriend) {
    const ok = await vscode.window.showWarningMessage(
      'LogCommenter: 对象不可直接 << 时将向类定义处注入 friend operator<<' +
      '（可能修改头文件）。继续？', { modal: true }, '继续');
    if (ok !== '继续') return;
  }
  try {
    const cc = await resolveCompileCommands(context(), path.dirname(editor.document.uri.fsPath));
    const lineText = editor.document.lineAt(pos.line).text;
    const args = [
      '--inject',
      '--file', editor.document.uri.fsPath,
      '--var', name,
      '--position', (pos.line + 1) + ':' + utf16ColToByte(lineText, pos.character),
      '--compile-commands', cc
    ];
    if (withFriend) args.push('--friend');
    const out = await runCli(args);
    const groups = normalizeGroups(out);
    const { ok: applied, count } = await applyEdits(groups);
    if (!applied) return void vscode.window.showErrorMessage('LogCommenter: applyEdit 失败');
    const files = groups.map(g => path.basename(g.file)).join(', ');
    channel?.appendLine('-> 已插入 ' + count + ' 处 edit：' + files);
    void vscode.window.setStatusBarMessage('LogCommenter: 已注入 ' + count + ' 处观测日志', 3000);
  } catch (err) {
    void vscode.window.showErrorMessage('LogCommenter: ' +
      (err instanceof Error ? err.message : String(err)));
  }
}

async function comment(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return void vscode.window.showErrorMessage('LogCommenter: 请先打开文件');
  try {
    const cc = await resolveCompileCommands(context(), path.dirname(editor.document.uri.fsPath));
    const args = [
      '--comment',
      '--file', editor.document.uri.fsPath,
      '--compile-commands', cc
    ];
    const out = await runCli(args);
    const groups = normalizeGroups(out);
    const { ok: applied, count } = await applyEdits(groups);
    if (!applied) return void vscode.window.showErrorMessage('LogCommenter: applyEdit 失败');
    channel?.appendLine('-> 已注释 ' + count + ' 处日志');
    if (count === 0) {
      void vscode.window.showInformationMessage('LogCommenter: 未找到带 pengtest 标记的日志');
    } else {
      void vscode.window.setStatusBarMessage('LogCommenter: 已注释 ' + count + ' 处日志', 3000);
    }
  } catch (err) {
    void vscode.window.showErrorMessage('LogCommenter: ' +
      (err instanceof Error ? err.message : String(err)));
  }
}

// 命令回调需要 ExtensionContext（compile_commands 缓存用）；activate 时闭包注入
function context(): vscode.ExtensionContext {
  if (!_context) throw new Error('extension not activated');
  return _context;
}
