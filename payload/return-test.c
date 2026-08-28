#include <stdint.h>

#define EXPECTED_COOKIE 0x45585731u

/*
 * Deliberately touches no hardware.  This isolates the P2 call/return ABI
 * from LCD, DMA, cache, storage, and interrupt behavior.
 */
uint32_t payload_main(uint32_t cookie) {
  volatile unsigned int delay;

  for (delay = 0; delay < 1000000u; ++delay)
    __asm__ volatile("nop");
  return cookie == EXPECTED_COOKIE ? 0x52455431u : 0x42414431u;
}
