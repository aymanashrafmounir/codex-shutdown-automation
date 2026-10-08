$ErrorActionPreference = 'Stop'
$root = Join-Path $env:LOCALAPPDATA 'CodexShutdownAutomation'
$executable = Join-Path $root 'app-0.2.1\CodexShutdownAutomation.exe'
if (Test-Path -LiteralPath $executable) {
    $quit = Start-Process -FilePath $executable -ArgumentList '--quit' -WindowStyle Hidden -Wait -PassThru
    if ($quit.ExitCode -ne 0) { throw 'The existing monitor refused to quit. Quit it from its tray menu; shortcuts were preserved.' }
    $currentSessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
    for ($exitAttempt = 0; $exitAttempt -lt 20; $exitAttempt++) {
        $remaining = Get-Process -Name CodexShutdownAutomation -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $currentSessionId }
        if (-not $remaining) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($remaining) { throw 'The monitor is still running. Quit it from its tray menu before uninstalling; shortcuts were preserved.' }
}
foreach ($directory in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Startup'))) {
    $shortcut = Join-Path $directory 'Codex Shutdown Automation.lnk'
    if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut -Force }
}
Write-Output 'Startup and Desktop shortcuts removed. Saved settings, history, and application files preserved.'
