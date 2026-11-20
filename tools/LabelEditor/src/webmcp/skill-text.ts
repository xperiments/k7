/**
 * The skill's text, as the page sees it.
 *
 * This is the one module that uses Vite's `?raw` import, and it is kept out of `tools.ts`
 * and `skill.ts` for a reason: `scripts/build-skill.mjs` loads the registry through Node
 * (jiti) to regenerate the skill's tool reference, and a `?raw` import would not resolve
 * there. The page passes the text in; the registry stays plain TypeScript.
 */
import skillText from "../../skills/k7-art/SKILL.md?raw";

export const SKILL_TEXT = skillText;
