# Ask edits the list it is beside

> **Moved, and the switch retired ([k7-look.md](../specs/k7-look.md) K21).** The list now takes the
> editor's left column and the panel with the files takes the right. The **Files / Ask** switch is
> gone as well: the card's tree is the column, and Ask is the composer at its foot, with the last
> answer drawn over the tree itself — the card's folders pruned to its rows — and **Files** in the
> column's header to give the card back. The decision below — Ask is a panel beside the list, with
> no page and no rail item — stands; what changed is its shape inside the column, and that
> `#/playlists/:id/ask` no longer names a tab.

[ADR 0086](./0086-the-assistant-is-a-conversation.md) made Ask a conversation on its own page,
the **IA Agent** page at `#/playlists/ask`, with the composer at its foot and one action at the end
of an answer: **Save playlist**, which created a new draft and opened it. That shape followed from
the page it lived on — a person arrived with a sentence and no list, so the only thing an answer
could produce was a list. It also kept
[ADR 0050](./0050-the-assistant-waits-for-a-secure-origin.md)'s rule that the model writes filters
and the App writes everything else, including the refusal: "add 5 songs to the playlist" stayed a
refusal because there was no playlist in view to add to.

The Playlists area now follows the selected card and the editor is where a list is
([ADR 0092](./0092-a-playlist-belongs-to-its-card.md)), and the owner asked for the assistant to
stand in the editor's left column beside the card's files, with no page and no rail item of its
own. Beside an open list the old refusal is no longer careful, it is absurd: the App has the
filters, has the rows the search returned, and has the list one column away.

## Decision

- **Ask is a panel in the editor's left column.** The column carries a segmented **Files / Ask**
  switch; no IA Agent page, no rail item, and `#/playlists/ask` redirects to `#/playlists`. The tab
  is a place and lives in the address: `#/playlists/:id` opens on Files, `#/playlists/:id/ask` opens
  on Ask, so a reload lands where the person was.
- **An answer's primary action adds its tracks to the open list.** They are appended at the end, in
  the order the answer shows them, repeats allowed — a playlist may play the same Track twice, as
  the editor already allows. The old save question becomes that action: *Add N tracks*.
- **Save as a new list remains, as the secondary.** It creates a draft named from the sentence, as
  `nameFor()` does today, and the answer then offers to open it — a jump out of the editor is the
  person's, not the save's. That is the "make me a whole set" path, and it needs no page of its own.
- **The model still writes filters and never results.** ADRs 0050 and 0053 stand unchanged: the
  model is asked for one object of `library_search` arguments, the App judges it, runs it, and
  writes every word the answer says. What changes is that the App may now **apply** an Intent to
  the list in view, which is an act of the App's on rows it found — the model never sees the list,
  the draft or a single row.
- **"Add five tracks to this playlist" stops being a refusal.** The sentence becomes filters like
  any other and its rows go into the open list. The rule that survives is the one that mattered: no
  sentence describes a track that does not exist.
- **One session per ask, and the transcript belongs to the visit.** [ADR 0089](./0089-the-assistant-keeps-one-base-session-and-clones-it.md)
  is unchanged: each ask stands alone, the base session is cloned per question, and the transcript
  is this tab's and is not stored. It now dies with the editor rather than with a page visit.

## Considered options

- **Keep the page.** Rejected: a tab that cannot put a row into the list beside it makes the person
  read the answer on the left and hand-copy it from the tree on the right, and it leaves the rail
  with two entries for one place.
- **Keep creating a new list from inside the editor.** Rejected: the list being edited is abandoned
  mid-sentence, so the assistant becomes a way to leave the editor — the opposite of standing beside
  it.
- **Ask finds and nothing enters the list.** Rejected: the tree already browses, and a search that
  cannot place its result is a detour.
- **Let the model see the list, or the rows, so it can write the change.** Rejected: the same rule
  as ADR 0050 and ADR 0053. A wrong filter is visible and correctable; a confident sentence about
  music that is not there is not.
- **Drop the address for the tab and keep it in local state.** Rejected: a reload would silently
  move the person out of the panel they chose.

## Consequences

- **The assistant's spec is rewritten, not just moved.** `docs/specs/k7-app.md` §7 loses the
  refusal of an instruction to change the library (P2), places the composer in the editor's column
  (P3), and turns the save question into the add action (P10). P4's "the Ask area closes" becomes
  the editor closing.
- **The editor gains the assistant's states.** The five states of the built-in model, the Filters
  sheet and the error-with-retry all have to work inside a column that also holds a file tree, and
  the column's empty state is now whichever of the two is showing.
- **The answer's row count is the thing that is tested.** The append keeps the answer's order, so
  `spreadAnswer()`'s selection becomes what a person hears; the tests pin the order and the
  repeats.
- **`web/CONTEXT.md`'s IA Agent stops being a page** and becomes the panel; **Ask** stays the
  conversation on it.

## Supersedes

None. It amends [ADR 0086](./0086-the-assistant-is-a-conversation.md): Ask is still a
conversation, the App still writes every sentence of it, and the Intent is still one control away.
What changes is where the conversation stands, and what an answer may do to the list beside it.
