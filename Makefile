CFLAGS ?= -O2 -Wall
USB    := $(shell pkg-config --cflags --libs libusb-1.0)
MINGW  ?= x86_64-w64-mingw32-gcc

all: tools/probe tools/capture

tools/probe: tools/probe.c
	$(CC) $(CFLAGS) $< -o $@ $(USB)

tools/capture: tools/capture.c
	$(CC) $(CFLAGS) $< -o $@ -lm $(USB)

# Windows-side harness for the vendor matcher (needs mingw-w64 + wine)
harness: engine-harness/bin/query.exe engine-harness/bin/init.exe
engine-harness/bin/%.exe: engine-harness/%.c engine-harness/common.h
	@mkdir -p engine-harness/bin
	$(MINGW) -O1 -Wall -Wno-unused-function $< -o $@

clean:
	rm -rf tools/probe tools/capture engine-harness/bin

.PHONY: all harness clean
