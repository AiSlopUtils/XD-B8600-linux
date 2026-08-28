CROSS_COMPILE ?= sh4-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

CFLAGS := -mb -m4a-nofpu -Os -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-pic -ffunction-sections -fdata-sections \
	-Wall -Wextra
ASFLAGS := -mb -m4a-nofpu
LDFLAGS := -EB -z max-page-size=4096 --gc-sections -T init.ld
OBJECTS := start.o syscalls.o init.o

all: init

init: $(OBJECTS) init.ld
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) init
