# Agent usage

Every turn an agent takes re-sends the whole conversation above it, so a session costs roughly
`tokens per turn × turns`, times the model's price. Both factors are measurable from the
transcripts; this file says where, what the first audit found, and how to check that a change
moved the number. The habits it measures are in [`AGENTS.md`](../../AGENTS.md#how-to-work).

## Where the meter is

- The app's account settings show the plan's usage and when it resets.
- Each session writes one `"usage"` record per request: `inputTokens` (new this turn),
  `cacheReadTokens` (the context above, re-read) and `totalTokens`, at
  `~/.dsh/sessions/--Users-pedrocasaubon-Documents-xtape--/session-<id>/session.v4.jsonl.zstd`.

## Audit one session

```bash
s=~/.dsh/sessions/--Users-pedrocasaubon-Documents-xtape--/session-<id>/session.v4.jsonl.zstd
zstd -dc "$s" | grep -o '"usage":{[^}]*}' | tr -d '"' | awk -F'[:,}]' \
  '{for(i=1;i<NF;i++) if($i=="totalTokens"){t=$(i+1); if(!n++)f=t; l=t; if(t>m)m=t}} \
   END{printf "turns=%d floor=%d last=%d peak=%d\n", n, f, l, m}'
```

`floor` is what every session starts with: the harness's system prompt and tool schemas,
`AGENTS.md`, the skill catalogue. `last` is what the newest turn re-reads. A session whose `last`
is several times its `floor` should have been split or summarised.

## Audit the week

```bash
d=~/.dsh/sessions/--Users-pedrocasaubon-Documents-xtape--
for s in "$d"/session-*/; do
  mt=$(stat -f %m "$s"); [ "$mt" -lt "$(date -v-7d +%s)" ] && continue
  day=$(date -r "$mt" +%Y-%m-%d)
  zstd -dc "$s/session.v4.jsonl.zstd" 2>/dev/null \
    | grep -o '"usage":{[^}]*}' | tr -d '"' \
    | awk -F'[:,}]' -v day="$day" '{for(i=1;i<NF;i++) if($i=="totalTokens"){r+=$(i+1);n++}}
       END{if(n) printf "%s\t%d\t%d\n", day, n, r}'
done | awk -F'\t' '{t[$1]+=$2; r[$1]+=$3} END{for(k in t) printf "%s\t%5d turns\t%6.1fM re-read\n", k, t[k], r[k]/1e6}' | sort
```

## What the first audit found (25 Nov 2026)

- 135 sessions recorded. The floor was 19–21k tokens: about 2.2k of it `AGENTS.md`, about 1.2k
  the skill catalogue, the rest the harness's system prompt and tool schemas.
- The days 20–25 Nov re-read about 2.8 billion tokens. The 20th alone was 5,783 turns and 2.05
  billion.
- Two sessions ran 3,918 and 3,798 turns; the longer re-read 1.54 billion tokens.
- Only about 5.6% of the 20th's 2.05 billion was the floor. The rest was the conversation growing,
  so the turns — not the files — were where the limit went.

The changes that followed were one task per session, targeted reads of the two `CONTEXT.md`
glossaries, and this measurement kept as the baseline. The other factor is the model: the same
tokens on the largest tier cost several times what they do on the default.
