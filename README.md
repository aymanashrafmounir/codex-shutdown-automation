# Codex Shutdown Automation

A native **C++17 / Qt 6 Widgets** desktop application for Windows. It watches all supported **local Codex chats**, then requests a normal shutdown after the current batch finishes. No browser, Node runtime, web server, telemetry, or cloud credentials are required.

**Shutdown starts OFF.** Enable it explicitly once per batch. Installation, process restart, cancellation, failed/interrupted monitored work, and a shutdown attempt never restore an old permission.

## Install and use

Windows 10/11 x64 is the supported target. Extract a release package, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Install.ps1
```

The installer copies the executable and Qt DLLs into your local application data, creates a Desktop shortcut, and launches the monitor in your Windows session. The Startup shortcut starts it at **Windows sign-in**, with shutdown OFF. It requires no administrator account. A logon session is required; this is a tray application.

1. Start work in Codex, then open **Codex Shutdown Automation** from Desktop or its tray icon.
2. Check the work table and monitoring blockers. Choose the settling and countdown durations (60–3600 seconds each); save changes before enabling.
3. Select **Enable once**. All pending local chats are tracked, including new work that appears during this batch.
4. After work completes, the stable-state interval runs, followed by a cancellable countdown. New activity or uncertainty resets readiness.
5. Select **Cancel shutdown** from the window or tray to keep the computer on. Closing the window keeps the monitor in the tray; **Quit monitor** disarms and exits.

English and Arabic interfaces are available. Changing settings cancels the current authorization.

## What is saved

Before requesting shutdown, the utility flushes its settings and decision journal to disk, verifies Codex's local history projection has caught up with its persisted session files, and scans again immediately before the OS call. History records the intent and whether Windows accepted the command. Acceptance is not proof the machine powered off.

**It cannot save unsaved buffers in arbitrary editors.** It never commits projects, changes their files, or forces applications closed. Windows or another application may block the normal shutdown to handle unsaved work.

Private state stays under `%LOCALAPPDATA%\CodexShutdownAutomation\state`: `settings.json` and `decisions.jsonl`. Enable permission exists only in memory. Chat titles appear locally in the work table and are not added to the decision journal.

## Monitoring coverage and limitations

The monitor reads local Codex SQLite stores in read-only mode: `state_5.sqlite`, `thread_history_1.sqlite`, `goals_1.sqlite`, and `queue_1.sqlite` under `CODEX_HOME` or `%USERPROFILE%\.codex`. `CODEX_HOME`, when set, must be an absolute path. The currently supported internal schemas are described in [architecture](docs/architecture.md) and covered by SQLite fixtures.

Missing/unreadable stores, schema drift, incomplete completion metadata, unknown states, history projection lag, pending tool sessions, queued work, and unfinished goals prevent shutdown. Failed/interrupted turns in the monitored batch consume permission even if more work remains queued. A newer turn supersedes an older orphan turn, while unresolved background processes are checked across turns.

This is a conservative persisted-state reader. It does not observe cloud-only chats, remote computers, other Windows users, or detached work that Codex does not record. It cannot atomically prevent new work after its final scan, or infer that a completed response means your project's goals succeeded. Codex schema changes may require an adapter update; unsupported state keeps the computer on.

## Build from source

Install Qt **6.8.3 MinGW x64**, the compatible **MinGW 13.1** compiler, CMake 3.21+, and Ninja. Then:

```powershell
.\scripts\Build.ps1 -QtRoot C:\Qt\6.8.3\mingw_64 -CompilerRoot C:\Qt\Tools\mingw1310_64
```

The script builds the application, runs native policy/controller/SQLite/UI tests and a Windows hidden-launch regression, and uses `windeployqt` to produce `dist\app`. Supply `-CMakePath` and `-NinjaPath` if these tools are not on PATH. Compatible Qt 6.8+ SDKs can also be selected; distributed packages must include their matching license notices.

Open `CMakeLists.txt` in Qt Creator for development. Production source is under `src/native`; domain rules, orchestration, persistence, monitor and OS adapters, and Widgets UI have separate responsibilities. Periodic SQLite reads run on a background thread so the window remains responsive; the shutdown gate performs an uncached verification.

## Safe verification

Tests link recording shutdown adapters, including background cache/freshness checks. No test invokes `shutdown.exe` or arms the live monitor. The application also offers a separate simulation:

```powershell
.\CodexShutdownAutomation.exe --simulation
```

Synthetic chats finish after 90/180 seconds. Simulation writes separate state, visibly labels itself, and substitutes a recording adapter for Windows shutdown. There is no CLI or IPC command to enable shutdown.

```powershell
.\CodexShutdownAutomation.exe --status
.\CodexShutdownAutomation.exe --quit
```

These local commands only report compact status or disarm/quit the current user's instance. A second normal launch shows the existing window. A live instance monitoring another `CODEX_HOME` rejects the connection until you quit it.

## Remove startup

```powershell
.\scripts\Uninstall.ps1
```

This quits the native utility and removes its Desktop and Startup shortcuts. Application files, saved settings and history are preserved.

The application's source is MIT licensed. Qt is dynamically linked and distributed under its LGPL terms; see [third-party notices](THIRD_PARTY_NOTICES.md). This independent utility is not an OpenAI product.
