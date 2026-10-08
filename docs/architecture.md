# Architecture and acceptance

## Responsibilities

- `types.h` defines settings, observations, independent turn status, policy view and monitor/shutdown interfaces.
- `codex_monitor.cpp` and `monitor_queries.h` read local SQLite metadata, history, goals, queue and projection offsets with read-only connections. Connections close after every scan.
- `background_monitor.cpp` scans on a joined worker thread and copies its timestamp-preserving cache for UI/policy updates. The source is serialized; `verify(now)` reads it again, bypassing the cache before shutdown. The initial/stale/failed cache cannot authorize readiness.
- `policy.cpp` owns in-memory one-time permission and waiting, settling, countdown and disarmed transitions.
- `store.cpp` validates settings, atomically replaces them with `QSaveFile`, and flushes the decision journal with `FlushFileBuffers` on Windows. No prompt, credential or project copies.
- `controller.cpp` owns a serialized scan/decision loop, persistence, final verification and adapter invocation. It retains failure outcomes until an explicit user action.
- `windows_shutdown.cpp` executes the fixed Windows system command with `/s /t 0`, without `/f`, a shell or forced termination. The cancellable countdown belongs to the domain policy.
- `main_window.cpp` is native Qt Widgets with English/Arabic, chat table, blockers, timings, history and tray controls. No web renderer.
- `main.cpp` wires adapters and startup. `QLockFile` prevents concurrent instances in the same user profile; a user-restricted `QLocalServer` only accepts show/status/quit. No external arming endpoint exists.
- `scripts/` build/deploy/install the native executable and dynamically linked Qt runtime. A per-user Startup shortcut starts at Windows sign-in; Desktop launches show the existing window.

## Monitor contract

`scan(now)` returns an `Observation` with its original capture time, health, deterministic revision, thread metadata and blockers. `Thread.turnStatus` preserves the raw latest turn independently of `status` and `workPending` overlays. A terminal ordinal records the newest failed/interrupted turn across each thread's history; the policy compares it with the arming baseline so a quickly superseded failure still consumes permission. These markers prevent pending overlays and fast follow-up turns from hiding failed/interrupted work.

Required stores and columns:

| Store | Required table/fields |
| --- | --- |
| `state_5.sqlite` | `threads`: id, title, rollout_path, history_mode, updated_at_ms |
| `thread_history_1.sqlite` | `thread_turns`: thread_id, turn_id, status, rollout_ordinal, started_at, completed_at, rollout_end_ordinal, rollout_end_byte_offset |
| same | `thread_history_projection_state`: thread_id, next_rollout_byte_offset, next_rollout_ordinal |
| same | `thread_items`: thread_id, turn_id, item_id, rollout_ordinal, updated_at_ordinal, item_type, item_json |
| `goals_1.sqlite` | `thread_goals`: thread_id, status, updated_at_ms |
| `queue_1.sqlite` | `queued_items`: id, thread_id, updated_at_ms |

Only status/process metadata is selected from tool item JSON. Latest turns are ordered by rollout ordinal. Command sessions are grouped by thread and process ID across turns; their newest revision must explicitly record a valid exit before resolution. A process exit in another thread cannot resolve it.

Completed turns need valid completion timestamps and final ordinal/byte offsets. Projection must cover them and equal the session file size. Database `data_version` and session file size/mtime are checked before returning. Changes during scanning, missing threads/files and unknown state fail closed.

## Policy and save order

Enable requires fresh healthy pending work. The batch captures active threads and observes new turn IDs. Old unrelated failures do not authorize a batch. Running/unknown work anywhere blocks readiness. Failed/interrupted monitored work consumes permission; another explicit Enable is necessary after resolution.

Once all work finishes, an unchanged revision must survive settling and countdown. Changes reset readiness. Settings changes and cancellation consume permission. Permission is never serialized, including in crash/restart recovery.

Execution order: save settings, save checkpoint and intent, scan again, compare revision and readiness, consume permission, immediately invoke the OS adapter. No persistence or event-loop reentry occurs between final verification and invocation. Accepted/failed outcomes are recorded afterward, with honest wording. Disk failure prevents invocation; OS failure stays disarmed and remains visible.

## Acceptance and verification

1. Independent native C++/Qt repository and reusable package; no private Codex stores, transcripts or machine-specific runtime files in Git.
2. Desktop and per-user Startup shortcuts installed. Live process stays OFF, including after manual process restart.
3. All supported local chats and subagents are monitored; uncertain or unfinished work keeps the computer running.
4. Cancellation, new work, terminal failures, missing projection, schema errors, pending queue/goals/processes and persistence failures are covered with native QtTest/SQLite fixtures.
5. Native Widgets tests exercise Enable, countdown, Cancel, settings, Arabic RTL and recent history using recording adapters only. Background adapter tests cover nonblocking cache access, unchanged capture timestamps, fresh final verification and joined destruction. The Windows launch regression starts an isolated simulation with `SW_HIDE`, uses second-launch IPC, and checks that Windows exposes a visible main window; acknowledgement alone is insufficient.
6. No real shutdown is required for acceptance. Windows restart/logon behavior is configured but only verified by an actual restart when explicitly performed.
7. Independent safety review passes before publication. Runtime state and captures remain ignored.

## Limits

These Codex stores are internal, not a stable public completion API. Persisted state cannot certify unrecorded/remote activity, semantic project success, or atomically stop new work arriving after the final scan. The utility only certifies its own durable settings/journal and observed Codex projection; it cannot save arbitrary application buffers.
