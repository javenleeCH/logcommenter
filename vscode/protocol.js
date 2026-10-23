// LogCommenter 协议层：CLI TextEdit JSON 解析与坐标系换算。
// 不依赖 vscode，可独立单测（node --test 或任意 runner）。

// CLI 列（1-based，UTF-8 字节，LibTooling spelling column）→ UTF-16 code units 数
function byteColToUtf16(lineText, byteCol1) {
  const buf = Buffer.from(lineText, 'utf8');
  let n = Math.max(0, byteCol1 - 1);
  if (n > buf.length) n = buf.length;
  // 若落在多字节字符中间，回退到该字符的起始字节
  while (n > 0 && (buf[n] & 0xc0) === 0x80) n--;
  return Buffer.from(buf.subarray(0, n)).toString('utf8').length;
}

// UTF-16 code units 数（VSCode 0-based 字符列）→ CLI 列（1-based UTF-8 字节）
function utf16ColToByte(lineText, utf16Col) {
  const prefix = lineText.slice(0, Math.max(0, utf16Col));
  return Buffer.from(prefix, 'utf8').length + 1;
}

// 单对象或数组 → 统一为 [{file, edits}]
function normalizeGroups(raw) {
  const obj = typeof raw === 'string' ? JSON.parse(raw) : raw;
  if (Array.isArray(obj)) return obj;
  return [{ file: obj.file, edits: obj.edits }];
}

// edit 位置 → {line, char}（0-based UTF-16），供 VSCode Position/Range 使用。
// lineTexts: 目标文件行文本数组（0-based 索引），用于字节列换算。
function editPositions(e, lineTexts) {
  const r = e.range;
  const sLine = r.start.line - 1;
  const eLine = r.end.line - 1;
  return {
    line: sLine,
    char: byteColToUtf16(lineTexts[sLine], r.start.col),
    endLine: eLine,
    endChar: byteColToUtf16(lineTexts[eLine], r.end.col),
    isInsert: r.start.line === r.end.line && r.start.col === r.end.col
  };
}

module.exports = { byteColToUtf16, utf16ColToByte, normalizeGroups, editPositions };
