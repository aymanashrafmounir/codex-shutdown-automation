param([string]$PackageDirectory, [switch]$NoOpen)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Windows is required.' }
if (-not [Environment]::Is64BitOperatingSystem) { throw '64-bit Windows is required.' }
if (-not $PackageDirectory) {
    $releaseApp = Join-Path (Split-Path $PSScriptRoot) 'app'
    if (Test-Path -LiteralPath (Join-Path $releaseApp 'CodexShutdownAutomation.exe')) { $PackageDirectory = $releaseApp }
    else { $PackageDirectory = Join-Path (Split-Path $PSScriptRoot) 'dist\app' }
}
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$sourceExecutable = Join-Path $package 'CodexShutdownAutomation.exe'
if (-not (Test-Path -LiteralPath $sourceExecutable)) { throw 'Build or extract the native release package first.' }
$applicationRoot = Join-Path $env:LOCALAPPDATA 'CodexShutdownAutomation'
$installedApp = Join-Path $applicationRoot 'app-0.2.1'
$executable = Join-Path $installedApp 'CodexShutdownAutomation.exe'
# The verified package can contact a source/build instance even on first installation.
$previous = Start-Process -FilePath $sourceExecutable -ArgumentList '--quit' -WindowStyle Hidden -Wait -PassThru
if ($previous.ExitCode -ne 0) { throw 'The existing monitor refused to quit. Quit it from its tray menu; installation was not changed.' }
$currentSessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
for ($exitAttempt = 0; $exitAttempt -lt 20; $exitAttempt++) {
    $existing = Get-Process -Name CodexShutdownAutomation -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $currentSessionId }
    if (-not $existing) { break }
    Start-Sleep -Milliseconds 250
}
if ($existing) { throw 'Quit existing native monitors from their tray menus before installing; no shortcuts were changed.' }
New-Item -ItemType Directory -Path $installedApp -Force | Out-Null
Copy-Item -LiteralPath $sourceExecutable -Destination $installedApp -Force
foreach ($item in Get-ChildItem -LiteralPath $package) {
    if ($item.Name -ne 'CodexShutdownAutomation.exe') { Copy-Item -LiteralPath $item.FullName -Destination $installedApp -Recurse -Force }
}
$shell = New-Object -ComObject WScript.Shell
function Write-UtilityShortcut([string]$Path, [string]$Arguments) {
    $shortcut = $shell.CreateShortcut($Path)
    $shortcut.TargetPath = $executable
    $shortcut.Arguments = $Arguments
    $shortcut.WorkingDirectory = $installedApp
    $shortcut.Description = 'Monitor all local Codex chats. Shutdown starts OFF.'
    $shortcut.IconLocation = "$executable,0"
    $shortcut.Save()
}
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Codex Shutdown Automation.lnk'
$startup = Join-Path ([Environment]::GetFolderPath('Startup')) 'Codex Shutdown Automation.lnk'
Write-UtilityShortcut $desktop ''
Write-UtilityShortcut $startup '--background'
@{ version = '0.2.1'; installedAt = [DateTime]::UtcNow.ToString('o'); executable = $executable; desktopShortcutPath = $desktop; startupShortcutPath = $startup } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $applicationRoot 'installation.json') -Encoding UTF8
Start-Process -FilePath $executable -ArgumentList '--background' -WorkingDirectory $installedApp -WindowStyle Hidden
if (-not $NoOpen) { Start-Sleep -Milliseconds 750; Start-Process -FilePath $executable -WorkingDirectory $installedApp -WindowStyle Hidden }
Write-Output "Installed native C++/Qt application. Shutdown is OFF. Desktop: $desktop"
