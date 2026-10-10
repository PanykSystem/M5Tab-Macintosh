// Host environment for native tests of the CPU core and translated ROM code.
#ifndef AOT_HOST_ENV_H
#define AOT_HOST_ENV_H

#include <stdint.h>

// Guest RAM is [0, size); the buffer needs 64 bytes of padding for accesses
// that start inside RAM and run past its end.
void HostSetRam(uint8_t *ram, uint32_t size);
// The ROM image, mapped at `base`, with the same padding.
void HostSetRom(uint8_t *rom, uint32_t base, uint32_t size);

#endif
