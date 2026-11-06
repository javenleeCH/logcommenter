param(
  [string]$Exe = "build\windows-msvc\logcommenter.exe",
  [string]$Cc = "build\windows-msvc\compile_commands.json"
)
$ErrorActionPreference = "Stop"
# 复制 sample.cpp 为 e2e 临时主文件，并造一份指向它的 compile_commands
$src = Resolve-Path "src\sample.cpp"
$dir = Split-Path $src
$applied = Join-Path $dir "e2e_write_sample.cpp"
Copy-Item $src $applied -Force
$before = [System.IO.File]::ReadAllBytes($applied)
$ccObj = Get-Content $Cc -Raw | ConvertFrom-Json
$entry = $ccObj | Where-Object { $_.file -replace "\\","/" -match "sample\.cpp$" } | Select-Object -First 1
$entry = $entry.PSObject.Copy()
$entry.file = ($applied -replace "\\","/")
$entry.command = $entry.command -replace [regex]::Escape("sample.cpp"), "e2e_write_sample.cpp"
$ccApplied = "$applied.cc.json"
$entry | ConvertTo-Json -Depth 5 | Set-Content $ccApplied -Encoding UTF8
# 单条目 JSON 数组
$one = "[$($entry | ConvertTo-Json -Depth 5 -Compress)]"
[System.IO.File]::WriteAllText($ccApplied, $one, (New-Object System.Text.UTF8Encoding($false)))

$env:LOGC_DISABLE_PCH = "1"
try {
  # 1) --write 注入
  $out = & $Exe --inject --file $applied --var total --write --compile-commands $ccApplied 2>&1
  if ($LASTEXITCODE -ne 0) { throw "inject --write failed: $out" }
  # CLI stdout 是完整 JSON（首字符 { 或 [）；stderr 干净时 out 即行数组
  $raw = "$out"
  $m = [regex]::Match($raw, '(?s)[\{\[].*[\}\]]\s*$')
  if (-not $m.Success) { throw "no JSON in output: $raw" }
  $d = $m.Value | ConvertFrom-Json
  if (-not $d.edits -or $d.edits.Count -lt 1) { throw "no edits in JSON output" }
  $after = [System.IO.File]::ReadAllText($applied)
  if ($after -notmatch "pengtest") { throw "file lost content" }
  if ($after -notmatch "\[obs\]") { throw "--write did not modify the file (no [obs] marker found)" }
  if ($after -eq [System.Text.Encoding]::UTF8.GetString($before)) { throw "file unchanged" }
  Write-Host "PASS: --write modified the file and still printed JSON"

  # 2) 写后的文件必须仍可解析（--comment 能重新找到日志行）
  $out2 = & $Exe --comment --file $applied --compile-commands $ccApplied 2>&1
  if ($LASTEXITCODE -ne 0) { throw "comment on written file failed: $out2" }
  $raw2 = "$out2"
  $m2 = [regex]::Match($raw2, '(?s)[\{\[].*[\}\]]\s*$')
  if (-not $m2.Success) { throw "no JSON in comment output: $raw2" }
  $j2 = $m2.Value | ConvertFrom-Json
  if (-not ($j2.edits | Where-Object { ($_.text -match "^// ") -or ($_.text -match "`n// ") })) {
    throw "comment found nothing on written file (parse broken?)"
  }
  Write-Host "PASS: written file still parses (comment found pengtest line)"

  # 3) 幂等：再次 --inject 不应产生新 edit
  $out3 = & $Exe --inject --file $applied --var total --compile-commands $ccApplied 2>&1
  if ($LASTEXITCODE -ne 0) { throw "re-inject failed: $out3" }
  $raw3 = "$out3"
  $m3 = [regex]::Match($raw3, '(?s)[\{\[].*[\}\]]\s*$')
  if (-not $m3.Success) { throw "no JSON in re-inject output: $raw3" }
  $j3 = $m3.Value | ConvertFrom-Json
  if ($j3.edits -and $j3.edits.Count -gt 0) { throw "idempotency broken: edits produced again" }
  Write-Host "PASS: idempotent (second --inject produced no edits)"
} finally {
  Remove-Item $applied -ErrorAction SilentlyContinue
  Remove-Item $ccApplied -ErrorAction SilentlyContinue
  Remove-Item "$ccApplied.logc-idx" -ErrorAction SilentlyContinue
  Remove-Item "$ccApplied.logc-pch" -Recurse -ErrorAction SilentlyContinue
}
