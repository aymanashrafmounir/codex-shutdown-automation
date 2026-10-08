param([string]$QtRoot, [string]$CompilerRoot, [string]$CMakePath, [string]$NinjaPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $QtRoot) { $QtRoot = Join-Path $root '.local\tooling\Qt\6.8.3\mingw_64' }
if (-not $CompilerRoot) { $CompilerRoot = Join-Path $root '.local\tooling\Qt\Tools\mingw1310_64' }
if (-not $CMakePath) {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { $CMakePath = $command.Source } else { $CMakePath = Join-Path $root '.local\tooling\venv\Scripts\cmake.exe' }
}
if (-not $NinjaPath) {
    $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($command) { $NinjaPath = $command.Source } else { $NinjaPath = Join-Path $root '.local\tooling\venv\Scripts\ninja.exe' }
}
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$CompilerRoot = (Resolve-Path -LiteralPath $CompilerRoot).Path
$env:PATH = "$(Join-Path $CompilerRoot 'bin');$(Join-Path $QtRoot 'bin');$env:PATH"
$build = Join-Path $root 'build'
& $CMakePath -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot" `
    "-DCMAKE_CXX_COMPILER=$(Join-Path $CompilerRoot 'bin\g++.exe')" "-DCMAKE_MAKE_PROGRAM=$NinjaPath"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $CMakePath --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
$ctest = Join-Path (Split-Path $CMakePath) 'ctest.exe'
& $ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
$package = Join-Path $root 'dist\app'
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'CodexShutdownAutomation.exe') -Destination $package -Force
& (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --no-opengl-sw --compiler-runtime --dir $package `
    (Join-Path $package 'CodexShutdownAutomation.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
Copy-Item -LiteralPath (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $package -Force
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $package -Recurse -Force
$releaseScripts = Join-Path $root 'dist\scripts'
New-Item -ItemType Directory -Path $releaseScripts -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'scripts\Install.ps1'), (Join-Path $root 'scripts\Uninstall.ps1') -Destination $releaseScripts -Force
$obsoleteBuildScript = Join-Path $releaseScripts 'Build.ps1'
if (Test-Path -LiteralPath $obsoleteBuildScript) { Remove-Item -LiteralPath $obsoleteBuildScript }
Write-Output "Native package ready: $package"
