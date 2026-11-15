/**
 * The lists a selected card has: which answer to read, and one row per list.
 *
 * A **Playlist** belongs to the card whose Tracks it names, so the Playlists area is not about
 * two collections any more: the selected card answers, and the page shows its lists once. This
 * module holds the two rules that make that answer legible, and nothing else.
 *
 * `chooseListSource` picks the answer: the player for the **Device card**, the card's own
 * `k7.playlists/` while it is open, and nothing otherwise. There is no copy of a card's lists in
 * this browser: a card that is in neither place has no readable lists, and the page shows only
 * the pending edits rather than something dated (ADR 0095).
 *
 * `joinLists` folds the answer together with this browser's drafts and its **pending removals**
 * into one row per list, saying how far it has got — only here, on its card, on the player — and
 * whether the copy there is current. The two are separate facts on purpose: a list can be on the
 * player and edited here since, and one ladder cannot say both.
 *
 * It is pure on purpose too. Reach and currency are the kind of thing that goes wrong quietly,
 * and neither needs a browser to be tested.
 */

/** A Playlist's id as the image's list record and `select` carry it: lowercase hex, padded. */
export function normaliseListId(id: string): string {
  const cleaned = id.trim().toLowerCase().replace(/^0x/, '');
  return /^[0-9a-f]{1,16}$/.test(cleaned) ? cleaned.padStart(16, '0') : cleaned;
}

/** One Playlist as an answer that carries its rows states it: a card's file, or the player. */
export interface ListRecord {
  /** 16 hex characters; the id `select` and `k7.select` carry. */
  id: string;
  name: string;
  /** Unix seconds. */
  createdAt: number;
  /** Snapshot paths in play order. */
  rows: string[];
  /** The file's content fingerprint, when the answer carries one (spec §7). */
  fingerprint?: string;
}

/** One Playlist as the player's index states it: the rows' count, not the rows. */
export interface ListSummary {
  id: string;
  name: string;
  createdAt: number;
  rows: number;
  /** The file's fingerprint, which the player reports since ADR 0094. */
  fingerprint?: string;
}

/** What the join needs from a **Draft**: the list's identity, its current rows and its hash. */
export interface DraftList {
  id: string;
  name: string;
  createdAt: number;
  rows: readonly string[];
  /** The draft's content fingerprint, when the caller computed it. */
  fingerprint?: string;
}

/** Which answer carries a card's lists. */
export type ListSourceKind = 'device' | 'card' | 'none';

/**
 * The rule, as data: which answer the page reads for the selected card.
 *
 * The **Device card** is the player and nothing else — with no card in the player there is no
 * source to read. Every other selection is a card in the computer: its own files while its
 * directory is readable, and nothing when it is not.
 */
export function chooseListSource(input: {
  /** The header's card switcher is on the Device card. */
  deviceView: boolean;
  /** The player reports a card it can name. */
  deviceCard: boolean;
  /** The selected card's directory is readable this session: a handle and its grant. */
  cardOpen: boolean;
}): ListSourceKind {
  if (input.deviceView) return input.deviceCard ? 'device' : 'none';
  return input.cardOpen ? 'card' : 'none';
}

/** How far a list has got. */
export type Reach = 'here' | 'card' | 'player';

/** One list, as the Playlists page shows it. */
export interface ListRow {
  id: string;
  name: string;
  /** Unix seconds. */
  createdAt: number;
  /** How many rows the page counts: the draft's when there is one, else the answer's. */
  rows: number;
  reach: Reach;
  /** This browser holds a draft for it. */
  draft: boolean;
  /** The card carries it, in its own file or in this browser's copy of that file. */
  onCard: boolean;
  /** The card's copy is older than the draft — the edit has not reached the card. */
  stale: boolean;
  /** Deleted here, and the source still carries it until the card is written. */
  removalPending: boolean;
  /** The player is walking this list now. */
  walking: boolean;
  /** The player was told to walk this list, and the card is out. */
  selected: boolean;
}

export interface JoinedLists {
  rows: ListRow[];
  /** Which answer supplied the card's lists; `none` means the card is not open. */
  card: 'card' | 'none';
}

