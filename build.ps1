# easyweb build script for Windows (PowerShell)
# Builds dist\easyweb.exe and dist\webhelp.exe
#
#   powershell -ExecutionPolicy Bypass -File build.ps1
#
param(
  [string]$Compiler = "gcc",
  [string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }
$out = (Resolve-Path $OutDir).Path

$core = @(
  "src/util.c", "src/lexer.c", "src/ast.c", "src/parser.c", "src/value.c",
  "src/interp.c", "src/jsgen.c", "src/kid.c", "src/render.c", "src/http.c",
  "src/server.c", "src/pkg.c", "src/packages.c", "src/highlight.c",
  "src/json.c", "src/webhelp.c"
)

$flags = @("-O2", "-std=c11", "-Wall", "-Wextra", "-static-libgcc")
$ldflags = @("-lm")
if ($IsWindows -or $env:OS -eq "Windows_NT") { $ldflags += "-lws2_32" }

Write-Host "==> building easyweb.exe"
& $Compiler @flags "-o" "$out\easyweb.exe" @core "src/main.c" @ldflags
if ($LASTEXITCODE -ne 0) { throw "easyweb.exe build failed" }

Write-Host "==> building webhelp.exe"
& $Compiler @flags "-o" "$out\webhelp.exe" @core "src/webhelp_main.c" @ldflags
if ($LASTEXITCODE -ne 0) { throw "webhelp.exe build failed" }

Write-Host "==> done"
Get-ChildItem $out | Select-Object Name, Length | Format-Table -AutoSize
