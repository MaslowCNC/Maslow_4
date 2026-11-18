# mklfs.wasm

`../mklfs.wasm` lets the firmware installer read and build littlefs images in the browser. It is
littlefs 2.11.0 compiled to WebAssembly by `mklfs.c`, taken from
[karaxuna/wasm-os](https://github.com/karaxuna/wasm-os/tree/c73bfc431bac8f17aff36f6fa78575a0dfad3372/mklfs)
(published on npm as `@wasm-os/mklfs` 0.1.0, MIT license) with one change: `MKLFS_NAME_MAX` sets
littlefs's `name_max`.

The published module writes `name_max` 255 into the superblock. The Maslow firmware mounts with
esp_littlefs's `CONFIG_LITTLEFS_OBJ_NAME_LEN` of 64 and refuses to mount an image whose `name_max`
is larger, then formats the partition, which would delete `maslow.yaml`. This build uses 64, the
same as the release `littlefs.bin` that PlatformIO's mklittlefs makes.

## Rebuilding

With [wasi-sdk 25](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-25) and the
[littlefs v2.11.0](https://github.com/littlefs-project/littlefs/releases/tag/v2.11.0) source in
`littlefs/`:

```bash
$WASI_SDK/bin/clang --target=wasm32-wasi -Oz \
  -DLFS_NO_MALLOC -DLFS_NO_ASSERT -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR \
  -DMKLFS_NAME_MAX=64 -Ilittlefs -mexec-model=reactor -Wl,--no-entry -Wl,--strip-all \
  mklfs.c littlefs/lfs.c littlefs/lfs_util.c -o ../mklfs.wasm
```

The module imports nothing. Its exports are used directly by `../index.html` (`openLittlefs`,
`buildLittlefs`).

## Licenses

`mklfs.c`: MIT, from wasm-os. littlefs: BSD-3-Clause, see `LICENSE-littlefs.md`.
