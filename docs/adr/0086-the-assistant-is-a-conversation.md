# The assistant is a conversation, and the App says what it found

Ask was built as a pipeline made visible: a prompt card, an **Intent** panel of rows and controls,
a **Run search** button, then a **Results** card. That shape came from the rule
[ADR 0050](./0050-the-assistant-waits-for-a-secure-origin.md) settled and
[ADR 0053](./0053-the-assistant-writes-filters.md) adopted — the model writes the filters and
never the results, so a wrong filter should be visible and correctable before anything runs. The
panel was the mitigation, and it worked: the twelve-sentence spike measured ten right, and the two
misses could be edited.

But it made the person read the machine's vocabulary before they could hear any music. Ask is for
"a late-night 8A set", not for `bpm_min: 124`; the form was the model's working, not the answer.
The ask now is a conversation: the sentence at the foot of the page, the answer as a message, and a
question about saving the set.

The constraint that built the panel has not gone away. The built-in model still returns filters
only, is still the model that measured 10/12, still over-fills a mood ask and still invents a
`query`. Hiding the Intent without a replacement would make those misses invisible, and a machine
without the built-in model would lose Ask entirely, contradicting
[ADR 0053](./0053-the-assistant-writes-filters.md)'s degrade to the form.

## Decision

- **Ask is a conversation on the IA Agent page.** The composer sits at the foot; a sentence becomes
  a message from the person, and the answer is a message from **Ask** with the tracks it found.
  There is no Intent panel on the page and no separate **Run search**.
- **The App writes every sentence Ask says.** The count, the reading of the filters, the source,
  the question about saving and the confirmation are computed from the Intent that ran and the
  search that answered. The model is still asked for one JSON object of filters, never for prose.
  A sentence in this panel can therefore describe only what the library returned.
- **The reading is visible, and the Intent is one control away.** The answer says what was
  understood ("Found 10 tracks — techno, 124–132 BPM") and which library answered. **Filters**
  opens the exact `library_search` arguments in a sheet, seeded with that Intent, and runs them.
  The same sheet is the fallback when the built-in model is unavailable, so ADR 0053's degrade
  survives without the form being the page.
- **Saving is a draft, and the card is the scope.** When the answer came from a card this browser
  holds — the Last image, or the player's card whose mark is this browser's scope — Ask asks
  **Save these N tracks as a playlist on this card?** Saving calls the existing draft store
  ([ADR 0051](./0051-a-playlist-lives-in-the-snapshot.md)); it writes no card and no player. A
  player's card that is not this browser's scope cannot be authored for, and that answer says so
  rather than offering a button that cannot work.
- **One session per ask, and the transcript is not context.** The one-shot session of
  [ADR 0084](./0084-the-assistant-is-the-browsers-built-in-model.md) stays. Each message stands
  alone; the previous sentence and its Intent are not sent back to the model. The transcript lives
  in this tab, is not stored, and a reload loses it; drafts survive.
- **The result is a preview.** Four tracks are shown, with **Show all N tracks** to expand. This
  keeps the assistant's question, and the button that answers it, in the same view as the answer.

## Considered options

- **Let the model write the reply.** Rejected: it never sees a track, so its prose could only
  describe music that may not exist — exactly what
  [ADR 0050](./0050-the-assistant-waits-for-a-secure-origin.md) refused — and a second measured
  prompt would have to cover copy no one can verify. The App's deterministic sentence is honest
  and testable.
- **Keep the Intent panel above the conversation.** Rejected: two ways to ask in one page, and the
  form still in the way of the music. The sheet keeps the correction without the standing cost.
- **Remove the Intent entirely and rely on rephrasing.** Rejected: the measured misses are the
  reason the Intent exists; a person cannot edit a number they cannot see, and a machine without
  the built-in model would have no Ask. The reading line and **Filters** are the smaller version
  of the same mitigation.
- **Keep the previous Intent in the next ask (multi-turn).** Rejected for now: it changes the
  one-shot prompt that measured 10/12, and a follow-up's quality would need its own measurement
  before it ships. The page does not pretend to remember.
- **Offer Save for every source, including a player's card in another scope.** Rejected: that card
  is not a directory this browser can write, and a draft under another card's scope would name
  rows that card does not have. The read-only sentence is the honest action.

## Consequences

- **The measured prompt is unchanged.** The 10/12, the rejected tightened prompt and the two known
  misses still describe what ships; this change adds no prompt text.
- **A misparse is found by reading the answer or hearing the result**, then fixed by asking again
  or opening **Filters**. That is slightly later than reading a panel before running, and it is
  the cost of putting the music first.
- **The conversation has no memory.** "Make it faster" after a set is a new ask, not a refinement.
  Support for that is a new session shape and a new measurement, not a UI change.
- **The transcript is ephemeral.** A reload clears it; the drafts, which are the work worth
  keeping, do not.
- **A source that cannot hold a playlist says so.** A player's card in another scope, or a card
  whose scope has changed under an old answer, gets a sentence and no button; the answer is not
  silently saved somewhere else.
- **More of the page is the App's, and less is the model's.** That is deliberate: the model's role
  is narrower than the conversation makes it feel, and the code says so where the copy is built
  (`web/src/playlists/chat.ts`).

## Supersedes

- **[ADR 0053](./0053-the-assistant-writes-filters.md)'s UI rule**, the Intent "shown on screen and
  editable before it is run", is replaced by the reading line plus the **Filters** sheet. Its shape
  rule — the model writes filters, never results — stands, as does its degrade to the filter form,
  now the sheet.
- **[ADR 0084](./0084-the-assistant-is-the-browsers-built-in-model.md)'s provider, setup and
  one-session decisions stand.** The model is still Chrome's built-in model, Ask still owns the
  setup, and the session is still one prompt long.
