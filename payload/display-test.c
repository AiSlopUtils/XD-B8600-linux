#include <stdint.h>

#define VRAM ((volatile uint16_t *)0xac200000u)
#define LCDC (*(volatile uint16_t *)0xb4000000u)
#define PORTR (*(volatile uint8_t *)0xa405013cu)
#define SAR3 (*(volatile uint32_t *)0xfe008050u)
#define DAR3 (*(volatile uint32_t *)0xfe008054u)
#define TCR3 (*(volatile uint32_t *)0xfe008058u)
#define CHCR3 (*(volatile uint32_t *)0xfe00805cu)
#define DMAOR (*(volatile uint16_t *)0xfe008060u)

#define WIDTH 528u
#define HEIGHT 320u
#define EXPECTED_COOKIE 0x45585731u

static uint16_t rgb565(unsigned int red, unsigned int green,
                       unsigned int blue) {
  return (uint16_t)(((red & 0xf8u) << 8) |
                    ((green & 0xfcu) << 3) | (blue >> 3));
}

static void sync(void) {
  __asm__ volatile("synco" ::: "memory");
}

static void lcd_command(uint16_t command) {
  PORTR &= 0xefu;
  sync();
  LCDC = command;
  sync();
  PORTR |= 0x10u;
  sync();
}

static void refresh(void) {
  unsigned int timeout = 1000000u;

  lcd_command(0x2c);
  TCR3 = (WIDTH * HEIGHT) >> 4;
  SAR3 = 0x0c200000u;
  DAR3 = 0x14000000u;
  DMAOR &= 0xfffeu;
  CHCR3 = 0x40101401u;
  DMAOR |= 1u;
  while (!(CHCR3 & 2u) && --timeout) { }
}

static void fill_rect(unsigned int x, unsigned int y, unsigned int width,
                      unsigned int height, uint16_t color) {
  unsigned int row;
  unsigned int column;
  for (row = y; row < y + height; ++row)
    for (column = x; column < x + width; ++column)
      VRAM[row * WIDTH + column] = color;
}

uint32_t payload_main(uint32_t cookie) {
  volatile unsigned int delay;
  uint16_t background = rgb565(3, 18, 30);
  uint16_t cyan = rgb565(56, 220, 255);
  uint16_t green = rgb565(70, 235, 165);
  uint16_t warning = rgb565(255, 190, 70);

  fill_rect(0, 0, WIDTH, HEIGHT, background);
  fill_rect(0, 0, WIDTH, 36, cyan);
  fill_rect(26, 72, WIDTH - 52, 8, green);
  fill_rect(26, 100, WIDTH - 52, 72,
            cookie == EXPECTED_COOKIE ? green : warning);
  fill_rect(26, 194, WIDTH - 52, 8, green);
  fill_rect(26, 228, WIDTH - 52, 44, cyan);
  refresh();

  for (delay = 0; delay < 10000000u; ++delay)
    __asm__ volatile("nop");
  return cookie ^ 0xffffffffu;
}
