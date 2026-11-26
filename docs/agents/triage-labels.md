# Triage Labels

The skills speak in terms of five canonical triage roles. This repo has no label field on its
tracker, so a role is carried by the Kanvas board's state plus a note on the task.

| Role | Carried by |
| --- | --- |
| `needs-triage` | a task still `proposed` (awaiting the human's approval) |
| `needs-info` | `kanvas ask <ID> "…"` — the task waits for the human's reply, marked on the board |
| `ready-for-agent` | `todo`, approved and unblocked (`kanvas ready` lists it) |
| `ready-for-human` | `review` — finished work waiting on a person |
| `wontfix` | the proposal is `withdraw`n, or the task is closed as accepted work that will not be done |

When a skill mentions a role, use the operation in its row rather than inventing a label. If
this repo ever gets a real label surface, replace the right-hand column with the label strings.
