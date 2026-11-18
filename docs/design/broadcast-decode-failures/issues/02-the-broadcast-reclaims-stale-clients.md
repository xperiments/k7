# 02 — The broadcast reclaims stale stream clients

**What to build:** A client slot that a vanished listener is holding, reclaimed on its own. One
listener that disappears without closing its connection must not stop everyone else from
listening.

**Status:** fixed on the board, 2026-11-18. Three holes closed and one instrument added; the
exact vanished-peer case is not reproducible on this bench.

**Blocked by:** None.

## What was seen

The K7's console, after the player had been rebooted a few times mid-stream:

```
W (1555802) icecast: refusing a client: all 4 slots are taken
```

and from two other machines, with the K7 still playing normally:

```
mac /stream:      503, 164 B
player's stream:  "the K7 answered 503"
```

A hard reset of the K7 cleared it:

```
mac /stream:      200, 146286 B in 4 s
```

## What it probably is

The broadcast hands each registered connection to a per-client sender task and frees the slot
when that sender's `http_send_all()` fails (`sender_close(slot, fd, "send failed")`). A listener
that vanishes *without* a FIN — a Wi-Fi drop, a power cycle, a player rebooted mid-stream, a
laptop that sleeps — leaves a socket the stack has not yet failed, so the sender keeps waiting
and the slot stays taken. `CLIENT_SEND_TIMEOUT_MS` bounds a single send, but a sender with nothing
new to send is not sending at all: the socket is only probed when the ring has bytes and the
window opens.

The client table is four slots (`max_clients = 4`), so four such corpses are enough to refuse
everyone. That is the failure the bench saw.

## What to measure and decide

1. How long a vanished client's slot survives today: pull a listener's power mid-stream and watch
   `refusing a client`.
2. The reclaim rule: a sender that has made no progress for N seconds drops its client, whatever
   the socket believes. The sender is the only task that can see progress, so the rule belongs
   there, not in the broadcast.
3. The census: the periodic memory line should say how many slots are in use, so the next person
   sees four corpses instead of inferring them.

## What was fixed

1. **A dead client never woke its sender.** `client_sink()` marks a client dead when its ring is
   full and returns — but the sender is the only task that can close the slot, and with a full
   ring nothing else ever wakes it. It now wakes the sender on that path.
2. **The sender's wait is timed** (`pdMS_TO_TICKS(1000)` instead of `portMAX_DELAY`), so a wake
   that was never given cannot hold a slot for ever: once a second it looks at its client and
   takes the dead or closed path.
3. **Keepalive on the client socket** (`SO_KEEPALIVE`, and `TCP_KEEPIDLE/INTVL/CNT` where the
   stack offers them) gives the stack a reason to notice a peer that vanished without a FIN.
4. **The census now says how many slots are in use** — `clients N/4` on the periodic memory
   line — so the next occurrence reads as four corpses rather than being inferred.

## What the bench could and could not show

Four listeners stopped with `SIGSTOP` produce this on the board:

```
client 0 connected (icy=0, burst=19226 B)
client 1 connected (icy=0, burst=19226 B)
client 2 connected (icy=0, burst=19226 B)
refusing a client: all 4 slots are taken
```

That is the 503 of the original report, reproduced at will — and it is **correct**: those clients
are alive; the Mac's kernel acknowledges and buffers, so the ring never fills and nothing should
be reclaimed. What the bench cannot do is make a peer *vanish without a FIN* (a power cut, a
Wi-Fi drop) while its socket still looks open, which is the case that wedged the origin for
minutes. The three holes above are the paths by which that case can hold a slot; the census is
how the next one will be seen.
