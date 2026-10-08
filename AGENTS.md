# Engineering boundaries

This is an independent Windows utility, not part of Helixtra.

- Keep UI, shutdown policy, orchestration, persistence, and operating-system adapters separate.
- Read Codex stores only in read-only mode. Never inspect credentials or transcript contents for publication.
- Installation and every process restart must remain disarmed. Arming is never persisted.
- Fail closed on unknown state, corrupt stores, incomplete turns, active goals, pending work, or monitor failure.
- Windows shutdown must never force applications closed. Tests use a recording adapter, never a real shutdown.
- Use native C++17 and Qt Widgets. No browser UI, WebEngine, HTTP server, or Node runtime.
- Private local IPC may only show, report compact status, or quit; arming requires a deliberate native UI action.
- Use Qt modules and cohesive files under 350 lines. Do not split files artificially.
- Run focused tests and an independent review before publishing. Keep private runtime data and machine paths out of Git.
