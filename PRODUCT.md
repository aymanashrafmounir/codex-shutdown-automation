# Codex Shutdown Automation

<!-- impeccable:product-schema 1 -->

## Platform

Windows desktop

## Stack

User-selected C++ and Qt desktop application. C++17, Qt 6 Widgets, Qt SQL with read-only SQLite, CMake, and a per-user PowerShell installer. Distribute dynamically linked Qt runtime libraries with the executable.

## Users

Codex desktop users running multiple local chats who want their Windows computer to shut down once the current batch of work finishes.

## Product Purpose

Observe all local Codex chats, require explicit one-time arming, and shut down only after work completes and a cancellable settling period expires.

## Operating Context

The native monitor starts in the system tray at Windows sign-in. A Desktop shortcut shows its Qt window. It remains disarmed after installation, restart, cancellation, and a shutdown attempt. It never resumes an old authorization.

## Capabilities and Constraints

- Show current chats and blockers, one-time Enable and Cancel controls, settling and countdown durations, and a local decision history.
- Save settings and shutdown decisions before requesting Windows shutdown.
- Codex projects stay outside this repository. Do not stage, commit, upload, or otherwise change their source to save work.
- Monitor databases read-only. Missing, incompatible, unreadable, or uncertain state blocks shutdown.
- No telemetry, cloud API keys, arbitrary shell actions, or forced application closure.
- Public reusable repository requested by the user. Publication depends on GitHub authentication.
- Language preference has been asked; English plus Arabic is the proposed default, not an established standing preference.

## Brand Commitments

Codex Shutdown Automation. Clear utility language and a primary state that makes disarmed versus armed unmistakable.

## Product Principles

- Explicit authorization is temporary.
- Uncertainty keeps the computer running.
- A chat awaiting the user is not completed work.
- Preserve saved files and retain an audit record of each decision.
