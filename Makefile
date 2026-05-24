CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -I.
LDFLAGS = -lgdi32 -lpsapi -mwindows
SRCS    = src/main.c src/detector.c src/logger.c
SRC_MKR = src/gamemode_active.c
RES_RC  = resources.rc
RES_O   = resources.o

VERSION := $(file < version.txt)

ARCH    := $(shell $(CC) -dumpmachine)
ifneq (,$(findstring w64,$(ARCH)))
  WINARCH  := win64
  RESTGT   := pe-x86-64
else
  WINARCH  := win32
  RESTGT   := pe-i386
endif

TARGET    := dist/rainmeter-gamemode-activator-$(VERSION)-$(WINARCH).exe
TARGET_MK := dist/gamemode_active.exe
DISTDIR   := dist

.PHONY: all clean run

all: $(TARGET_MK) $(TARGET)

$(RES_O): $(RES_RC) resource.h assets/icons/app_icon.ico assets/icons/gamemode.ico assets/icons/sleep_mode.ico
	windres --target=$(RESTGT) -i $< -o $@

$(TARGET_MK): $(SRC_MKR) | $(DISTDIR)
	$(CC) -O2 -std=c11 -mwindows -o $@ $<

$(TARGET): $(SRCS) $(RES_O) version.txt | $(DISTDIR)
	$(CC) $(CFLAGS) -DVERSION=\"$(VERSION)\" -o $@ $(SRCS) $(RES_O) $(LDFLAGS)

$(DISTDIR):
	mkdir -p $@

clean:
	rm -f $(RES_O)
	rm -rf $(DISTDIR)

run: $(TARGET)
	./$(TARGET)
