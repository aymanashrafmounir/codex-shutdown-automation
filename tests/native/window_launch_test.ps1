param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporary = Join-Path $temporaryRoot ("csa-window-test-" + [Guid]::NewGuid().ToString('N'))
$originalLocalData = $env:LOCALAPPDATA
$originalPlatform = $env:QT_QPA_PLATFORM
$monitor = $null
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    # A separate simulation profile cannot contact or arm the user's live monitor.
    $env:LOCALAPPDATA = $temporary
    $env:QT_QPA_PLATFORM = 'windows'
    $monitor = Start-Process -FilePath $Executable -ArgumentList '--simulation','--background' -WindowStyle Hidden -PassThru
    $statusPath = Join-Path $temporary 'status.json'
    $errorPath = Join-Path $temporary 'error.txt'
    $ready = $false
    for ($attempt = 0; $attempt -lt 12; $attempt++) {
        $probe = Start-Process -FilePath $Executable -ArgumentList '--simulation','--status' -WindowStyle Hidden `
            -Wait -PassThru -RedirectStandardOutput $statusPath -RedirectStandardError $errorPath
        if ($probe.ExitCode -eq 0) { $ready = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw 'Simulation did not become ready.' }
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
    if (-not $status.simulation -or $status.armed -or $status.phase -ne 'disarmed') {
        throw 'Window regression must run only in disarmed simulation.'
    }
    # This is the real second-launch IPC path, after a hidden initial launch.
    $show = Start-Process -FilePath $Executable -ArgumentList '--simulation' -WindowStyle Hidden `
        -Wait -PassThru -RedirectStandardOutput (Join-Path $temporary 'show.json') -RedirectStandardError $errorPath
    if ($show.ExitCode -ne 0) { throw 'Second launch did not acknowledge show.' }
    $visible = $false
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $monitor.Refresh()
        # .NET exposes a MainWindowHandle only for a visible, unowned native window.
        if ($monitor.MainWindowHandle -ne 0 -and $monitor.MainWindowTitle -eq 'Codex Shutdown Automation') {
            $visible = $true; break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $visible) { throw 'Show was acknowledged but the native window stayed hidden.' }
    Write-Output 'Hidden launch -> second-launch IPC -> visible native window passed. Shutdown remained OFF.'
} finally {
    if ($monitor -and -not $monitor.HasExited) {
        $quit = Start-Process -FilePath $Executable -ArgumentList '--simulation','--quit' -WindowStyle Hidden -Wait -PassThru
        if (-not $monitor.WaitForExit(5000)) { $monitor.Kill(); $monitor.WaitForExit() }
    }
    $env:LOCALAPPDATA = $originalLocalData
    $env:QT_QPA_PLATFORM = $originalPlatform
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (-not $resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^csa-window-test-[a-f0-9]{32}$') {
        throw 'Refusing cleanup outside the dedicated test directory.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
