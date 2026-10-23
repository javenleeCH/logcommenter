// LogCommenter VSCode 插件薄壳（M2）
// 职责：拿选区/光标 → 调短生命周期 CLI → 解析 TextEdit JSON → applyEdit 落地。
// 不解析源码、不常驻进程；所有语义决策在 CLI（LogEngine）内。
//
// 协议（CLI 输出，见 src/logengine/text_edit.{h,cpp}）：
//   单文件：{ "file": "a.cpp", "edits": [{ range: {start:{line,col},end:{line,col}}, text }] }
//   跨文件：[ { "file": "a.cpp", "edits": [...] }, { "file": "a.h", "edits": [...] } ]
//   line/col 均 1-based；start==end 为纯插入，text 插入到 (line,col) 字符之前；
//   col = 行长+1 表示行尾。col 是 UTF-8 字节列（LibTooling spelling column）。
// 本插件负责：1-based → 0-based；字节列 ↔ UTF-16 字符列（VSCode 用 UTF-16 code units）。

const vscode = require('vscode');
const { execFile } = require('child_process');
const path = require('path');
const fs = require('fs');
const { byteColToUtf16, utf16ColToByte, normalizeGroups } = require('./protocol');

const CLI_TIMEOUT_MS = 20000;

/** @type {vscode.OutputChannel} */
let channel;

function activate(context) {
  channel = vscode.window.createOutputChannel('LogCommenter');
  context.subscriptions.push(
    vscode.commands.registerCommand('logcommenter.inject', () =>
      inject(context, false)),
    vscode.commands.registerCommand('logcommenter.injectFriend', () =>
      inject(context, true)),
    vscode.commands.registerCommand('logcommenter.comment', () =>
      comment(context)),
    channel
  );
}

function deactivate() {}

// ---------------------------------------------------------------------------
// 定位
// ---------------------------------------------------------------------------

function wordAt(editor, pos) {
  const range = editor.document.getWordRangeAtPosition(pos);
  if (!range) return '';
  return editor.document.getText(range).trim();
}

// 取注入目标：变量名 + 光标位置（VSCode Position）
function resolveTarget(editor) {
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

function resolveCli() {
  const cfg = vscode.workspace.getConfiguration('logcommenter');
  const p = cfg.get('cliPath', '');
  return p ? p : 'logcommenter';
}

async function resolveCompileCommands(context, startDir) {
  const cfg = vscode.workspace.getConfiguration('logcommenter');
  const p = cfg.get('compileCommands', '');
  if (p) return p;
  // 向上探测 + workspaceState 缓存（同一目录只探一次）
  const cacheKey = 'cc:' + startDir;
  const cached = context.workspaceState.get(cacheKey);
  if (cached && fs.existsSync(cached)) return cached;
  let dir = startDir;
  for (;;) {
    const cand = path.join(dir, 'compile_commands.json');
    if (fs.existsSync(cand)) {
      context.workspaceState.update(cacheKey, cand);
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

function runCli(args) {
  return new Promise((resolve, reject) => {
    const bin = resolveCli();
    channel.appendLine('$ ' + bin + ' ' + args.join(' '));
    execFile(bin, args, { timeout: CLI_TIMEOUT_MS, maxBuffer: 32 * 1024 * 1024, windowsHide: true },
      (err, stdout, stderr) => {
        if (err) {
          if (err.killed) return reject(new Error('CLI 超时（' + CLI_TIMEOUT_MS / 1000 + 's）'));
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

async function applyEdits(groups) {
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

async function inject(context, withFriend) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return vscode.window.showErrorMessage('LogCommenter: 请先打开文件');
  const { name, pos } = resolveTarget(editor);
  if (!name) {
    return vscode.window.showErrorMessage(
      'LogCommenter: 未选中变量，或光标不在标识符上（C++ 标识符仅含字母/数字/下划线）');
  }
  if (withFriend) {
    const ok = await vscode.window.showWarningMessage(
      'LogCommenter: 对象不可直接 << 时将向类定义处注入 friend operator<<' +
      '（可能修改头文件）。继续？', { modal: true }, '继续');
    if (ok !== '继续') return;
  }
  try {
    const cc = await resolveCompileCommands(context, path.dirname(editor.document.uri.fsPath));
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
    const { ok, count } = await applyEdits(groups);
    if (!ok) return vscode.window.showErrorMessage('LogCommenter: applyEdit 失败');
    const files = groups.map(g => path.basename(g.file)).join(', ');
    channel.appendLine('-> 已插入 ' + count + ' 处 edit：' + files);
    vscode.window.setStatusBarMessage('LogCommenter: 已注入 ' + count + ' 处观测日志', 3000);
  } catch (err) {
    vscode.window.showErrorMessage('LogCommenter: ' + err.message);
  }
}

async function comment(context) {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return vscode.window.showErrorMessage('LogCommenter: 请先打开文件');
  try {
    const cc = await resolveCompileCommands(context, path.dirname(editor.document.uri.fsPath));
    const args = [
      '--comment',
      '--file', editor.document.uri.fsPath,
      '--compile-commands', cc
    ];
    const out = await runCli(args);
    const groups = normalizeGroups(out);
    const { ok, count } = await applyEdits(groups);
    if (!ok) return vscode.window.showErrorMessage('LogCommenter: applyEdit 失败');
    channel.appendLine('-> 已注释 ' + count + ' 处日志');
    if (count === 0) {
      vscode.window.showInformationMessage('LogCommenter: 未找到带 pengtest 标记的日志');
    } else {
      vscode.window.setStatusBarMessage('LogCommenter: 已注释 ' + count + ' 处日志', 3000);
    }
  } catch (err) {
    vscode.window.showErrorMessage('LogCommenter: ' + err.message);
  }
}

module.exports = { activate, deactivate };
