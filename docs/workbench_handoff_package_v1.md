# Workbench Handoff Package v1

`workbench_handoff_package` is a stable, read-only snapshot artifact for controlled handoff between the Workbench UI, a human reviewer, and a downstream agent prompt. It is exported from `POST /api/workbench/snapshot` as `handoff_package`.

## Stability

- `schema.name`: `workbench_handoff_package`
- `schema.version`: `1`
- `schema.stability`: `stable`
- The package is observational only. It must not start agents, execute commands, mutate repositories, or override gate decisions.

## Top-level fields

- `success`: package generation status.
- `read_only`: always true for this package.
- `package_version`: numeric package version, currently `1`.
- `schema`: schema metadata.
- `title`: human-readable package title.
- `objective`: suggested handoff objective from `agent_context`.
- `selected_path`: selected workbench path when available.
- `gate`: compact gate decision, blockers, and next steps.
- `verification`: verification commands and last run evidence.
- `failure_context`: present only when the last verification/failure state failed.
- `review_context`: review checklist and focus summary.
- `timeline_context`: bounded timeline summary.
- `agent_context`: agent-oriented context and constraints.
- `change_summary`: compact git/selected-file summary.
- `brief`: short summary suitable for UI preview.
- `limits`: array limits applied to bounded fields.
- `truncation`: booleans indicating whether bounded fields were truncated.

## Gate semantics

Consumers must respect `gate.decision`:

- `pass`: handoff/final review may proceed.
- `review`: handoff needs explicit human judgment.
- `blocked`: do not treat the package as complete; fix blockers first.

`gate.handoff_allowed` is the direct boolean derived from the decision and blockers.

## Bounded fields

To keep copy/download payloads predictable, these fields are bounded:

| Field | Limit |
|---|---:|
| `verification.commands` | 8 |
| `timeline_context.entries` | 20 |
| `review_context.checklist` | 12 |
| `gate.blockers` | 12 |
| `gate.next_steps` | 8 |

When a limit is hit, the corresponding `truncation.*` flag is true.

## Usage boundary

The package is safe to copy, download, attach to a review, or paste into a controlled agent prompt. It is not an execution request by itself. Any command rerun must still go through the governed Test Loop / permission / audit path.
