# The device warning is the one extra hue

The look has an orange primary and a neutral secondary ([ADR 0081](./0081-the-look-has-a-primary-and-a-secondary.md)).
The device link was a word in the neutral ink — "device unreachable" in the stage header, and a
gray `MonitorX` on phones. Beside the card switcher it read as incidental text, and on a phone
the one signal that the player is off was easy to miss. The device being unreachable is the one
state a person has to see before touching the card switcher; it is not a data error.

## Decision

- **Add `--k7-alert: #F2C94C` and `--color-alert`.** The Badge `alert` variant is a yellow
  outline with `TriangleAlert` and the word "device unreachable".
- **It is used only by the device-unreachable badge in the stage header.** `--warning` stays the
  neutral state word; measured values, data errors and every other state keep the
  shape-and-words rule.
- **Desktop carries the icon and the word; phones carry the icon with an accessible name**, and
  the badge is a `role="status"` with the label "device unreachable".
- **The contrast on the page is 11.2:1.** The icon and the word carry the state, not colour
  alone.
- `k7-look.md`'s hard constraints, token table, Badge row and K22 name the exception.

## Considered options

- **Keep the neutral word.** It disappears beside the switcher, at exactly the moment the player
  is unreachable.
- **Use the accent.** Orange means primary/current; a warning in orange would read as the primary
  action.
- **Restore the four state hues.** Colour would carry meaning again and the rest of the look
  would need a new hierarchy; one badge does not justify it.

## Consequences

- There is one yellow in the chrome; every other state remains neutral.
- The header warning is visible and announced, and the card switcher still names the card.
- A later warning that is not the device link needs its own decision; this is not a state
  palette.
- The measured values that use `text-warning` stay neutral; the alert token is separate on
  purpose.

## Supersedes

[ADR 0081](./0081-the-look-has-a-primary-and-a-secondary.md)'s no-second-hue rule, for the
device-unreachable badge only.
