# 05 — The Menu's Player list and its progress

**What to build:** `Settings → Player` becomes the list of `player-update.md` U26: the transport row
it has today, and `Update`, whose value is the truth at the moment it is drawn — no image, no
player, up to date, which way the version goes, how far the act has got, and how it ended.

**Blocked by:** 04 — The K7 probes and hands the image over.

**Status:** built; host tier and the K7's firmware build, nothing on a panel. `t18_menu` covers the
list's every state (699 checks with the model's other cases): the Settings row opens it with a
chevron, Transport returns the transport action and follows the setting, and Update reads No
image / Transport is HTTP / Searching... / Updating 37% / Updating 2 of 3 / updated 2 / Up to date /
No player / `0.6.2 -> 0.7.0` / `0.8.0 <- 0.7.0`, inert in all but the last kind. The view probes when
the list opens, cancels when it closes, and the K7 builds at 2,132,240 B with 49 % free. The panel
pass is PU-06.

- [x] `Settings → Player` opens a list of two rows: `Transport` (`ESP-NOW` / `HTTP`, behaving as it
      does today) and `Update`.
- [x] `Update` reads **No image** with no partition, **No player** when nothing answered the probe,
      **Up to date** when the answers are all on the image, the direction `0.6.2 → 0.7.0` or
      `0.6.2 ← 0.7.0` when it is actionable, and the act's progress and outcome while and after it
      runs.
- [x] Opening the list raises the radio and probes; closing it drops the radio again when the
      transport is HTTP.
- [x] The row is inert unless the direction is actionable: a player on the image, no image and no
      player are states, not failures, and the row says which.
- [x] The Menu's model gains the list without breaking the transport row's own behaviour, and the
      host menu tests cover every state the row can be in.

Spec: `docs/specs/menu.md`; `docs/specs/player-update.md` U26; `docs/specs/espnow-stream.md` E9.
