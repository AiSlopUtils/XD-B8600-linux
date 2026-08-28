.SUFFIXES:

ifeq ($(strip $(DEVKITSH4)),)
$(error "Please set DEVKITSH4 in your environment. export DEVKITSH4=<path to sdk>")
endif
include $(DEVKITSH4)/exword_rules

TARGET       := lnx03
MODNAME      := lnxldr
APPTITLE     := Linux Loader Startup Test
APPID        := LNX03
APPMOD       := lnx03.d01

SOURCEDIR    := src
HTMLDIR      := html
BUILDS       := ja cn
CFILES       := $(wildcard $(SOURCEDIR)/*.c)
SFILES       := $(wildcard $(SOURCEDIR)/*.s)
OBJECTS      := $(CFILES:.c=.o) $(SFILES:.s=.o)

LDFLAGS      := -Wall -nostdlib -L$(DEVKITPRO)/libdataplus/lib -ldataplus -lgraphics -lsh4a
CFLAGS       := -Wall -std=gnu17 -ffreestanding -fno-builtin -fno-strict-aliasing -I$(DEVKITPRO)/libdataplus/include -Os
ASFLAGS      := -Wall -m4-nofpu

app: $(addprefix build/,$(addsuffix /$(APPID),$(BUILDS)))

$(TARGET).elf: $(OBJECTS)

.SECONDEXPANSION:
build/%/$(APPID): $(TARGET).d01 $$(wildcard $(HTMLDIR)/$$*/*.htm)
	@echo building $* version in $@...
	@mkdir -p $@
	@cp $(TARGET).d01 $@
	@for f in $(HTMLDIR)/$*/*.htm; do \
		sed -e 's/@APPTITLE/$(APPTITLE)/g' -e 's/@APPID/$(APPID)/g' -e 's/@APPMOD/$(APPMOD)/g' $$f > $@/$$(basename $$f); \
	done
	@touch $@/fileinfo.cji

clean:
	@rm -fr build $(OBJECTS) $(TARGET).elf $(TARGET).elf.map $(TARGET).d01
