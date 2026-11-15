import { useEffect, useRef, useState } from 'react';

import { Input } from '@/components/ui/input';
import { updateDraft, type Draft } from '../playlists/drafts';

/**
 * The list's name, where the list is named: the stage header's page crumb, which is also the
 * field that renames it (k7-look.md K17). The editor then starts with its two columns rather
 * than with a second title over them.
 *
 * Pressing the name opens it; Enter or leaving the field commits, Escape abandons, and a name
 * that is empty or unchanged closes on the name the list already has — the id is derived from
 * the name when a list is made, and the file's `name` may not be empty
 * (`playlist-format.md`). A rename is an edit like any other: it changes the fingerprint, so
 * it schedules the write that carries the list to its card.
 */
export function PlaylistTitle({ draft }: { draft: Draft }) {
  const [editing, setEditing] = useState(false);
  const [name, setName] = useState(draft.name);
  /* Escape closes the field, and the blur that follows must not commit the text it abandoned. */
  const abandoned = useRef(false);

  /* A name that changed elsewhere — the row on the page, the card's own file — wins while the
   * field is closed. While it is open, the person's text is the one being typed. */
  useEffect(() => {
    if (!editing) setName(draft.name);
  }, [draft.name, editing]);

  const commit = () => {
    if (abandoned.current) return;
    const trimmed = name.trim();
    setEditing(false);
    if (trimmed === '' || trimmed === draft.name) {
      setName(draft.name);
      return;
    }
    updateDraft(draft.id, { name: trimmed });
  };

  if (!editing) {
    return (
      <button
        type="button"
        onClick={() => {
          abandoned.current = false;
          setName(draft.name);
          setEditing(true);
        }}
        aria-label={`Rename ${draft.name}`}
        aria-current="page"
        className="max-w-full truncate rounded-[4px] text-left text-sm font-light text-foreground outline-none hover:underline hover:underline-offset-4 focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-background"
      >
        {draft.name}
      </button>
    );
  }

  return (
    <label className="flex min-w-0 items-center">
      <span className="sr-only">Playlist name</span>
      <Input
        value={name}
        onChange={(event) => setName(event.target.value)}
        onBlur={commit}
        onKeyDown={(event) => {
          if (event.key === 'Enter') {
            event.preventDefault();
            event.currentTarget.blur();
          }
          if (event.key === 'Escape') {
            abandoned.current = true;
            setName(draft.name);
            setEditing(false);
          }
        }}
        maxLength={60}
        autoComplete="off"
        autoFocus
        className="h-7 w-72 max-w-full"
      />
    </label>
  );
}
