param([Parameter(Mandatory = $true)][string]$QtRoot)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build build-windows --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
ctest --test-dir build-windows -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
$task_install_root = Join-Path (Get-Location).Path "release/windows"
cmake --install build-windows --config Release --prefix "$task_install_root"
if ($LASTEXITCODE -ne 0) { throw "Deployment failed" }
Write-Host "Готово: release/windows/bin/optical_cad.exe"
