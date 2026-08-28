#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <graphics/color.h>
#include <graphics/drawing.h>
#include <graphics/lcdc.h>
#include <graphics/text.h>
#include <sh4a/input/keypad.h>
#include <syscalls/syscalls.h>

#define SCREEN_WIDTH 528
#define SCREEN_HEIGHT 320
#define MAX_LINES 72
#define LINE_LENGTH 64
#define REPORT_LENGTH 6144
#define LINES_PER_PAGE 16

#define REG32(address) (*(volatile uint32_t *)(address))

struct rom_header {
  char signature[12];
  char model[4];
  uint32_t magic;
  uint32_t nor_size;
};

static char lines[MAX_LINES][LINE_LENGTH];
static char report[REPORT_LENGTH];
static unsigned int line_count;

static void add_line(const char *format, ...) {
  va_list args;

  if (line_count >= MAX_LINES) return;
  va_start(args, format);
  vsprintf(lines[line_count], format, args);
  ++line_count;
}

static struct rom_header *find_rom_header(void) {
  struct rom_header *header;

  header = (struct rom_header *)0x8000ff80;
  if (memcmp(header->signature, "CASIODICS", 9) == 0) return header;
  header = (struct rom_header *)0x8001ff80;
  if (memcmp(header->signature, "CASIODICS", 9) == 0) return header;
  return 0;
}

static void copy_field(char *destination, const char *source,
                       unsigned int length) {
  unsigned int i;

  for (i = 0; i < length; ++i) {
    char value = source[i];
    destination[i] = (value >= 32 && value <= 126) ? value : '.';
  }
  destination[length] = 0;
}

static unsigned int parse_model_number(const char model[4]) {
  unsigned int i;
  unsigned int value = 0;

  for (i = 1; i < 4; ++i) {
    if (model[i] < '0' || model[i] > '9') break;
    value = value * 10 + (unsigned int)(model[i] - '0');
  }
  return value;
}

static uint32_t external_ram_size(struct rom_header *header,
                                  uint32_t pvr) {
  unsigned int model_number;

  if (header != 0 && header->model[0] == 'C') {
    model_number = parse_model_number(header->model);
    return model_number >= 160 ? 0x01000000 : 0x00800000;
  }
  if (pvr == 0x10300b00) return 0x01000000;
  return 0x00200000;
}

static unsigned int clock_divisor(unsigned int code) {
  static const unsigned char divisors[16] = {
    2, 3, 4, 6, 8, 12, 16, 0, 24, 32, 36, 48, 0, 72, 0, 0
  };
  return divisors[code & 15];
}

static uint32_t read_sr(void) {
  uint32_t value;
  __asm__ volatile("stc sr,%0" : "=r"(value));
  return value;
}

static uint32_t read_vbr(void) {
  uint32_t value;
  __asm__ volatile("stc vbr,%0" : "=r"(value));
  return value;
}

static uint32_t read_gbr(void) {
  uint32_t value;
  __asm__ volatile("stc gbr,%0" : "=r"(value));
  return value;
}

static unsigned int make_report(void) {
  unsigned int i;
  unsigned int offset = 0;

  for (i = 0; i < line_count; ++i) {
    unsigned int length = (unsigned int)strlen(lines[i]);
    if (offset + length + 2 >= REPORT_LENGTH) break;
    memcpy(report + offset, lines[i], length);
    offset += length;
    report[offset++] = '\r';
    report[offset++] = '\n';
  }
  report[offset] = 0;
  return offset;
}

static int save_report(const char *path, unsigned int length) {
  int descriptor;
  int written;

  sys_delete(path);
  if (sys_create(path, 1) < 0) return -1;
  descriptor = sys_open(path, FILE_WR);
  if (descriptor < 0) return -2;
  written = sys_write(descriptor, report, length);
  sys_close(descriptor);
  return written == (int)length ? 0 : written;
}

