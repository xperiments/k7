#pragma once
/*
 * flash_job - run a flash write where it is safe to run one.
 *
 * Every HTTP worker has its stack in PSRAM (http_loop's create_task) because internal RAM
 * is the scarce resource here. A flash write disables the cache, and the PSRAM window
 * goes with it: a task whose stack lives in PSRAM cannot run through that window, and the
 * device resets. That is not a theory - a POST that wrote NVS, or that flashed a
 * Snapshot, reset the connection on the board while every read kept working.
 *
 * So the flash operations the page can trigger are handed to one task whose stack is
 * INTERNAL and whose body touches no PSRAM: the Snapshot write, the remembered Playlist
 * selection, and serving a file out of the Shell's SPIFFS partition (a SPIFFS read is a
 * flash read, and it disables the cache the same way). The caller blocks on a semaphore,
 * which is safe - waiting is not running.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the writer. Idempotent; call before anything can ask it to write (before the
 * HTTP listener at the latest). Returns false when the task could not be created. */
bool flash_job_start(void);

/* Run `fn(ctx)` on the writer and wait for it. `fn` touches flash (reads or writes) and
 * must never call flash_job_run itself. Before the writer exists this runs `fn` inline:
 * the only callers then are boot-time, on an internal stack.
 *
 * It waits until the job is done, with no timeout: `ctx` usually points at the caller's
 * stack frame, so returning early would leave the writer reading a frame that is gone. A
 * job that runs long is logged, not abandoned. */
bool flash_job_run(bool (*fn)(void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