export interface JoinInput {
  /** What the selected card carries, when its directory was read. */
  card: { kind: 'card'; lists: readonly ListRecord[] } | null;
  /** The player's index, when it holds this card and answered. */
  player: { lists: readonly ListSummary[]; selected: string | null; walking: boolean } | null;
  /** This browser's drafts for the card. */
  drafts: readonly DraftList[];
  /** The ids deleted here that the card has not been written since. */
  removals: readonly string[];
}

/**
 * Whether the card's copy says the same thing as the draft. The fingerprint when both sides
 * have one — the device reports it, and a file read back has one — and the rows otherwise.
 */
function sameList(
  draft: DraftList,
  copy: { rows: readonly string[] | null; fingerprint?: string; rowsCount: number },
): boolean {
  if (draft.fingerprint !== undefined && copy.fingerprint !== undefined) {
    return draft.fingerprint === copy.fingerprint;
  }
  return copy.rows !== null ? sameRows(draft.rows, copy.rows) : draft.rows.length === copy.rowsCount;
}

/** Whether two row lists are the same order of the same paths. */
function sameRows(left: readonly string[], right: readonly string[]): boolean {
  if (left.length !== right.length) return false;
  return left.every((path, at) => path === right[at]);
}

/**
 * One row per list, from the three answers that can name one.
 *
 * A list this browser has deleted appears only while a source still carries it — a removal
 * whose card is already without the list is nothing to show, and the caller can drop the
 * marker. A name the person changed here wins over the card's, because the page is about to
 * write it back; `stale` is what says the card has not got it yet.
 */
export function joinLists(input: JoinInput): JoinedLists {
  const cardLists = new Map<string, ListRecord>();
  for (const list of input.card?.lists ?? []) cardLists.set(normaliseListId(list.id), list);

  const playerLists = new Map<string, ListSummary>();
  for (const list of input.player?.lists ?? []) playerLists.set(normaliseListId(list.id), list);

  const drafts = new Map<string, DraftList>();
  for (const draft of input.drafts) drafts.set(normaliseListId(draft.id), draft);

  const removals = new Set(input.removals.map(normaliseListId));
  const ids = new Set<string>([...cardLists.keys(), ...playerLists.keys(), ...drafts.keys()]);
  for (const id of removals) ids.add(id);

  const rows: ListRow[] = [];
  for (const id of ids) {
    const onCard = cardLists.get(id) ?? null;
    const onPlayer = playerLists.get(id) ?? null;
    const draft = drafts.get(id) ?? null;
    const removalPending = removals.has(id);

    /* A removal with nothing left to remove: the card was written, or never had the list. */
    if (draft === null && onCard === null && onPlayer === null) continue;

    const reach: Reach = onPlayer !== null ? 'player' : onCard !== null ? 'card' : 'here';
    const walking = onPlayer !== null && input.player?.walking === true;
    const selected =
      onPlayer !== null &&
      input.player?.selected != null &&
      normaliseListId(input.player.selected) === id;

    rows.push({
      id,
      name: draft?.name ?? onCard?.name ?? onPlayer?.name ?? '',
      createdAt: draft?.createdAt ?? onCard?.createdAt ?? onPlayer?.createdAt ?? 0,
      rows: draft !== null ? draft.rows.length : (onCard?.rows.length ?? onPlayer?.rows ?? 0),
      reach,
      draft: draft !== null,
      onCard: onCard !== null,
      /* The source that carries the list is the copy to compare with: the card's file when it
       * is here, else the player's own fingerprint of that file. */
      stale:
        draft !== null &&
        ((onCard !== null &&
          !sameList(draft, { rows: onCard.rows, fingerprint: onCard.fingerprint, rowsCount: onCard.rows.length })) ||
          (onCard === null &&
            onPlayer !== null &&
            !sameList(draft, {
              rows: null,
              fingerprint: onPlayer.fingerprint,
              rowsCount: onPlayer.rows,
            }))),
      removalPending,
      walking,
      selected,
    });
  }

  rows.sort(
    (left, right) => left.createdAt - right.createdAt || left.id.localeCompare(right.id),
  );

  return {
    rows,
    card: input.card?.kind ?? 'none',
  };
}
