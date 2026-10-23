#!/usr/bin/env python3
"""B 组系统测试（E2E）：operator<< 实查 / --friend 注入 / 跨文件协议 / 幂等 / 地址兜底。

覆盖场景（与 tests/e2e/friend_widget.h + friend_sample.cpp 一一对应）：
  A. --inject --var w   → operator<< 实查：单对象输出，观测 [obs] w= 流式（无 (ptr)）
  B. --inject --var g --friend → 跨文件数组：2 组（cpp 观测 + 头文件 friend 注入），
     观测 [obs] g= 流式；friend 文本含 "friend" 与 "operator<<"
  C. apply 后副本必须能再次编译（clang++ -fsyntax-only）
  D. 幂等：对已注入副本再跑 --var g --friend → 无任何新 edit
  E. --inject --var g（无 --friend）→ (ptr) 地址兜底
  F. --inject --var bx --friend（模板类）→ 跳过 friend，仍 (ptr) 兜底

用法: e2e_bgroup.py <logcommenter-exe>
依赖: 系统 clang++（仅用于编译验证）；compile_commands 由脚本自造（单条）。
"""
import json
import os
import shutil
import subprocess
import sys


def fail(msg):
    sys.exit("FAIL: " + msg)


def run(exe, args):
    r = subprocess.run([exe] + args, capture_output=True, text=True)
    if r.returncode != 0:
        fail("%s\n%s" % (args, r.stderr))
    return json.loads(r.stdout)


def apply_edits(path, edits):
    """按 (line,col) 从后往前应用 TextEdit（同文件多 edit 时行号不偏移）。"""
    lines = open(path, encoding="utf-8").read().splitlines(keepends=True)
    ordered = sorted(edits, key=lambda e: (-e["range"]["start"]["line"],
                                           -e["range"]["start"]["col"]))
    for e in ordered:
        ln, col = e["range"]["start"]["line"], e["range"]["start"]["col"]
        lines[ln - 1] = lines[ln - 1][:col - 1] + e["text"] + lines[ln - 1][col - 1:]
    return "".join(lines)


def clean(*paths):
    for p in paths:
        if os.path.exists(p):
            os.remove(p)
        if os.path.exists(p + ".logc-idx"):
            os.remove(p + ".logc-idx")


def main():
    exe = os.path.abspath(sys.argv[1])
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    e2e_dir = os.path.join(root, "tests", "e2e")
    src = os.path.join(e2e_dir, "friend_sample.cpp")
    hdr = os.path.join(e2e_dir, "friend_widget.h")

    # 0) 自造单条 compile_commands（绝对路径，目录为 e2e 目录）
    cc_path = os.path.join(e2e_dir, "cc_bgroup.json")
    command = "clang++ -std=c++17 -fsyntax-only -I%s %s" % (e2e_dir, src)
    with open(cc_path, "w", encoding="utf-8") as f:
        json.dump([{"file": src, "directory": e2e_dir, "command": command}], f)

    # A) operator<< 实查：Widget 有重载 → 流式，不落 (ptr)
    d = run(exe, ["--inject", "--file", src, "--var", "w",
                  "--compile-commands", cc_path])
    if isinstance(d, list):
        fail("var w (streamable) should stay single-file output: %s" % d)
    t = json.dumps(d)
    if "[obs] w=" not in t or "(ptr)" in t:
        fail("operator<< lookup failed for w: %s" % t)

    # B) --friend：Gadget 无重载 → 跨文件数组（cpp 观测 + 头文件 friend）
    d = run(exe, ["--inject", "--file", src, "--var", "g", "--friend",
                  "--compile-commands", cc_path])
    if not isinstance(d, list):
        fail("friend inject should emit multi-file array: %s" % json.dumps(d))
    if len(d) != 2:
        fail("expected 2 file groups (cpp obs + h friend): %s" % json.dumps(d))
    obs_edits = None
    fr_edits = None
    for g in d:
        f = g["file"].replace("\\", "/")
        if f.endswith("friend_sample.cpp"):
            obs_edits = g["edits"]
        elif f.endswith("friend_widget.h"):
            fr_edits = g["edits"]
    if obs_edits is None or fr_edits is None:
        fail("missing cpp/header group: %s" % json.dumps(d))
    ot = json.dumps(obs_edits)
    ft = json.dumps(fr_edits)
    if "[obs] g=" not in ot or "(ptr)" in ot:
        fail("friend obs not streamable: %s" % ot)
    if "operator<<" not in ft or "friend" not in ft:
        fail("friend text missing: %s" % ft)

    # C) apply（头文件副本换名 + cpp 副本改 include）→ 编译验证
    hdr_e2e = os.path.join(e2e_dir, "friend_widget_e2e.h")
    applied = os.path.join(e2e_dir, "e2e_applied_friend_sample.cpp")
    with open(hdr_e2e, "w", encoding="utf-8") as f:
        f.write(apply_edits(hdr, fr_edits))
    content = apply_edits(src, obs_edits)
    content = content.replace('"friend_widget.h"', '"friend_widget_e2e.h"')
    with open(applied, "w", encoding="utf-8") as f:
        f.write(content)
    cc = shutil.which("clang++")
    if not cc:
        fail("clang++ not found; cannot verify applied copy compiles")
    r = subprocess.run([cc, "-std=c++17", "-fsyntax-only", "-I", e2e_dir,
                        applied], capture_output=True, text=True)
    if r.returncode != 0:
        fail("applied copy does not compile:\n%s\n%s" % (r.stdout, r.stderr))

    # D) 幂等：对副本再跑 --var g --friend → 无新 edit（marker + 已有 friend）
    applied_cc = os.path.join(e2e_dir, "cc_applied.json")
    with open(applied_cc, "w", encoding="utf-8") as f:
        json.dump([{"file": applied, "directory": e2e_dir,
                    "command": "clang++ -std=c++17 -fsyntax-only -I%s %s" %
                    (e2e_dir, applied)}], f)
    d2 = run(exe, ["--inject", "--file", applied, "--var", "g", "--friend",
                   "--compile-commands", applied_cc])
    n = len(d2) if isinstance(d2, list) else len(d2["edits"])
    if n != 0:
        fail("friend/idempotency broken: %s" % json.dumps(d2))

    # E) 无 --friend → (ptr) 地址兜底
    d = run(exe, ["--inject", "--file", src, "--var", "g",
                  "--compile-commands", cc_path])
    if isinstance(d, list) or "(ptr)" not in json.dumps(d):
        fail("expected ptr fallback without --friend: %s" % json.dumps(d))

    # F) 模板类 + --friend → 跳过注入，仍 (ptr)
    d = run(exe, ["--inject", "--file", src, "--var", "bx", "--friend",
                  "--compile-commands", cc_path])
    if isinstance(d, list):
        fail("template class must skip friend (single-file): %s" % json.dumps(d))
    if "(ptr)" not in json.dumps(d):
        fail("template class should fall back to ptr: %s" % json.dumps(d))

    clean(hdr_e2e, applied, cc_path, applied_cc)
    print("E2E B-group (lookup/friend/multi-file/idempotent/fallback) OK")


if __name__ == "__main__":
    main()
