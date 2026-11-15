# A genre the sentence names wins like its numbers

[ADR 0088](./0088-the-sentence-wins-on-the-numbers-it-states.md) gave the sentence reader the
numbers a sentence states and left names, moods and genres to the model: a regex over fuzzy
semantics would be the confident paragraph
[ADR 0050](./0050-the-assistant-waits-for-a-secure-origin.md) refused, in filter form. The
evaluation then showed the shape of that line's cost. "three hours of ambient" recovered its
45-track limit from the sentence but still searched the whole library, because the genre the
sentence had plainly named was not read; on the static arm the model gave a genre to 15 sentences
that only implied a mood or named an artist. A genre is not a mood: it is a word the library's own
profile carries, and the person can see it and correct it in the Intent exactly as they can a
number. Where the sentence says the word, the App can read it deterministically.

## Decision

- **The sentence reader also reads a genre the sentence names**, as a whole word. It reads the
  profile's genres and, without one or beside one, a static list; the two apply together, so a
  library without reggaeton still understands a sentence that names reggaeton.
- **The sentence's casing is the signal.** A lowercase genre always counts ("three hours of
  ambient", "fast techno"); a capitalised genre counts at the start of the sentence or after a
  lowercase word ("Ambient music", "three hours of Ambient"); a capitalised genre after a
  capitalised word is read as the rest of a name, so the artist "Daft Punk" is not the punk genre.
  The word must also stand alone: "the album Discovery" is not disco.
- **The static list leaves out the words that are commonly something else.** "house" is a room,
  "lo-fi" a style of recording, "dance" a mood; a sentence that uses one of them must not invent
  the genre. A profile's own genre list can carry them.
- **A genre the reader finds replaces the model's**, as a number does. A genre it does not find
  keeps the model's value.

## Considered options

- **Leave the genre to the model.** Rejected: the model lost the genre the sentence stated
  outright and invented one where the sentence only implied a mood; both are visible mistakes in
  the Intent that the App already has the word for.
- **Ask the prompt for it.** Rejected on the record, as in
  [ADR 0088](./0088-the-sentence-wins-on-the-numbers-it-states.md): prompt changes have been
  measured and lost, and this is a deterministic read of the sentence the App already sends.
- **Match the static list case-insensitively.** Rejected: "Daft Punk" would be the punk genre, and
  the search would return nothing for the artist the person named.
- **Take a genre only from the profile's list.** Rejected: the image arms lost 18 genres whose
  sentences named them because the library did not carry the word; the sentence still means it.

## Consequences

- **The Intent can carry a genre the model did not write.** It is shown in the Intent and the
  Filters sheet, and can be corrected, like the numbers.
- **A capitalised genre after a capitalised word is not read.** "Play Ambient" stays unread where
  "Play some Ambient" is ambient; the rule prefers the named artist, because a wrong genre narrows
  a search to nothing while a missing one leaves it as the model wrote it.
- **A synonym the list does not carry is not read.** "dnb" is not drum and bass, so the sentence's
  own filter adds nothing and the model's genre decision stands as §7 P2 describes.
- **k7-app §7 P2 gains the case-aware whole-word rule and the union of the profile's genres and the
  static words.** The prompt, the schema and the examples do not change.

## Supersedes

[ADR 0088](./0088-the-sentence-wins-on-the-numbers-it-states.md)'s rule that the extractor
"never writes a name, a mood or a genre", and its rejected option "extract names, moods or genres
too", for the genre only. Names and moods stay the model's: they have no list to check a word
against, and a wrong one is not a value the person can see and correct the same way.
