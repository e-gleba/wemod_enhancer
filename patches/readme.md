# Custom patches

Drop-in directory for user patch code. Every `*.c` file placed here is
compiled into `version.dll` automatically — no CMake edits needed (the
build re-globs this directory on every rebuild via `CONFIGURE_DEPENDS`).

## Contract

- Exactly **one** of your patch files must define the entry point:

  ```c
  void wemod_run_custom_patches(void)
  ```

  `DllMain` calls it right after the ASAR integrity fuse is flipped.
  With no patch files present the call compiles to an inline no-op —
  zero cost, no link errors.

- The public proxy API is available via:

  ```c
  #include <wemod_enhancer/fuses.h>
  ```

- Keep patch code `DllMain`-safe: the loader lock is held, so no
  `LoadLibrary` of complex graphs, no thread creation, no CRT-heavy work.

## Relocating the directory

Point the cache variable at any other location:

```sh
cmake -DWEMOD_ENHANCER_PATCHES_DIR=/path/to/my/patches ...
```

## Example

`patches/hello.c`:

```c
#include <windows.h>

static void my_patch(void)
{
    /* ... */
}

void wemod_run_custom_patches(void)
{
    my_patch();
}
```
