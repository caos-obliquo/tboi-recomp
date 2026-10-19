# Working rules for this repository

These are rules, not suggestions. They exist because each one was learned from a
concrete failure in this project's own history, and the failure is recorded
next to the rule so the reasoning is auditable.

---

## 1. Work is issue-driven, start to finish

The GitHub issue tracker is the project memory. Work that is not anchored to an
issue cannot be resumed, reviewed, or trusted, because the next session has no
way to know what was intended or what is left.

This is a loop, and all three steps are mandatory:

### 1a. Start from an issue

Before doing any work, name the issue it serves. `gh issue list --state open` is
the starting point, not an afterthought.

**If no open issue covers the work, open one first, then do the work.** Findings
do not live only in a commit message, a branch name, or a scratch file in
`/tmp`. If the work was worth doing it is worth being findable.

A finding becomes an issue when it is any of these:

- a bug in our own tooling or process
- a constraint that will bite the next person
- an open question with a named next probe
- a claim that turned out to be wrong, with the disproof

All four happened in one session and produced [#12](https://github.com/caos-obliquo/tboi-recomp/issues/12), [#13](https://github.com/caos-obliquo/tboi-recomp/issues/13), [#14](https://github.com/caos-obliquo/tboi-recomp/issues/14), [#15](https://github.com/caos-obliquo/tboi-recomp/issues/15), [#16](https://github.com/caos-obliquo/tboi-recomp/issues/16) and [#17](https://github.com/caos-obliquo/tboi-recomp/issues/17). None of them was
planned; all of them were real.

### 1b. Anchor the work to the issue

- **One branch per issue.** The branch name references it.
- **The commit message names it.** `feat(bindings): ... [#17](https://github.com/caos-obliquo/tboi-recomp/issues/17)`. A commit whose
  subject cannot be traced to an issue cannot be traced to an intent.
- **The issue carries the acceptance criteria**, written before the work starts
  where that is possible, so "done" is checkable rather than felt.

### 1c. Report back, in the same turn

**No issue is left silent.** When work produces a result that bears on an open
issue, that issue gets a reply before the turn ends — not deferred to "later",
not skipped because the work was nominally about something else.

A reply must do one of three things, and must say which:

- **Resolved**, with the evidence inline: the measurement, the commit, the
  count, and what it does *not* establish.
- **Partially advanced**, stating exactly what moved and what did not, so the
  next person does not have to rediscover the boundary.
- **Blocked**, naming the specific blocker and the probe that would clear it.

"Still working on it" is not one of the three. Neither is a bare status ping.

Every reply states the **current** state, not the state at the last reply. If
another measurement has since superseded part of what an earlier comment claimed,
say so on the issue. An issue that carries a stale claim and no correction is
worse than one with no comment, because it looks authoritative.

### 1d. Closing

Close an issue only when **every** acceptance criterion is met and evidenced. A
result that partially closes an issue says partial, and the issue stays open: a
half-truth in the tracker is worse than an open issue, because it removes the
reminder that work remains.

When a finding *invalidates* something an issue asserted, that is a comment too,
posted in the same turn. Silently superseding a claim is how a tracker starts
lying.

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
- **Reference the issue it serves**, in the subject or the first body line:
  `feat(bindings): anchor property names to the call site [#17](https://github.com/caos-obliquo/tboi-recomp/issues/17)`. A commit
  that cannot be traced to an issue cannot be traced to an intent.
- The body explains **why**, and for a fix it states what the old code did
  instead, ideally demonstrated rather than asserted.
- Commit the tool, not its output. Generated evidence files live outside the
  repository.
- Never suppress a type error, never weaken a test to make a suite green, and
  never leave a failing check unexplained. A pre-existing failure is reported as
  pre-existing, not silently fixed and not silently ignored.
---

## 5. Progress is updated in the same commit as the work

The `## Progress` table in `README.md` is the project's honest statement of what
exists. It is updated **in the same commit or pull request as the change it
describes** — never in a follow-up, never at the end of a batch.

A row that changes meaning gets rewritten, not appended to. If work moves a
number, the old number does not survive anywhere in the repository.

Keep these distinctions explicit in the table, because collapsing them is how a
progress table starts lying:

- **located** (an address is known) versus **implemented** (behaviour exists).
  These moved by very different amounts in the same commit and must not share a
  row.
- **recovered** (the name is bound) versus **useful** (the name denotes a method
  rather than a type constructor or a property accessor).
- **named** versus **complete**. 524 of 1064 closure sites carry their name
  inline; the other 539 do not, and that is a different state, not a smaller
  version of the same one.

If a number cannot be reproduced by a command in `tools/`, it does not go in the
table. If the table would overstate the work, the fix is to make the table
smaller and more precise, never to round up.

---

## 6. Branching and repository settings

`main` is protected. Direct pushes, force pushes and branch deletion are
blocked; all 11 CI check contexts are required; history must be linear; review
threads must be resolved.

- **One branch per issue.** Prefix `feature/`, `fix/`, `add/` or `docs/`,
  and name the issue in the branch where it stays legible: `fix/17-anonymous-body-names`.
- **Never commit to `main`.** Work lands through a pull request.
- **Never weaken the required-context list to make a PR merge.** If a check
  fails, fix the cause. If a context name is wrong, correct the name — a wrong
  required context blocks every merge forever, which is worse than a red build.
- **Adding a CI job means adding its context to the protected list in the same
  change.** A job that is not required is decorative.

**Required approvals are 0, deliberately.** This repository has one
collaborator: the owner. GitHub does not count a PR author's own approval, so
requiring one approval makes every PR permanently unmergeable while the admin
escape hatch quietly defeats the rule anyway. If a second person with write
access is ever added, raise it then — and check the rule actually becomes
satisfiable before setting it.

Admin bypass (`enforce_admins: false`) is intentional. It is an escape hatch
for the owner, not an oversight; enabling it would mean locking yourself out of
your own repository with no way to recover without a second account.

---

## 7. A finding becomes an issue the moment it is discovered

Four kinds of thing must be opened as an issue immediately, in the same turn,
rather than left in a commit message or noted "for later":

1. **A bug in our own tooling.** Nine separate defects in this project's own
   probe scripts were found in one session. Each one had silently produced a
   wrong answer first.
2. **A constraint that will bite the next person.** A branch whose name no
   longer matches its content looks correct to every command that inspects
   status, and pushes the wrong work to the wrong pull request.
3. **An open question with a named next probe.** "What does this constant mean"
   is not an issue; "what does this constant mean, and here is the probe that
   would answer it" is.
4. **A claim that turned out to be wrong, with the disproof.** The 92 confirmed
   bindings, and the first assessment's malware verdict, were both retracted.
   Both retractions belong in the record permanently, because a retraction with
   no stated cause is just a quieter error.

Retraction is a first-class outcome here, equal in value to a confirmation.
Suppressing one to protect a previous claim is the worst move available.
