DEVKITSH4 ?= /work/toolchain/devkitPro/devkitSH4
PREFIX := $(DEVKITSH4)/bin/sh-elf-
CC := $(PREFIX)gcc
LD := $(PREFIX)ld
OBJCOPY := $(PREFIX)objcopy

CFLAGS := -mb -m4-nofpu -mrenesas -ffreestanding -fno-builtin -fno-pic -Os -Wall
LDFLAGS := -EB -T payload.ld

all: display-test.pay return-test.pay

start.o: start.S
	$(CC) $(CFLAGS) -c $< -o $@

display-test.o: display-test.c
	$(CC) $(CFLAGS) -c $< -o $@

display-test.elf: start.o display-test.o payload.ld
	$(LD) $(LDFLAGS) start.o display-test.o -o $@

display-test.bin: display-test.elf
	$(OBJCOPY) -O binary $< $@

display-test.pay: display-test.bin pack_payload.py
	python3 pack_payload.py --returns $< $@

return-test.o: return-test.c
	$(CC) $(CFLAGS) -c $< -o $@

return-test.elf: start.o return-test.o payload.ld
	$(LD) $(LDFLAGS) start.o return-test.o -o $@

return-test.bin: return-test.elf
	$(OBJCOPY) -O binary $< $@

return-test.pay: return-test.bin pack_payload.py
	python3 pack_payload.py --returns $< $@

clean:
	rm -f *.o *.elf *.bin *.pay
