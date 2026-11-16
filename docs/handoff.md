# Handoff — tboi-recomp (2026-11-16)

## 1. Goal
Clean-room recompile of The Binding of Isaac: Repentance+ v1.9.7.17.J460
(`isaac-ng.exe`, sha256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`,
Steam user copy), native on Linux x86_64 + Android arm64.

## 2. Changed files
- `docs/handoff.md` — rewritten: 5-section snapshot, max 40 lines, continues
  [#77](https://github.com/caos-obliquo/tboi-recomp/issues/77).
- `README.md` — docs-list row for `docs/handoff.md` (on `docs/77-handoff`).

## 3. Verified commands and results
- `gh pr view 76` → MERGED 2026-11-16T04:00:13Z; `main` @ `7b59738`.
- `gh pr checks 63` → 9/9 pass (2026-11-15).
- `sha256sum` exe → `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`.
- `gh issue view 37` → CLOSED; Clamp/Clamped grade `rejected`.

## 4. Open questions
- Audio interface + SDL2 backend (#64); Android packaging / on-device (#66).
- LZW mode 0x01 (#52) behind runtime vtable (`0xba05bc`); Clamp/Clamped
  runtime semantics; REPENTOGON `.zhl` silent on Clamp.

## 5. Exact next step
Commit rewrite on fresh branch, PR base `main`, comment on [#77]; then pick
next open issue.
