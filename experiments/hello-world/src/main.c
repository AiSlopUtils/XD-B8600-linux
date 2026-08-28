#include <graphics/drawing.h>
#include <graphics/color.h>
#include <graphics/text.h>
#include <graphics/lcdc.h>
#include <graphics/init.h>

#define SCREEN_WIDTH 528
#define SCREEN_HEIGHT 320

int main(void) {
  volatile unsigned long delay;
  struct font *fnt = get_font();

  graphics_init(SCREEN_WIDTH, SCREEN_HEIGHT, (void *)0xAC200000);

  set_pen(create_rgb16(0, 0, 0));
  draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  set_pen(create_rgb16(255, 255, 255));
  render_text((SCREEN_WIDTH - (sizeof "XD-B8600 LCD initialized" - 1) * fnt->width) / 2, SCREEN_HEIGHT / 2 - fnt->height * 2, "XD-B8600 LCD initialized");
  set_pen(create_rgb16(0, 255, 0));
  render_text((SCREEN_WIDTH - (sizeof "Returning automatically" - 1) * fnt->width) / 2, SCREEN_HEIGHT / 2, "Returning automatically");
  lcdc_copy_vram();

  /* Avoid model-specific keypad mappings while testing display startup. */
  for (delay = 0; delay < 1000000000UL; ++delay) { }
  return -2;
}
