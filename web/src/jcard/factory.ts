import { readJcardDesign, type JcardDesign } from './model';

/**
 * The factory pack's J-cards (art-packs.md §2): every K7Art in `assets/k7art/`, read at
 * build time from the same files the Python packer turns into the embedded label sheet.
 *
 * The folder is outside the web root, so Vite resolves it through the `@k7art` alias. Only
 * the three container fields the App draws are imported: Vite's JSON named exports are
 * tree-shaken, so the label section - the sheet's, not the browser's - never reaches the
 * bundle. A design the renderer cannot draw is left out rather than failing the App's boot;
 * the K7Art's own Export and the packer are where a bad file is refused by name.
 *
 * The pool orders the factory designs by id, the order the sheet and a pack use
 * (k7art.md §1, art-packs.md §5).
 */
const IDS = import.meta.glob('@k7art/*.json', {
  eager: true,
  import: 'id',
}) as Record<string, unknown>;
const NAMES = import.meta.glob('@k7art/*.json', {
  eager: true,
  import: 'name',
}) as Record<string, unknown>;
const JCARDS = import.meta.glob('@k7art/*.json', {
  eager: true,
  import: 'jcard',
}) as Record<string, unknown>;

export const FACTORY_JCARDS: readonly JcardDesign[] = Object.keys(JCARDS)
  .map((path) =>
    readJcardDesign({
      id: IDS[path],
      name: NAMES[path],
      jcard: JCARDS[path],
    }),
  )
  .filter((design): design is JcardDesign => design !== null)
  .sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));

export function factoryDesigns(): readonly JcardDesign[] {
  return FACTORY_JCARDS;
}
