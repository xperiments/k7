// <generated:label-vocabulary>
// assets/labels/vocabulary.json is the source. tools/label_pack.py writes this
// block, and `make -C host label` refuses it when it is stale. Do not edit: the
// vocabulary is the source, and this is the editor's copy of it.
export const VOCAB = {
  space: { w: 748, h: 359 },
  aperture: { x: 95, y: 101, w: 556, h: 166, r: 83 },
  value: 96,
  canvas: 8,
  limits: { prims: 48, slots: 16, templates: 32 },
  coords: {
    x: [-32768, 32767],
    y: [-32768, 32767],
    w: [1, 65535],
    h: [1, 65535],
  },
  slots: [
    { id: "song_title", label: "Song Title", sim: "Neon Skyline" },
    { id: "song_artist", label: "Song Artist", sim: "The Analog Sons" },
    { id: "song_album", label: "Song Album", sim: "Night Drive" },
    { id: "genre", label: "Genre", sim: "Synthwave" },
    { id: "bpm", label: "BPM", sim: "124" },
    { id: "musical_key", label: "Musical Key", sim: "8A" },
    { id: "energy", label: "Energy", sim: "70" },
    { id: "danceability", label: "Danceability", sim: "62" },
    { id: "valence", label: "Valence", sim: "55" },
    { id: "arousal", label: "Arousal", sim: "48" },
    { id: "mood", label: "Mood", sim: "relaxed" },
  ],
  fonts: [
    { name: "Jura", px: 20, symbol: "jura_20" },
    { name: "Jura", px: 28, symbol: "jura_28" },
    { name: "Jura", px: 36, symbol: "jura_36" },
    { name: "Archivo Narrow 600", px: 32, symbol: "archivo_narrow_600_32", editor: true },
    { name: "Barlow Condensed 700", px: 78, symbol: "barlow_condensed_700_78", editor: true },
    { name: "IBM Plex Mono 500", px: 18, symbol: "ibm_plex_mono_500_18", editor: true },
  ],
  defaults: { fonts: {
    title: { ff: "Archivo Narrow 600", px: 32 },
    numeral: { ff: "Barlow Condensed 700", px: 78 },
    caption: { ff: "IBM Plex Mono 500", px: 18 },
  } },
} as const;
// </generated:label-vocabulary>
