/** The multilingual word matcher the purpose table and the key spellings share. */

/** Japanese has no spaces, so its words match inside a run of characters; Latin words need edges. */
export function containsWord(text: string, word: string): boolean {
  if (/[\p{Script=Han}\p{Script=Hiragana}\p{Script=Katakana}]/u.test(word)) return text.includes(word);
  const escaped = word.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return new RegExp(`(?<![\\p{L}\\p{N}])${escaped}(?![\\p{L}\\p{N}])`, 'u').test(text);
}

export function hasAny(sentence: string, words: readonly string[]): boolean {
  return words.some((word) => containsWord(sentence.toLowerCase(), word));
}
