param([string]$Mode = "release")

$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vswhereArgs = @(
    "-latest"
    "-products", "*"
    "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    "-format", "json"
)
$vsInstanceJson = & $vswhere @vswhereArgs

if ($LASTEXITCODE -ne 0) {
    throw "vswhere.exe failed with exit code $LASTEXITCODE."
}

$vsInstance = $vsInstanceJson | ConvertFrom-Json | Select-Object -First 1
if (-not $vsInstance) {
    throw "No Visual Studio instance with the C++ x64/x86 tools was found."
}

$devShellModule = Join-Path $vsInstance.installationPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (-not (Test-Path $devShellModule)) {
    throw "Visual Studio developer shell module not found: $devShellModule"
}

Import-Module $devShellModule
Enter-VsDevShell $vsInstance.instanceId -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"

& (Join-Path $PSScriptRoot "..\build.bat") $Mode
exit $LASTEXITCODE
