# Fixture corpus for the host tests and for the board.
#
# Generated, not hand-assembled, so it is reproducible - but *committed*, because
# mp3/flac encoders differ between machines and versions and host and board are
# supposed to see exactly the same bytes (requirement H6).
#
#   make -C host fixtures
#
# The corpus covers all three source formats at three sample rates (two of which
# are not the station rate, so the resampler is exercised), one file shorter than
# a single encoder frame, and one whose frame count is deliberately not a
# multiple of 1152 to drive the end-of-track flush (T2b).

FFMPEG ?= ffmpeg
RATES  := 44100 48000 32000
DUR    := 0.6
TAIL_DUR := 0.777
TINY_DUR := 0.01

RATE_FILES := $(foreach r,$(RATES),tone-$(r).mp3 tone-$(r).flac tone-$(r).wav)
EXTRA      := tail-44100.flac tiny-44100.wav
OUT        := $(RATE_FILES) $(EXTRA)

.PHONY: all clean list
all: $(OUT)

define GEN
tone-$(1).mp3:
	$$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=$(1):duration=$$(DUR)" -ac 2 -c:a libmp3lame -b:a 192k $$@
tone-$(1).flac:
	$$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=$(1):duration=$$(DUR)" -ac 2 -c:a flac $$@
tone-$(1).wav:
	$$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=$(1):duration=$$(DUR)" -ac 2 -c:a pcm_s16le $$@
endef
$(foreach r,$(RATES),$(eval $(call GEN,$(r))))

tail-44100.flac:
	$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=330:sample_rate=44100:duration=$(TAIL_DUR)" -ac 2 -c:a flac $@

tiny-44100.wav:
	$(FFMPEG) -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=$(TINY_DUR)" -ac 2 -c:a pcm_s16le $@

list:
	@ls -l $(OUT)

clean:
	rm -f $(OUT)
