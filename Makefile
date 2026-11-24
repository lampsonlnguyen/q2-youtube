SYSROOT ?= $(HOME)/.cache/q2/sysroot
Q2_CC ?= clang
Q2_LD ?= ld.lld
Q2_TARGET := --target=mipsel-linux-gnu --sysroot=$(SYSROOT) -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008
Q2_LOADER := /lib/ld-linux-mipsn8.so.1
HOST_CC ?= cc

.PHONY: all clean check
all: dist/youtube.zip

dist/q2-youtube.o: q2-youtube.c
	@test -f "$(SYSROOT)/lib/ld-linux-mipsn8.so.1" || { echo "Set SYSROOT to the Q2 Pod sysroot"; exit 1; }
	mkdir -p $(@D)
	$(Q2_CC) $(Q2_TARGET) -Os -Wall -Wextra -Werror -c $< -o $@

dist/q2-start.o: q2-start.S
	mkdir -p $(@D)
	$(Q2_CC) $(Q2_TARGET) -c $< -o $@

dist/.youtube/q2-youtube: dist/q2-start.o dist/q2-youtube.o
	mkdir -p $(@D)
	$(Q2_LD) -m elf32ltsmip -e __start --dynamic-linker $(Q2_LOADER) --image-base=0x400000 -z noexecstack --hash-style=sysv -s -o $@ $^ "$(SYSROOT)/lib/libc.so.6" "$(SYSROOT)$(Q2_LOADER)"

dist/youtube.zip: dist/.youtube/q2-youtube q2app run companion-url.example
	cp q2app run companion-url.example dist/.youtube/
	chmod +x dist/.youtube/run
	cd dist && zip -q youtube.zip .youtube/q2-youtube .youtube/q2app .youtube/run .youtube/companion-url.example

check:
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsyntax-only q2-youtube.c
	node --check companion.mjs

clean:
	rm -rf dist
