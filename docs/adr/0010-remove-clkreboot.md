# ADR 0010 — Remove the backward clock reset command

Status: accepted (recorded 2026-10-28)

## Context

`clkreboot` set the clock to a fixed past timestamp and rebooted. It was an
exception to the forward-only manual clock rule and was exposed by the shared
CLI to server roles. Companion CLI already refused it.

## Decision

Remove `clkreboot` from the shared CLI and all role help. Manual clock changes
remain forward-only; a validated GPS fix can correct a clock that is ahead.

## Consequences

A GPS-less node whose clock is ahead has no CLI command to move it backward.
The command removal changes no stored settings or mesh protocol data.
