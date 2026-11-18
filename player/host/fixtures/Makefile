# The player's own fixture corpus: bodies long enough to exercise the ring's buffer.
#
# Generated, not hand-assembled, and committed, because encoders differ between machines and the
# tests must see the same bytes everywhere. Not in host/fixtures because that corpus is a stand-in
# Library for the K7's tests, which count its files.
#
#   make -C player/host/fixtures
#
# 6 s at 64 kbps: the output primes 4.5 s before it plays (k7player.md R28), so a shorter body
# would never reach the target and would leave the tests asserting a silence they cannot explain.
# 64 kbps keeps that six seconds in 48 KB instead of 96.

FFMPEG ?= ffmpeg
DUR    := 6

OUT := long-44100.mp3 long-32000.mp3

.PHONY: all clean list
all: $(OUT)

long-44100.mp3:
	$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=$(DUR)" -ac 2 -c:a libmp3lame -b:a 64k $@

long-32000.mp3:
	$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=32000:duration=$(DUR)" -ac 2 -c:a libmp3lame -b:a 64k $@

list:
	@echo $(OUT)

clean:
	rm -f $(OUT)
