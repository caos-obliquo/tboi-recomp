# Handoff — tboi-recomp (2026-11-17)

## 1. Goal
Clean-room recompile of The Binding of Isaac: Repentance+ v1.9.7.17.J460
(`isaac-ng.exe`, sha256 `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`,
Steam user copy), native on Linux x86_64 + Android arm64.

## 2. Changed files
- `docs/handoff.md` — dated 2026-11-17. Mandatory next deliverable is the
  **playtesting box**, tracked as
  [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79).
- `README.md` — docs-list row for `docs/handoff.md` (on `docs/77-handoff`).
- `docs/HANDOFF.md` — §12 corrected: 9 required contexts (was a stale
  "11 incl macOS" claim; PR #10 merged 2026-10-19), release-record fix (#74)
  noted.

## 3. Verified commands and results
- `gh issue view 77` → CLOSED/COMPLETED (auto via `Closes #77`); AC re-scoped
  (owner decision, 2026-11-16). #76 MERGED 2026-11-16T04:00:13Z.
- #67 CLOSED; white-box slice done (SDL2, ≈60 Hz, no game data). NOT a playtesting box.
- Exe sha256 → `3bdfc8bae0dc7e334b76009d0ad45dfbb16ee5f00c06ffbc3a0094e34d44616b`.
- `gh pr checks 81` → 9/9 green (run 44012965729); squash-merged → `main` @ `a49d727`;
  0.1.4 → 0.1.12 (release catch-up for #74); 14 stale `release/pr-*` branches deleted.
- `gh api repos/.../required_status_checks` → 9 contexts, `strict=true`.
- Release-record workflow: Actions checkbox flipped 2026-11-17 so
  `version.yml` can open record PRs; v0.1.3 remains the latest tag/release;
  no v0.1.4+ tags cut.

## 4. Open questions
- **Mandatory: playtesting box** — definition, gating subsystems, platform(s),
  verification meaning. Owner to scope; do not infer —
  [#79](https://github.com/caos-obliquo/tboi-recomp/issues/79).
- NOT mandatory (owner, 2026-11-17): #64 audio, #66 Linux/Android epic,
  #52 LZW (behind runtime vtable `0xba05bc`), #3/#4 signatures, #35 metamethods.
- Clamp/Clamped runtime semantics; REPENTOGON `.zhl` silent on Clamp.

## 5. Exact next step
Define the playtesting box with the owner ([#79](https://github.com/caos-obliquo/tboi-recomp/issues/79)).
Do not start #64/#66/#52/#3/#4/#35 as "mandatory" — they are not.
