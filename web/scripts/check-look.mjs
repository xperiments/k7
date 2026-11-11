/**
 * The K7 look's automated checks, as [k7-look.md] §9.1 lists them.
 *
 * Each rule is a heuristic over the source, with the exceptions the spec names: the theme
 * and the coverflow's own rules live in `index.css`, the shelf's weights are its drawing,
 * and content images are allowed. `node scripts/check-look.mjs [srcDir]` exits 1 on a hit,
 * so it can be pointed at another tree (the pre-K7 commit) for the before/after counts.
 */
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const root = resolve(process.argv[2] ?? join(here, '..', 'src'));

function walk(dir) {
  return readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) return walk(path);
    return /\.(tsx?|css)$/.test(name) ? [path] : [];
  });
}

const files = walk(root).map((path) => ({
  rel: relative(root, path).split(sep).join('/'),
  lines: readFileSync(path, 'utf8').split('\n'),
}));

const contentImages = new Set([
  'ui/cover.tsx',
  'ui/cassette.tsx',
  'ui/album-detail.tsx',
  'ui/areas/Video.tsx',
  'site/Site.tsx',
]);

const rules = [
  {
    id: 'colour',
    what: 'hard-coded colours (the theme file owns them)',
    pattern: /#[0-9a-fA-F]{3,8}\b|rgba?\(|hsla?\(|oklch\(/,
    skip: (file) => file.rel === 'index.css',
  },
  {
    id: 'palette',
    what: 'Tailwind palette utilities (semantic tokens only)',
    pattern:
      /(bg|text|border|ring|fill|stroke|from|to|via|outline|divide|shadow)-(red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose|slate|gray|zinc|neutral|stone|white|black)(-[0-9]+)?/,
  },
  {
    id: 'decoration',
    what: 'shadows, gradients, blur (the shelf and the shadow tokens excepted)',
    pattern: /shadow-|bg-gradient|backdrop-blur|blur-|drop-shadow|ring-offset-[0-9]/,
    skip: (file) => file.rel === 'index.css',
    skipLine: (line) => line.includes('shadow-none') || line.includes('ring-offset-'),
  },
  {
    id: 'shape',
    what: 'radii other than panel/pill and headings above 300',
    pattern: /rounded-(lg|xl|2xl|3xl)|font-(semibold|bold)/,
    skip: (file) => file.rel === 'ui/album-shelf.tsx',
  },
  {
    id: 'image',
    what: 'raster images in UI chrome (content images excepted)',
    pattern: /<img|url\([^)]*\.(png|jpe?g|webp)/,
    skip: (file) => file.rel === 'index.css' || contentImages.has(file.rel),
  },
];

let failed = false;
for (const rule of rules) {
  const hits = [];
  for (const file of files) {
    if (rule.skip?.(file)) continue;
    file.lines.forEach((line, index) => {
      if (rule.skipLine?.(line)) return;
      if (rule.pattern.test(line)) hits.push(`${file.rel}:${index + 1}: ${line.trim()}`);
    });
  }
  console.log(`${hits.length === 0 ? 'ok  ' : 'FAIL'} ${rule.id}: ${rule.what} — ${hits.length}`);
  for (const hit of hits) console.log(`     ${hit}`);
  if (hits.length > 0) failed = true;
}

process.exit(failed ? 1 : 0);
