# Host-side measurements for tape_fx, out of tree on purpose: this is a probe,
# not part of the firmware. The numbers it produces are in
# docs/research/tape-fx-output-stream.md.
#
#   make            host cost bench (bench.c against the effect as written)
#   make opt        the same with the two per-frame sinf() calls replaced by a table
#   make v3         the same, plus a division-free saturator (the ~8 % variant)
#   make v4         v3 with a polynomial fitted to the original curve (transparent)
#   make compare    the null test: original vs v3 vs v4 on the same PCM
#   make probe      what the effect does to a sine: gain, tone, noise floor
#   make asm        ESP32-S3 instruction counts per stereo frame, for -Og..-Os
#
# The ESP32-S3 half needs the IDF toolchain; point S3 at a bin/ directory:
#   make asm S3=~/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin

CC      ?= cc
CFLAGS  ?= -O2 -std=gnu11
S3      ?= $(HOME)/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin
S3CC     = $(S3)/xtensa-esp32s3-elf-gcc
S3OD     = $(S3)/xtensa-esp32s3-elf-objdump
S3FLAGS  = -mlongcalls -fno-builtin-memcpy -fno-builtin-memset -std=gnu23

all: bench
bench: bench.c tape_fx.c tape_fx.h
	$(CC) $(CFLAGS) -o bench bench.c tape_fx.c -lm

opt: bench.c tape_fx_opt.c tape_fx.h
	$(CC) $(CFLAGS) -o bench_opt bench.c tape_fx_opt.c -lm

v3: bench.c tape_fx_v3.c tape_fx.h
	$(CC) $(CFLAGS) -o bench_v3 bench.c tape_fx_v3.c -lm

v4: bench.c tape_fx_v4.c tape_fx.h
	$(CC) $(CFLAGS) -o bench_v4 bench.c tape_fx_v4.c -lm

compare: compare.c wrap_orig.c wrap_tab.c wrap_cubic.c wrap_poly.c tape_fx.c tape_fx_opt.c tape_fx_v3.c tape_fx_v4.c tape_fx.h
	$(CC) $(CFLAGS) -o compare compare.c wrap_orig.c wrap_tab.c wrap_cubic.c wrap_poly.c -lm

probe: probe.c tape_fx.c tape_fx.h
	$(CC) $(CFLAGS) -o probe probe.c tape_fx.c -lm

asm:
	@for O in Og O2 O3 Os; do \
	  for src in tape_fx tape_fx_opt tape_fx_v3 tape_fx_v4; do \
	    $(S3CC) $(S3FLAGS) -$$O -c $$src.c -o $${src}_$$O.o; \
	  done; \
	done
	OD=$(S3OD) python3 analyze.py tape_fx_Og.o tape_fx_O2.o tape_fx_O3.o tape_fx_Os.o \
	                             tape_fx_opt_Og.o tape_fx_opt_O2.o tape_fx_opt_O3.o tape_fx_opt_Os.o \
	                             tape_fx_v3_Og.o tape_fx_v3_O2.o tape_fx_v3_O3.o \
	                             tape_fx_v4_O2.o tape_fx_v4_O3.o

clean:
	rm -f bench bench_opt bench_v3 bench_v4 compare probe asan* corner* thdtest *.o *.dis size

.PHONY: all bench opt v3 v4 compare probe asm clean
