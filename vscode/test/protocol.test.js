// 协议层单测：node --test test/
// 覆盖：字节列↔UTF-16 换算（含中文/多字节字符）、跨文件数组协议、edit 定位。
const test = require('node:test');
const assert = require('node:assert');
const { byteColToUtf16, utf16ColToByte, normalizeGroups, editPositions } =
  require('../protocol.js');

test('ascii 行列恒等', () => {
  const ascii = 'int total = 42;';
  assert.strictEqual(byteColToUtf16(ascii, 5), 4);          // 1-based→0-based
  assert.strictEqual(utf16ColToByte(ascii, 4), 5);          // 0-based→1-based
  assert.strictEqual(utf16ColToByte(ascii, ascii.length), ascii.length + 1); // 行尾
  assert.strictEqual(byteColToUtf16(ascii, ascii.length + 1), ascii.length);
});

test('中文行：双向换算一致', () => {
  // 'std::cout << "日志" << x;'  ASCII 前缀 14 字节，日/志各 3 字节
  const cn = 'std::cout << "日志" << x;';
  const xUtf16 = cn.indexOf('x');
  const xByte = Buffer.from(cn.slice(0, xUtf16), 'utf8').length + 1;
  assert.strictEqual(utf16ColToByte(cn, xUtf16), xByte);
  assert.strictEqual(byteColToUtf16(cn, xByte), xUtf16);
});

test('多字节字符中间回退到字符起点', () => {
  const cn = 'std::cout << "日志" << x;';
  assert.strictEqual(byteColToUtf16(cn, 17), cn.indexOf('日')); // 日中间→日起点
  assert.strictEqual(byteColToUtf16(cn, 19), cn.indexOf('志')); // 志中间→志起点
  assert.strictEqual(byteColToUtf16(cn, 18), cn.indexOf('志')); // 志起点不回退
});

test('行尾 col=字节长+1 映射到 UTF-16 行尾', () => {
  const cn = 'std::cout << "日志" << x;';
  assert.strictEqual(
    byteColToUtf16(cn, Buffer.byteLength(cn, 'utf8') + 1), cn.length);
});

test('normalizeGroups：单对象与跨文件数组', () => {
  const single = { file: 'a.cpp', edits: [] };
  assert.deepStrictEqual(normalizeGroups(JSON.stringify(single)),
    [{ file: 'a.cpp', edits: [] }]);
  const multi = [{ file: 'a.cpp', edits: [] }, { file: 'a.h', edits: [] }];
  assert.deepStrictEqual(
    normalizeGroups(JSON.stringify(multi)).map(g => g.file), ['a.cpp', 'a.h']);
});

test('editPositions：1-based 字节列 → 0-based UTF-16 位置', () => {
  const cn = 'std::cout << "日志" << x;';
  const xUtf16 = cn.indexOf('x');
  const xByte = Buffer.from(cn.slice(0, xUtf16), 'utf8').length + 1;
  const p = editPositions(
    { range: { start: { line: 1, col: xByte }, end: { line: 1, col: xByte } },
      text: ' /* obs */' }, [cn]);
  assert.strictEqual(p.line, 0);
  assert.strictEqual(p.char, xUtf16);
  assert.strictEqual(p.isInsert, true);
});

test('editPositions：区间替换（start<end）', () => {
  const lines = ['int a = 1;', 'int b = 2;'];
  const p = editPositions(
    { range: { start: { line: 2, col: 1 }, end: { line: 2, col: 5 } },
      text: 'x' }, lines);
  assert.strictEqual(p.line, 1);
  assert.strictEqual(p.endLine, 1);
  assert.strictEqual(p.isInsert, false);
});
