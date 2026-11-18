/*
 * littlefs image builder compiled to WebAssembly: create a ready-to-flash
 * littlefs partition image with no native binaries, in Node and the browser
 * alike. The module imports nothing, so it instantiates anywhere.
 *
 * Geometry is caller-provided (block/read/prog/cache/lookahead sizes must
 * match what will mount the image); ../src/index.js defaults to the
 * geometry ESP-IDF's esp_littlefs uses.
 *
 * The ABI is deliberately primitive (static buffers, no allocator) so the
 * module needs no imports: the host writes the filename into mk_name_buf()
 * and the file bytes into mk_file_buf(), then calls mk_add_file(len).
 */
#include <stdint.h>
#include <string.h>

#include "lfs.h"

#define BLOCK_CYCLES 512

#ifndef MKLFS_NAME_MAX
#define MKLFS_NAME_MAX 0 /* 0 = LFS_NAME_MAX (255) */
#endif

#define MAX_IMAGE_BYTES (16u * 1024u * 1024u)
#define MAX_FILE_BYTES (8u * 1024u * 1024u)
#define MAX_NAME_BYTES 256
#define MAX_CACHE_BYTES 4096
#define MAX_LOOKAHEAD_BYTES 1024

#define EXPORT(name) __attribute__((export_name(name)))

static uint8_t s_image[MAX_IMAGE_BYTES];
static uint8_t s_file_buf[MAX_FILE_BYTES];
static char s_name_buf[MAX_NAME_BYTES];

static uint8_t s_read_cache[MAX_CACHE_BYTES];
static uint8_t s_prog_cache[MAX_CACHE_BYTES];
static uint8_t s_file_cache[MAX_CACHE_BYTES];
static uint8_t s_lookahead[MAX_LOOKAHEAD_BYTES];

static lfs_t s_lfs;
static struct lfs_config s_cfg;

static int bd_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
  memcpy(buffer, &s_image[block * c->block_size + off], size);
  return 0;
}

static int bd_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
  memcpy(&s_image[block * c->block_size + off], buffer, size);
  return 0;
}

static int bd_erase(const struct lfs_config* c, lfs_block_t block) {
  memset(&s_image[block * c->block_size], 0xFF, c->block_size);
  return 0;
}

static int bd_sync(const struct lfs_config* c) {
  return 0;
}

EXPORT("mk_file_buf") uint8_t* mk_file_buf(void) {
  return s_file_buf;
}

EXPORT("mk_file_cap") uint32_t mk_file_cap(void) {
  return MAX_FILE_BYTES;
}

EXPORT("mk_name_buf") char* mk_name_buf(void) {
  return s_name_buf;
}

EXPORT("mk_name_cap") uint32_t mk_name_cap(void) {
  return MAX_NAME_BYTES;
}

EXPORT("mk_image_ptr") uint8_t* mk_image_ptr(void) {
  return s_image;
}

/*
 * Geometry constraints are validated here because littlefs's own asserts are
 * compiled out: read/prog must divide cache, cache must divide block,
 * lookahead is a multiple of 8.
 */
static int32_t setup_config(uint32_t block_size, uint32_t block_count, uint32_t read_size, uint32_t prog_size,
                            uint32_t cache_size, uint32_t lookahead_size) {
  if (block_size == 0 || block_count == 0 || (uint64_t)block_size * block_count > MAX_IMAGE_BYTES) {
    return LFS_ERR_INVAL;
  }
  if (read_size == 0 || prog_size == 0 || cache_size == 0 || cache_size > MAX_CACHE_BYTES ||
      cache_size % read_size != 0 || cache_size % prog_size != 0 || block_size % cache_size != 0) {
    return LFS_ERR_INVAL;
  }
  if (lookahead_size == 0 || lookahead_size > MAX_LOOKAHEAD_BYTES || lookahead_size % 8 != 0) {
    return LFS_ERR_INVAL;
  }

  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.read = bd_read;
  s_cfg.prog = bd_prog;
  s_cfg.erase = bd_erase;
  s_cfg.sync = bd_sync;
  s_cfg.read_size = read_size;
  s_cfg.prog_size = prog_size;
  s_cfg.block_size = block_size;
  s_cfg.block_count = block_count;
  s_cfg.block_cycles = BLOCK_CYCLES;
  s_cfg.cache_size = cache_size;
  s_cfg.lookahead_size = lookahead_size;
  s_cfg.name_max = MKLFS_NAME_MAX;
  s_cfg.read_buffer = s_read_cache;
  s_cfg.prog_buffer = s_prog_cache;
  s_cfg.lookahead_buffer = s_lookahead;
  return 0;
}

