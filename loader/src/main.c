#include <stdint.h>

#include <graphics/color.h>
#include <graphics/drawing.h>
#include <graphics/lcdc.h>
#include <graphics/text.h>
#include <sh4a/input/keypad.h>
#include <syscalls/syscalls.h>

#define SCREEN_WIDTH 528
#define SCREEN_HEIGHT 320

#define PAYLOAD_HEADER_SIZE 32u
#define PAYLOAD_P2_START 0xac800000u
#define PAYLOAD_P2_END   0xad000000u
#define PAYLOAD_FLAG_EXECUTABLE 0x00000001u
#define PAYLOAD_FLAG_RETURNS    0x00000002u
#define PAYLOAD_COOKIE          0x45585731u

struct payload_header {
  char magic[8];
  uint32_t header_size;
  uint32_t load_address;
  uint32_t entry_address;
  uint32_t payload_size;
  uint32_t payload_crc32;
  uint32_t flags;
};

enum load_status {
  LOAD_OK = 0,
  LOAD_NOT_FOUND,
  LOAD_BAD_HEADER,
  LOAD_BAD_RANGE,
  LOAD_BAD_SIZE,
  LOAD_READ_ERROR,
  LOAD_BAD_CRC
};

static struct payload_header header;
static uint32_t measured_crc;
static int source_is_card;

static int bytes_equal(const char *left, const char *right,
                       unsigned int count) {
  unsigned int i;
  for (i = 0; i < count; ++i)
    if (left[i] != right[i]) return 0;
  return 1;
}

static void hex32(char output[11], uint32_t value) {
  static const char digits[] = "0123456789ABCDEF";
  unsigned int i;
  output[0] = '0';
  output[1] = 'x';
  for (i = 0; i < 8; ++i)
    output[2 + i] = digits[(value >> (28 - i * 4)) & 15];
  output[10] = 0;
}

static void decimal(char output[11], uint32_t value) {
  char reverse[10];
  unsigned int used = 0;
  unsigned int i;
  if (value == 0) {
    output[0] = '0';
    output[1] = 0;
    return;
  }
  while (value != 0 && used < sizeof(reverse)) {
    reverse[used++] = (char)('0' + value % 10);
    value /= 10;
  }
  for (i = 0; i < used; ++i) output[i] = reverse[used - i - 1];
  output[used] = 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             unsigned int size) {
  unsigned int i;
  unsigned int bit;
  for (i = 0; i < size; ++i) {
    crc ^= data[i];
    for (bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1));
  }
  return crc;
}

static const char *status_text(enum load_status status) {
  switch (status) {
    case LOAD_OK: return "Payload verified and ready";
    case LOAD_NOT_FOUND: return "linux.pay not found on internal storage or card";
    case LOAD_BAD_HEADER: return "Payload header is invalid";
    case LOAD_BAD_RANGE: return "Payload requests memory outside the safe P2 pool";
    case LOAD_BAD_SIZE: return "Payload size does not match the file";
    case LOAD_READ_ERROR: return "Storage read failed before payload completed";
    case LOAD_BAD_CRC: return "CRC mismatch: payload will not be launched";
  }
  return "Unknown loader error";
}

static void label_value(unsigned int y, const char *label,
                        const char *value) {
  set_pen(create_rgb16(137, 164, 196));
  render_text(18, y, label);
  set_pen(create_rgb16(226, 232, 240));
  render_text(190, y, value);
}

static void render_startup_screen(void) {
  set_pen(create_rgb16(6, 12, 22));
  draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  set_pen(create_rgb16(22, 46, 74));
  draw_rect(0, 0, SCREEN_WIDTH, 34);
  set_pen(create_rgb16(116, 214, 255));
  render_text(12, 10, "EX-WORD LINUX LOADER - STARTUP CHECK");

  set_pen(create_rgb16(86, 235, 176));
  render_text(18, 68, "Loader code started successfully");
  set_pen(create_rgb16(226, 232, 240));
  render_text(18, 104, "No payload file or staging RAM has been touched");
  render_text(18, 136, "Release ENTER, then press ENTER to inspect linux.pay");

  set_pen(create_rgb16(22, 46, 74));
  draw_rect(0, 286, SCREEN_WIDTH, 34);
  set_pen(create_rgb16(226, 232, 240));
  render_text(12, 298, "BACK: return to Casio   No firmware is written");
  lcdc_copy_vram();
}

static void render_screen(enum load_status status, int armed,
                          const char *last_result) {
  char value[11];

  set_pen(create_rgb16(6, 12, 22));
  draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
  set_pen(create_rgb16(22, 46, 74));
  draw_rect(0, 0, SCREEN_WIDTH, 34);
  set_pen(create_rgb16(116, 214, 255));
  render_text(12, 10, "EX-WORD LINUX LOADER - REVERSIBLE TEST");

  set_pen(status == LOAD_OK ? create_rgb16(86, 235, 176)
                            : create_rgb16(255, 150, 120));
  render_text(18, 54, status_text(status));

  if (status == LOAD_OK) {
    label_value(88, "Source", source_is_card ? "microSD" : "internal storage");
    hex32(value, header.load_address);
    label_value(108, "Load address", value);
    hex32(value, header.entry_address);
    label_value(128, "Entry address", value);
    decimal(value, header.payload_size);
    label_value(148, "Payload bytes", value);
    hex32(value, measured_crc);
    label_value(168, "Measured CRC32", value);
    hex32(value, header.flags);
    label_value(188, "Flags", value);

    if (last_result != 0) {
      set_pen(create_rgb16(86, 235, 176));
      render_text(18, 220, last_result);
    } else if (armed) {
      set_pen(create_rgb16(255, 193, 92));
      render_text(18, 220, "ARMED: press ENTER again to transfer control");
    } else {
      set_pen(create_rgb16(226, 232, 240));
      render_text(18, 220, "Press ENTER to arm the payload");
    }
  }

  set_pen(create_rgb16(22, 46, 74));
  draw_rect(0, 286, SCREEN_WIDTH, 34);
  set_pen(create_rgb16(226, 232, 240));
  render_text(12, 298, "BACK: return to Casio   No firmware is written");
  lcdc_copy_vram();
}

