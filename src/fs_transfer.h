#ifndef FS_TRANSFER_H
#define FS_TRANSFER_H
#include <stddef.h>
#include <stdint.h>
#include <zephyr/fs/fs.h>
/* Callback only enqueues bytes. All filesystem work runs in main. */
void fs_editor_rx(const uint8_t *data, size_t len);
void fs_editor_reset(void);
void fs_editor_run(struct fs_mount_t *mp);
#endif