/** Format and mount a fresh filesystem of block_count blocks. */
EXPORT("mk_create")
int32_t mk_create(uint32_t block_size, uint32_t block_count, uint32_t read_size, uint32_t prog_size,
                  uint32_t cache_size, uint32_t lookahead_size) {
  int32_t err = setup_config(block_size, block_count, read_size, prog_size, cache_size, lookahead_size);
  if (err) {
    return err;
  }

  memset(s_image, 0xFF, block_size * block_count);
  err = lfs_format(&s_lfs, &s_cfg);
  if (err) {
    return err;
  }
  return lfs_mount(&s_lfs, &s_cfg);
}

/**
 * Mount an existing image the host has already written into mk_image_ptr()
 * (block_size * block_count bytes). Read-side counterpart of mk_create.
 */
EXPORT("mk_mount")
int32_t mk_mount(uint32_t block_size, uint32_t block_count, uint32_t read_size, uint32_t prog_size,
                 uint32_t cache_size, uint32_t lookahead_size) {
  int32_t err = setup_config(block_size, block_count, read_size, prog_size, cache_size, lookahead_size);
  if (err) {
    return err;
  }
  return lfs_mount(&s_lfs, &s_cfg);
}

/** Write mk_file_buf()[0..len) to the file named in mk_name_buf(). */
EXPORT("mk_add_file") int32_t mk_add_file(uint32_t len) {
  if (len > MAX_FILE_BYTES || s_name_buf[0] == '\0') {
    return LFS_ERR_INVAL;
  }

  struct lfs_file_config file_cfg = {.buffer = s_file_cache};
  lfs_file_t file;
  int err = lfs_file_opencfg(&s_lfs, &file, s_name_buf, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &file_cfg);
  if (err) {
    return err;
  }

  lfs_ssize_t written = lfs_file_write(&s_lfs, &file, s_file_buf, len);
  int close_err = lfs_file_close(&s_lfs, &file);
  if (written < 0) {
    return (int32_t)written;
  }
  if ((uint32_t)written != len) {
    return LFS_ERR_NOSPC;
  }
  return close_err;
}

/** Create a directory (parents must already exist). */
EXPORT("mk_mkdir") int32_t mk_mkdir(void) {
  if (s_name_buf[0] == '\0') {
    return LFS_ERR_INVAL;
  }
  return lfs_mkdir(&s_lfs, s_name_buf);
}

/** Unmount; the finished image is at mk_image_ptr(). */
EXPORT("mk_finish") int32_t mk_finish(void) {
  return lfs_unmount(&s_lfs);
}

/* --- Read side: directory iteration and file extraction --- */

static lfs_dir_t s_dir;
static uint32_t s_entry_size;

/** Open the directory named in mk_name_buf() ("/" for the root). */
EXPORT("mk_dir_open") int32_t mk_dir_open(void) {
  return lfs_dir_open(&s_lfs, &s_dir, s_name_buf);
}

/**
 * Advance the open directory. Returns 0 at the end, 1 for a file, 2 for a
 * directory, negative on error; the entry name lands in mk_name_buf() and
 * its size is readable via mk_entry_size(). "." and ".." are skipped.
 */
EXPORT("mk_dir_next") int32_t mk_dir_next(void) {
  struct lfs_info info;
  while (true) {
    int err = lfs_dir_read(&s_lfs, &s_dir, &info);
    if (err <= 0) {
      return err;
    }
    if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
      continue;
    }

    size_t len = strlen(info.name);
    if (len + 1 > MAX_NAME_BYTES) {
      return LFS_ERR_NAMETOOLONG;
    }
    memcpy(s_name_buf, info.name, len + 1);
    s_entry_size = info.size;
    return info.type == LFS_TYPE_DIR ? 2 : 1;
  }
}

EXPORT("mk_entry_size") uint32_t mk_entry_size(void) {
  return s_entry_size;
}

EXPORT("mk_dir_close") int32_t mk_dir_close(void) {
  return lfs_dir_close(&s_lfs, &s_dir);
}

/**
 * Read the whole file named in mk_name_buf() into mk_file_buf(). Returns
 * the file size, or negative on error (including files over the buffer cap).
 */
EXPORT("mk_read_file") int32_t mk_read_file(void) {
  if (s_name_buf[0] == '\0') {
    return LFS_ERR_INVAL;
  }

  struct lfs_file_config file_cfg = {.buffer = s_file_cache};
  lfs_file_t file;
  int err = lfs_file_opencfg(&s_lfs, &file, s_name_buf, LFS_O_RDONLY, &file_cfg);
  if (err) {
    return err;
  }

  lfs_soff_t size = lfs_file_size(&s_lfs, &file);
  if (size < 0 || size > (lfs_soff_t)MAX_FILE_BYTES) {
    lfs_file_close(&s_lfs, &file);
    return size < 0 ? (int32_t)size : LFS_ERR_NOMEM;
  }

  lfs_ssize_t read = lfs_file_read(&s_lfs, &file, s_file_buf, (lfs_size_t)size);
  int close_err = lfs_file_close(&s_lfs, &file);
  if (read < 0) {
    return (int32_t)read;
  }
  if (read != size) {
    return LFS_ERR_IO;
  }
  return close_err ? close_err : (int32_t)size;
}