static int open_payload(void) {
  int descriptor = sys_open("\\\\drv0\\CASIOTXT\\linux.pay", FILE_RD);
  source_is_card = 0;
  if (descriptor < 0)
    descriptor = sys_open("\\\\drv0\\CASIOTXT\\LINUX.PAY", FILE_RD);
  if (descriptor < 0)
    descriptor = sys_open("\\\\drv0\\linux.pay", FILE_RD);
  if (descriptor < 0)
    descriptor = sys_open("\\\\drv0\\LINUX.PAY", FILE_RD);
  if (descriptor >= 0) return descriptor;

  source_is_card = 1;
  descriptor = sys_open("\\\\crd0\\CASIOTXT\\linux.pay", FILE_RD);
  if (descriptor < 0)
    descriptor = sys_open("\\\\crd0\\CASIOTXT\\LINUX.PAY", FILE_RD);
  if (descriptor < 0)
    descriptor = sys_open("\\\\crd0\\linux.pay", FILE_RD);
  if (descriptor < 0)
    descriptor = sys_open("\\\\crd0\\LINUX.PAY", FILE_RD);
  return descriptor;
}

static enum load_status load_payload(void) {
  static const char expected_magic[8] = {'E','X','W','P','A','Y','1',0};
  int descriptor;
  int file_size;
  int count;
  uint32_t done = 0;
  uint32_t crc = 0xffffffffu;

  descriptor = open_payload();
  if (descriptor < 0) return LOAD_NOT_FOUND;
  file_size = sys_get_filesize(descriptor);
  count = sys_read(descriptor, &header, sizeof(header));
  if (count != (int)sizeof(header)) {
    sys_close(descriptor);
    return LOAD_BAD_HEADER;
  }
  if (!bytes_equal(header.magic, expected_magic, sizeof(expected_magic)) ||
      header.header_size != PAYLOAD_HEADER_SIZE ||
      !(header.flags & PAYLOAD_FLAG_EXECUTABLE)) {
    sys_close(descriptor);
    return LOAD_BAD_HEADER;
  }
  if (header.load_address < PAYLOAD_P2_START ||
      header.load_address >= PAYLOAD_P2_END ||
      header.payload_size == 0 ||
      header.payload_size > PAYLOAD_P2_END - header.load_address ||
      header.entry_address < header.load_address ||
      header.entry_address >= header.load_address + header.payload_size) {
    sys_close(descriptor);
    return LOAD_BAD_RANGE;
  }
  if (file_size != (int)(PAYLOAD_HEADER_SIZE + header.payload_size)) {
    sys_close(descriptor);
    return LOAD_BAD_SIZE;
  }

  while (done < header.payload_size) {
    uint32_t remaining = header.payload_size - done;
    unsigned int request = remaining > 65536u ? 65536u : remaining;
    uint8_t *destination = (uint8_t *)(header.load_address + done);
    count = sys_read(descriptor, destination, request);
    if (count <= 0) {
      sys_close(descriptor);
      return LOAD_READ_ERROR;
    }
    crc = crc32_update(crc, destination, (unsigned int)count);
    done += (uint32_t)count;
  }
  sys_close(descriptor);
  measured_crc = crc ^ 0xffffffffu;
  if (measured_crc != header.payload_crc32) return LOAD_BAD_CRC;
  return LOAD_OK;
}

static uint32_t execute_payload(void) {
  uint32_t (*entry)(uint32_t) =
      (uint32_t (*)(uint32_t))header.entry_address;
  return entry(PAYLOAD_COOKIE);
}

int main(int entry_context, char **unused_argument) {
  enum load_status status;
  int armed = 0;
  int controls_released = 0;
  volatile unsigned int delay;
  (void)entry_context;
  (void)unused_argument;

  render_startup_screen();
  while (1) {
    keypad_read();
    if (!get_key_state(KEY_POWER) && !get_key_state(KEY_BACK) &&
        !get_key_state(KEY_ENTER))
      controls_released = 1;
    if (controls_released && get_key_pressed(KEY_ENTER)) break;
    if (controls_released &&
        (get_key_pressed(KEY_POWER) || get_key_pressed(KEY_BACK)))
      return -2;
    for (delay = 0; delay < 1000; ++delay) { }
  }

  status = load_payload();
  render_screen(status, armed, 0);
  while (1) {
    keypad_read();
    if (get_key_state(KEY_POWER) || get_key_state(KEY_BACK)) return -2;
    if (status == LOAD_OK && get_key_pressed(KEY_ENTER)) {
      if (!armed) {
        armed = 1;
        render_screen(status, armed, 0);
      } else {
        uint32_t result = execute_payload();
        armed = 0;
        if (header.flags & PAYLOAD_FLAG_RETURNS) {
          (void)result;
          render_screen(status, armed, "P2 payload executed and returned successfully");
        } else {
          render_screen(status, armed, "Payload unexpectedly returned to the loader");
        }
      }
    }
    for (delay = 0; delay < 1000; ++delay) { }
  }
}
