/**
 * The App's J-card seam (k7art.md §5, art-packs.md §6): the factory designs at the top of
 * the pool, the pool itself, the reader a pack's J-cards go through, and the DOM renderer.
 *
 * APP-65 imports this module, not the files behind it: it reads a `.k7artpack` with
 * `readArtPack`, installs the active pack's J-cards with `setUserJcards`, and resets with
 * `resetUserJcards`. The Art packs page renders a design's preview with `Jcard` before
 * anything is uploaded. The store that fetches and caches the active pack is
 * `./active` - it is deliberately not re-exported here, because it opens IndexedDB and the
 * renderer must remain usable in a page that never touches the store.
 */
export {
  JCARD_BOUND,
  JCARD_FACES,
  JCARD_FIELDS,
  JCARD_FONT_ROLES,
  JCARD_HEIGHT,
  JCARD_PARTS,
  JCARD_PRIM_MAX,
  JCARD_SAFE,
  JCARD_SIZE_MAX,
  JCARD_SIZE_MIN,
  JCARD_SPACE,
  JCARD_UNITS_PER_INCH,
  JCARD_WIDTH,
  readJcardDesign,
  readJcardPack,
  type JcardDesign,
  type JcardFace,
  type JcardFields,
  type JcardField,
  type JcardFont,
  type JcardFontRole,
  type JcardPart,
  type JcardPrim,
  type JcardRect,
  type JcardSection,
  type JcardText,
} from './model';
export { jcardFrame, type JcardFit, type JcardFrame, type JcardNode } from './render';
export { Jcard } from './face';
export { FACTORY_JCARDS, factoryDesigns } from './factory';
export {
  ArtPackError,
  artPackKey,
  crc32,
  readArtPack,
  type ArtPack,
  type ArtPackPart,
} from './pack';
export {
  activePool,
  designFor,
  pickDesign,
  resetUserJcards,
  setUserJcardPack,
  setUserJcards,
  subscribePool,
  useJcardDesign,
  useJcardPool,
  userJcards,
} from './pool';
export { faceIndexFor, faceSeedOf, hueOf } from './seed';