static void render_page(unsigned int page, unsigned int page_count,
                        int save_status) {
  unsigned int i;
  unsigned int index;
  unsigned int y = 36;
  char footer[LINE_LENGTH];

  set_pen(create_rgb16(7, 13, 23));
  draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  set_pen(create_rgb16(22, 46, 74));
  draw_rect(0, 0, SCREEN_WIDTH, 28);
  set_pen(create_rgb16(116, 214, 255));
  render_text(8, 8, "XD-B8600 SYSTEM INFORMATION");

  for (i = 0; i < LINES_PER_PAGE; ++i) {
    index = page * LINES_PER_PAGE + i;
    if (index >= line_count) break;
    if (lines[index][0] == '[') {
      set_pen(create_rgb16(86, 235, 176));
    } else if (lines[index][0] == '!') {
      set_pen(create_rgb16(255, 193, 92));
    } else {
      set_pen(create_rgb16(226, 232, 240));
    }
    render_text(8, y, lines[index]);
    y += 16;
  }

  set_pen(create_rgb16(22, 46, 74));
  draw_rect(0, 292, SCREEN_WIDTH, 28);
  set_pen(save_status == 0 ? create_rgb16(86, 235, 176)
                           : create_rgb16(255, 193, 92));
  sprintf(footer, "LEFT/RIGHT: pages  BACK: exit  %u/%u  SAVE:%s",
          page + 1, page_count, save_status == 0 ? "OK" : "ERR");
  render_text(8, 300, footer);
  lcdc_copy_vram();
}

