# Working rules for this repository

These are rules, not suggestions. They exist because each one was learned from a
concrete failure in this project's own history, and the failure is recorded
next to the rule so the reasoning is auditable.

---

## 1. Answer every issue, properly, or say why not

**No issue is left silent.** When work in this repository produces a result that
bears on an open issue, that issue gets a reply in the same turn — not
deferred to "later", not silently ignored because the work was nominally about
something else.

A reply must do one of three things, and must say which:

- **Resolved**, with the evidence inline: the measurement, the commit, the
  count, and what it does *not* establish.
- **Partially advanced**, stating exactly what moved and what did not, so the
  next person does not have to rediscover the boundary.
- **Blocked**, naming the specific blocker and the probe that would clear it.

"Still working on it" is not one of the three. Neither is a bare status ping.

When a result *partially* closes an issue, say partial. Do not close an issue
whose acceptance criteria are not met; a half-truth in the issue tracker is
worse than an open issue, because it removes the reminder that work remains.

If new work is discovered that no issue covers, open one. Do not let findings
live only in a commit message or a private scratch file.

---

## 2. Validate every claim before it is written down

No number reaches a document, an issue, or a commit message without a command
that produced it, and that command must have been run in this session.

- Measure, never estimate. If a count is reported, the tool that counted it is
  named and its output is reproducible.
- Never hand-compute an address, an offset, or a section boundary. Addition
  mistakes here are silent and produce plausible-looking wrong answers — this
  has happened repeatedly, including a run that reported **zero** findings while
  exiting successfully because a window was placed below the real image.
- Every search that can return nothing carries a **positive control**: a known
  result that must appear. A search returning empty is only meaningful next to a
  search that was proven capable of finding something.
- A tool that hardens a code path must be shown to produce **identical output**
  before and after. If the outputs differ, the change is a regression, not a
  fix.
- Self-reporting success is not evidence. If a control cannot fail, it is not a
  control.

**Retraction is a first-class outcome.** When a claim is disproved, correct the
document that made it, in a commit whose message explains the disproof, and
leave the retraction visible. Do not quietly delete the wrong number and move
on: the retraction is what stops the next person from re-deriving the same
mistake.

---

## 3. Cross-document every finding

A finding lives in exactly one place and is referenced from the others. It is
never written twice in two slightly different words.

When a result lands:

- `docs/binding-surface-findings.md` — measured addresses, counts, and the
  reasoning behind a method. **This is authoritative for addresses.** If an
  address appears anywhere else, it must match this file.
- `docs/HANDOFF.md` — what is in flight, what is blocked, what is a known dead
  end. A dead end recorded here saves the next session from re-mining it.
- `docs/arch000-format.md` — container format claims only.
- `README.md` — progress and scope for humans. Never a place for a raw address.
- `tools/README.md` — what each tool does, and what it does *not* establish.
- GitHub issues — the durable answer, with the evidence inline.

If one of these is stale after a change, the change is not finished. A finding
that exists only in a commit message is lost.

Cross-check in both directions. A new measurement that contradicts a documented
number updates the document *and* the issue that quoted it. A documented dead
end that turns out to be wrong gets corrected before anyone spends time on it.

---

## 4. Commits

- Conventional prefix, imperative summary: `feat(scope):`, `fix(tools):`,
  `docs(findings):`, `chore(dependabot):`.
- The body explains **why**, and for a fix it states what the old code did
  instead, ideally demonstrated rather than asserted.
- Commit the tool, not its output. Generated evidence files live outside the
  repository.
- Never suppress a type error, never weaken a test to make a suite green, and
  never leave a failing check unexplained. A pre-existing failure is reported as
  pre-existing, not silently fixed and not silently ignored.