#!/usr/bin/env python3
"""E2E 校验：把 --inject 产出的 TextEdit 真正应用到源文件副本上，验证
(1) 应用后的文件仍是合法 C++（能被工具再次解析并找到 pengtest 日志）；
(2) 对已注入的副本重复 --inject 是幂等的（方案 §3.4：重复调用不叠加）。
此前 CI 只 grep JSON 文本、从不应用 edit，插入点落在 token 中间的 P0 缺陷
（把 "42" 劈成两半）就是从这条缝里漏过去的。

用法: e2e_apply_check.py <logcommenter> <source.cpp> <compile_commands.json>
"""
import json
import os
import subprocess
import sys

exe, src_path, cc_path = sys.argv[1], sys.argv[2], sys.argv[3]
src_dir, src_name = os.path.split(os.path.abspath(src_path))
# 与原文件同目录：相对 include 与编译参数才仍然成立
applied = os.path.join(src_dir, "e2e_applied_" + src_name)
cc_applied = applied + ".cc.json"


def run(args):
    r = subprocess.run([exe] + args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("FAIL: %s\n%s" % (args, r.stderr))
    return json.loads(r.stdout)


# 1) 注入 total 观测
d = run(["--inject", "--file", src_path, "--var", "total",
         "--compile-commands", cc_path])
assert d["edits"], "inject(var) produced no edits"
lines = open(src_path, encoding="utf-8").read().splitlines(keepends=True)
for e in d["edits"]:
    # 语义：插入到 (line,col) 字符之前（1-based；col = 行长+1 为行尾）
    ln, col = e["range"]["start"]["line"], e["range"]["start"]["col"]
    lines[ln - 1] = lines[ln - 1][:col - 1] + e["text"] + lines[ln - 1][col - 1:]
with open(applied, "w", encoding="utf-8", newline="") as f:
    f.write("".join(lines))

# 2) 造一份指向副本的单条 compile_commands：沿用原条目参数，
#    源文件 token 换名（工具会剥掉与 --file 同名的 token 再注入）
cc = json.load(open(cc_path, encoding="utf-8"))
entry = [e for e in cc if e["file"].replace("\\", "/").endswith(src_name)][0]
entry = dict(entry)
entry["file"] = applied.replace("\\", "/")
entry["command"] = entry["command"].replace(
    src_name, os.path.basename(applied))
with open(cc_applied, "w", encoding="utf-8") as f:
    json.dump([entry], f)

# 3) 副本必须仍能解析：--comment 能在上面重新找到 pengtest 日志
d2 = run(["--comment", "--file", applied, "--compile-commands", cc_applied])
assert any('"// "' in json.dumps(x) for x in d2["edits"]), \
    "applied copy no longer parses (comment found nothing)"

# 4) 幂等：对已注入的副本再次 --inject 同一变量，不应产生新 edit
d3 = run(["--inject", "--file", applied, "--var", "total",
          "--compile-commands", cc_applied])
assert not d3["edits"], "idempotency broken: %s" % d3["edits"]

# 成功后清理（失败时保留现场便于排查）；.logc-idx 是工具在 db 旁生成的索引缓存
os.remove(applied)
os.remove(cc_applied)
if os.path.exists(cc_applied + ".logc-idx"):
    os.remove(cc_applied + ".logc-idx")
print("E2E inject-apply-parse-idempotent OK")
