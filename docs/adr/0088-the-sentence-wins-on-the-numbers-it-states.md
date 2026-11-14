# The sentence wins on the numbers it states

Ask's contract is that the built-in model writes the **Intent** and never the results
([ADR 0053](./0053-the-assistant-writes-filters.md)), and the App still writes the sentence the
person reads ([ADR 0086](./0086-the-assistant-is-a-conversation.md)). The App already corrects the
model on the way: it rewrites a key spelling the device cannot read, drops a purpose's invented
filters, and drops a query that is not a name the sentence used. Those are corrections to a
visible filter, not a second author.

The built-in model is weakest at the things a sentence states most plainly: a count, a listening
session's length, a track's length, a tempo, an offset, a key. It is also the part a person can
check in the Intent and edit. A count of 50 for "10 songs" is exactly the kind of miss
[ADR 0053](./0053-the-assistant-writes-filters.md) kept the Intent visible for; the App can read
the 10 from the sentence without asking the model again.

## Decision

- **A pure extractor reads the sentence for the fields a regex can settle:** a count ("10 songs",
  "the 20 most danceable tracks"), a listening session ("about an hour", at 15 tracks an hour), a
  track length ("under 3 minutes"), a tempo ("140 to 120 BPM", "under 100 BPM"), an offset ("skip
  40") and a key ("in F# minor", "8A").
- **A field it finds replaces the model's field.** A measurement pair is one field: a stated
  maximum clears the model's invented minimum, and a stated minimum clears its maximum. A field it
  does not find keeps the model's value. A key the model already has under another spelling is left
  alone; two spellings of one key are not a conflict.
- **It runs only over a runnable Intent.** "add 5 songs to the playlist" states a count and is not
  a search; it stays a refusal. The extractor never turns a failure into a search.
- **It never writes a name, a mood or a genre.** Those are the fuzzy semantics the model is for;
  a regex over them would be the confident paragraph
  [ADR 0050](./0050-the-assistant-waits-for-a-secure-origin.md) refused, in filter form.
- **The model still writes the Intent and never the results**, and the Intent stays visible. The
  extractor is reported beside the model's answer in the evaluation (`effective`), like the key
  spelling and the purpose normalizer; it is not model quality.

## Considered options

- **Leave the numbers to the model.** Rejected: the model is weakest there, the corpus states the
  values explicitly, and a filter is visible and correctable, so the App should correct the
  visible thing.
- **Put the rule in the prompt.** Rejected on the record: prompt changes have been measured and
  lost once, and the sentences are already in the prompt's examples. The App can read them
  deterministically, with no second model call and no prompt measurement.
- **Rescue a refusal with the extractor.** Rejected: a command that states a count is not a
  search. The extractor applies to an Intent that already parsed, so "add 5 songs" keeps its
  refusal.
- **Extract names, moods or genres too.** Rejected: those are the parts the model is for, and a
  wrong name is not a number a person can see and correct the same way.

## Consequences

- **The Intent can contain a value the model did not write.** The reading line and the Filters
  sheet show it, and the search still runs only what the Intent says.
- **A unitless "around 140" is left to the model.** Without "bpm" or "tempo" it can be a count;
  the extractor requires the unit rather than guessing. A stated "around 140 bpm" becomes a
  135–145 window, and a bare "120 bpm" becomes 120–120.
- **The listening session stays 15 tracks an hour, capped at a page**, as the prompt already says;
  a track's length and a session's length never share a key.
- **A key is taken only if the device's grammar reads it**, and a readable spelling is kept as the
  sentence wrote it. An unreadable spelling never replaces a valid key.
- **The evaluation's model verdict is unchanged.** The extractor is an app-side step, reported
  through `effective`; the per-field score shows which fields it moved.
- **k7-app §7 P2 gains the sentence-wins rule.** The prompt, schema and examples do not change.
