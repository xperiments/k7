# Flash I/O runs on one task with an internal stack

The front door's writes were built the obvious way: the HTTP handler that receives a
Snapshot calls `app_board_snapshot_flash`, and the handler that changes the broadcast's
order writes the remembered id to NVS. On the board, both reset the device. Every read kept
working, which is what made it look like a protocol bug for an hour.

The cause is the two halves of this firmware meeting. Internal RAM is the scarce resource,
so the HTTP workers get their stacks from PSRAM (`http_loop`'s `create_task`, and the
comment there explains the 10 KB per-request it saved). A flash write disables the cache,
and on this chip the PSRAM window goes with it: a task whose stack lives in PSRAM cannot
run through the window. `Guru Meditation`, the connection resets, the device reboots, and
the partition it was writing is untouched. Serving a file from the Shell's SPIFFS partition
is a flash read through `esp_partition_read`, and it fails the same way.

## Decision

- **One writer task, stack in internal DRAM, no PSRAM in its body.** `components/portable/
  flash_job/` starts it before the HTTP listener and runs one closure at a time: the
  Snapshot flash, the NVS selection write, and serving a page out of SPIFFS. The caller
  blocks on a semaphore, which is safe — waiting is not running.
- **The page's `serve_file` runs on it.** That serialises page loads behind one task, which
  is the price. A page is a handful of files and a LAN is quick; a second flash-capable
  task would cost another internal stack for a burst that happens twice a session.
- **Its stack is 6 KB, and no I/O buffer lives on it.** The 2 KB transfer chunk moved to the
  heap; the first version put it on the stack and overflowed, which read as a null partition
  pointer inside SPIFFS rather than as a stack problem.

## What this costs

A slow client can hold the writer for as long as one file takes, and a Snapshot upload
waits behind it. The writer's caller gives up after a timeout, so a wedged job does not
wedge the page for ever, but the job itself is not cancelled.

## What it does not change

The portable components still compile on the host with no `#ifdef`: the shim's tasks and
semaphores are real, so `flash_job` runs there too and the integration suite exercises the
same path. The rule this leaves behind is the useful part: **in this firmware, anything that
touches flash at runtime goes through `flash_job_run`.**