int main(int entry_context, char **unused_argument) {
  struct rom_header *header = find_rom_header();
  uint32_t pvr = REG32(0xff000030);
  uint32_t cvr = REG32(0xff000040);
  uint32_t prr = REG32(0xff000044);
  uint32_t mmucr = REG32(0xff000010);
  uint32_t ccr = REG32(0xff00001c);
  uint32_t frqcr = REG32(0xa4150000);
  uint32_t ram_size = external_ram_size(header, pvr);
  uint32_t nor_size = header != 0 ? header->nor_size : 0;
  unsigned long internal_total = 0, internal_free = 0;
  unsigned long card_total = 0, card_free = 0;
  unsigned long install_drive = 0;
  int internal_total_status, internal_free_status;
  int card_total_status, card_free_status = -99;
  int save_status;
  unsigned int report_length;
  unsigned int page = 0;
  unsigned int page_count;
  unsigned int pll_field = (frqcr >> 24) & 0x3f;
  unsigned int iclk_code = (frqcr >> 20) & 0x0f;
  unsigned int shclk_code = (frqcr >> 12) & 0x0f;
  unsigned int bclk_code = (frqcr >> 8) & 0x0f;
  unsigned int pclk_code = frqcr & 0x0f;
  char signature[13] = "not found";
  char model[5] = "none";
  char addin_id[10] = {0};
  const char *report_path;
  volatile unsigned int delay;
  union { uint32_t word; unsigned char bytes[4]; } endian = {0x01020304};

  (void)unused_argument;

  sys_dict_info(&install_drive, addin_id);
  internal_total_status = sys_totaldiskspace("drv0", &internal_total);
  internal_free_status = sys_freediskspace("drv0", &internal_free);
  card_total_status = sys_totaldiskspace("crd0", &card_total);
  if (card_total_status == 0)
    card_free_status = sys_freediskspace("crd0", &card_free);

  if (header != 0) {
    copy_field(signature, header->signature, 12);
    copy_field(model, header->model, 4);
  }
  report_path = install_drive ? "\\\\crd0\\SYSINFO.TXT"
                              : "\\\\drv0\\SYSINFO.TXT";

  add_line("[Identity and firmware]");
  add_line("Tool: System Information v1");
  add_line("Retail model: Casio EX-word XD-B8600");
  add_line("Add-in ID: %s", addin_id);
  add_line("Install medium: %s", install_drive ? "microSD" : "internal");
  add_line("ROM header address: 0x%08X", (uint32_t)header);
  add_line("ROM signature: %s", signature);
  add_line("Firmware model code: %s", model);
  add_line("Header magic: 0x%08X", header ? header->magic : 0);
  add_line("Entry context: 0x%08X", (uint32_t)entry_context);
  add_line("Syscall dispatcher: 0x%08X", REG32(0x74000000));
  add_line("");

  add_line("[CPU and execution state]");
  add_line("CPU family: %s", pvr == 0x10300b00 ? "Renesas SH-4A" : "SH family");
  add_line("PVR: 0x%08X", pvr);
  add_line("PRR: 0x%08X", prr);
  add_line("CVR: 0x%08X", cvr);
  add_line("SR: 0x%08X", read_sr());
  add_line("VBR: 0x%08X", read_vbr());
  add_line("GBR: 0x%08X", read_gbr());
  add_line("MMUCR: 0x%08X", mmucr);
  add_line("CCR: 0x%08X", ccr);
  add_line("Byte order: %s-endian", endian.bytes[0] == 4 ? "little" : "big");
  add_line("");

  add_line("[Clock generator]");
  add_line("FRQCR: 0x%08X", frqcr);
  add_line("PLL field: %u  multiplier: x%u", pll_field,
           (pll_field + 1) * 2);
  add_line("ICLK divisor: /%u (code %u)", clock_divisor(iclk_code), iclk_code);
  add_line("SHCLK divisor: /%u (code %u)", clock_divisor(shclk_code), shclk_code);
  add_line("BCLK divisor: /%u (code %u)", clock_divisor(bclk_code), bclk_code);
  add_line("PCLK divisor: /%u (code %u)", clock_divisor(pclk_code), pclk_code);
  add_line("!MHz omitted: oscillator frequency is not yet verified");
  add_line("");

  add_line("[Memory map]");
  add_line("External RAM: %u bytes (%u MiB)", ram_size,
           ram_size / 1048576);
  add_line("External RAM range: 0x8C000000-0x%08X",
           0x8c000000 + ram_size - 1);
  add_line("NOR flash: %u bytes (%u MiB)", nor_size,
           nor_size / 1048576);
  add_line("NOR mapping: 0x80000000-0x%08X",
           nor_size ? 0x80000000 + nor_size - 1 : 0);
  add_line("Add-in code window: 0x70000080-0x7001FFFF");
  add_line("Add-in RAM window: 0x7400004C-0x74007FFF");
  add_line("Main VRAM: 0xAC200000, 337920 bytes, RGB565");
  add_line("Main LCD: 528x320, MMIO 0xB4000000");
  add_line("Sub LCD: 240x96 touch; controller not mapped yet");
  add_line("Keyboard matrix: 0xA44B0000");
  add_line("");

  add_line("[Storage]");
  add_line("Internal status: total=%d free=%d",
           internal_total_status, internal_free_status);
  add_line("Internal total: %u bytes (%u MiB)",
           (unsigned int)internal_total,
           (unsigned int)(internal_total / 1048576));
  add_line("Internal free: %u bytes (%u MiB)",
           (unsigned int)internal_free,
           (unsigned int)(internal_free / 1048576));
  add_line("Internal used: %u bytes (%u MiB)",
           (unsigned int)(internal_total - internal_free),
           (unsigned int)((internal_total - internal_free) / 1048576));
  add_line("microSD status: total=%d free=%d",
           card_total_status, card_free_status);
  add_line("microSD total: %u bytes (%u MiB)",
           (unsigned int)card_total,
           (unsigned int)(card_total / 1048576));
  add_line("microSD free: %u bytes (%u MiB)",
           (unsigned int)card_free,
           (unsigned int)(card_free / 1048576));
  add_line("microSD used: %u bytes (%u MiB)",
           (unsigned int)(card_total - card_free),
           (unsigned int)((card_total - card_free) / 1048576));
  add_line("Text report: %s", report_path);
  add_line("");

  add_line("[Interfaces and notes]");
  add_line("USB protocol device: 07CF:6101 (EX-word link)");
  add_line("OS: proprietary Casio firmware with syscall gateway");
  add_line("All values collected using read-only register access");
  add_line("Unknown fields are retained as raw hex for analysis");

  report_length = make_report();
  save_status = save_report(report_path, report_length);
  page_count = (line_count + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
  render_page(page, page_count, save_status);

  while (1) {
    keypad_read();
    if (get_key_state(KEY_POWER) || get_key_state(KEY_BACK)) return -2;
    if (get_key_pressed(KEY_RIGHT) || get_key_pressed(KEY_PAGE_DOWN) ||
        get_key_pressed(KEY_DOWN)) {
      page = (page + 1) % page_count;
      render_page(page, page_count, save_status);
    }
    if (get_key_pressed(KEY_LEFT) || get_key_pressed(KEY_PAGE_UP) ||
        get_key_pressed(KEY_UP)) {
      page = page == 0 ? page_count - 1 : page - 1;
      render_page(page, page_count, save_status);
    }
    for (delay = 0; delay < 1000; ++delay) { }
  }
}
