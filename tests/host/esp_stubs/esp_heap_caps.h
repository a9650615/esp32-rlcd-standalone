#pragma once

// Host stand-in for ESP-IDF's capability-aware allocator, so a module that
// asks for PSRAM can be compiled and run by tests/host.
//
// Plain malloc: on the board the capability flags decide *which* heap the
// allocation comes from, and the difference is the whole reason several
// defects in this codebase existed - but none of that is observable from a
// host process, and pretending otherwise would only be a fake that tests
// itself. What these tests check is what the module does with the bytes.
//
// A shim rather than #ifdef ESP_PLATFORM inside the module: the module's
// allocation calls are the real thing under test, and guarding them would
// mean the host runs different code from the board.

#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_EXEC     (1 << 0)
#define MALLOC_CAP_32BIT    (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT  (1 << 12)

static inline void* heap_caps_malloc(size_t size, uint32_t caps) {
  (void)caps;
  return malloc(size);
}

static inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
  (void)caps;
  return realloc(ptr, size);
}

static inline void heap_caps_free(void* ptr) { free(ptr); }
