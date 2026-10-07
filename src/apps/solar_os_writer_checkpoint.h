#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "solar_os_writer_buffer.h"
#include "solar_os_writer_files.h"
#include "solar_os_storage.h"

typedef struct solar_os_writer_checkpoint solar_os_writer_checkpoint_t;
typedef struct {
    uint32_t cursor, anchor, zoom;
    uint64_t version;
    bool saved; /* tombstone: no recoverable document */
} solar_os_writer_checkpoint_meta_t;
typedef struct {
    uint64_t requested, inflight, pending, completed;
    uint32_t generation, submitted, replaced, writes, errors, cancelled;
    size_t snapshot_bytes, peak_snapshot_bytes;
    unsigned stack_free;
    esp_err_t error;
    bool busy, paused;
} solar_os_writer_checkpoint_status_t;

/* All public calls belong to the application task. No live document is retained.
 * The private worker and its mutex are shared; each client reserves its path. */
esp_err_t solar_os_writer_checkpoint_open(const char *recovery, const char *document,
    solar_os_writer_checkpoint_t **out);
void solar_os_writer_checkpoint_close(solar_os_writer_checkpoint_t *c);
esp_err_t solar_os_writer_checkpoint_submit(solar_os_writer_checkpoint_t *c,
    const solar_os_writer_buffer_t *buffer, const solar_os_writer_checkpoint_meta_t *meta);
void solar_os_writer_checkpoint_status(solar_os_writer_checkpoint_t *c,
    solar_os_writer_checkpoint_status_t *out);
/* Pause promotions, drop pending, and join active I/O. Remains paused until resume. */
void solar_os_writer_checkpoint_fence(solar_os_writer_checkpoint_t *c);
void solar_os_writer_checkpoint_resume(solar_os_writer_checkpoint_t *c);
esp_err_t solar_os_writer_checkpoint_wait(solar_os_writer_checkpoint_t *c);
/* Application-task device boundary: drain accepted snapshots. With shutdown,
 * join the shared worker too; subsequent submissions are rejected. */
esp_err_t solar_os_writer_checkpoint_drain_all(bool shutdown);
esp_err_t solar_os_writer_checkpoint_available(solar_os_writer_checkpoint_t *c,
    const char *recovery, const char *document);
esp_err_t solar_os_writer_checkpoint_rebind(solar_os_writer_checkpoint_t *c,
    const char *recovery, const char *document);
/* Synchronous boundary transaction, requires a fenced client. Also used for
 * save/discard tombstones. Destination must be unclaimed or claimed by c. */
esp_err_t solar_os_writer_checkpoint_commit(solar_os_writer_checkpoint_t *c,
    const char *recovery, const char *document, const solar_os_writer_buffer_t *buffer,
    const solar_os_writer_checkpoint_meta_t *meta);
/* Reads only a verified destination, never temp/backup. Legacy plain UTF-8 is
 * returned with legacy=true. Caller frees text with heap_caps_free(). */
esp_err_t solar_os_writer_checkpoint_read(const char *recovery, const char *document,
    char **text, size_t *len, solar_os_writer_checkpoint_meta_t *meta, bool *legacy);
