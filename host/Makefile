# Host build for macOS.
#
# This target compiles the SAME portable component sources the firmware compiles,
# in the same files, against host/include instead of ESP-IDF. It may add files (the
# shim, the host main, the test mains); it may not fork a component. The app/
# components (the screen display, and later the entry policy and the board adapter)
# are firmware-only; host/tests/layering.sh H3 checks that none of them is compiled
# here. There is no target conditional compilation anywhere in components/ - the shim
# is the only thing that knows it is a Mac.
#
#   make -C host            build the runnable server
#   make -C host test       build and run the unit tests (no board, no network)
#   make -C host integration run the HTTP acceptance tests against the server
#   make -C host check      both of the above
#   make -C host tools      build the measurement/one-shot tools
#   make -C host fixtures   (re)generate the fixture corpus with ffmpeg
#   make -C host bench      host encoder-cost baseline (T11h)

HERE  := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(HERE)/..
BUILD ?= $(HERE)/build
OBJ   := $(BUILD)/obj
BIN   := $(BUILD)/bin

CC     ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-gnu-zero-variadic-macro-arguments
ifeq ($(XTAPE_MCP_CONTROL),1)
CFLAGS += -DXTAPE_MCP_CONTROL=1
endif
LDLIBS := -lpthread -lm

INC := -I$(HERE)/include \
       -I$(ROOT)/components/portable/http_server/include \
       -I$(ROOT)/components/portable/audio_format/include \
       -I$(ROOT)/components/portable/library/include \
       -I$(ROOT)/components/portable/library_http/include \
       -I$(ROOT)/components/portable/mcp/include \
       -I$(ROOT)/components/portable/mini_icecast/include \
       -I$(ROOT)/components/portable/tone_eq/include \
       -I$(ROOT)/components/portable/mini_files/include \
       -I$(ROOT)/components/portable/cassette/include \
       -I$(ROOT)/components/portable/artwork/include \
       -I$(ROOT)/components/portable/menu/include \
       -I$(ROOT)/components/portable/knob_model/include \
       -I$(ROOT)/components/portable/controls/include \
       -I$(ROOT)/components/portable/groups/include \
       -I$(ROOT)/components/portable/rgb/include \
       -I$(ROOT)/components/portable/snapshot/include \
       -I$(ROOT)/components/portable/app_services/include \
       -I$(ROOT)/components/portable/app_board/include \
       -I$(ROOT)/components/portable/dr_libs/include \
       -I$(ROOT)/components/portable/shine/src/lib \
       -I$(HERE) \
       -I$(HERE)/tests

SHINE_SRC := $(wildcard $(ROOT)/components/portable/shine/src/lib/*.c)
SHINE_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(notdir $(SHINE_SRC)))

# The production sources. The firmware target compiles these portable files too.
PROD_SRC := \
  $(ROOT)/components/portable/http_server/http_server.c \
  $(ROOT)/components/portable/http_server/http_parse.c \
  $(ROOT)/components/portable/audio_format/audio_format.c \
  $(ROOT)/components/portable/library/library.c \
  $(ROOT)/components/portable/library_http/library_http.c \
  $(ROOT)/components/portable/mcp/mcp.c \
  $(ROOT)/components/portable/mini_icecast/mini_icecast.c \
  $(ROOT)/components/portable/mini_icecast/mi_station.c \
  $(ROOT)/components/portable/mini_icecast/mi_burst.c \
  $(ROOT)/components/portable/mini_icecast/mi_icy.c \
  $(ROOT)/components/portable/mini_icecast/mi_ring.c \
  $(ROOT)/components/portable/mini_icecast/mi_encode.c \
  $(ROOT)/components/portable/mini_icecast/mi_decode.c \
  $(ROOT)/components/portable/mini_icecast/mi_resample.c \
  $(ROOT)/components/portable/tone_eq/tone_eq.c \
  $(ROOT)/components/portable/mini_files/mini_files.c \
  $(ROOT)/components/portable/cassette/cassette.c \
  $(ROOT)/components/portable/cassette/cassette_layout.c \
  $(ROOT)/components/portable/cassette/cassette_button_rule.c \
  $(ROOT)/components/portable/cassette/timeline.c \
  $(ROOT)/components/portable/cassette/entrance.c \
  $(ROOT)/components/portable/artwork/artwork.c \
  $(ROOT)/components/portable/artwork/id3.c \
  $(ROOT)/components/portable/artwork/flac.c \
  $(ROOT)/components/portable/artwork/tags.c \
  $(ROOT)/components/portable/menu/menu.c \
  $(ROOT)/components/portable/menu/pad.c \
  $(ROOT)/components/portable/knob_model/knob.c \
  $(ROOT)/components/portable/controls/controls.c \
  $(ROOT)/components/portable/groups/groups.c \
  $(ROOT)/components/portable/rgb/rgb.c \
  $(ROOT)/components/portable/mini_icecast/mi_shuffle.c \
  $(ROOT)/components/portable/snapshot/snapshot.c \
  $(ROOT)/components/portable/app_services/app_services.c \
  $(SHINE_SRC)

vpath %.c $(ROOT)/components/portable/http_server \
          $(ROOT)/components/portable/audio_format \
          $(ROOT)/components/portable/library \
          $(ROOT)/components/portable/library_http \
          $(ROOT)/components/portable/mcp \
          $(ROOT)/components/portable/mini_icecast \
          $(ROOT)/components/portable/tone_eq \
          $(ROOT)/components/portable/mini_files \
          $(ROOT)/components/portable/cassette \
          $(ROOT)/components/portable/artwork \
          $(ROOT)/components/portable/menu \
          $(ROOT)/components/portable/knob_model \
          $(ROOT)/components/portable/controls \
          $(ROOT)/components/portable/groups \
          $(ROOT)/components/portable/rgb \
          $(ROOT)/components/portable/snapshot \
          $(ROOT)/components/portable/app_services \
          $(ROOT)/components/portable/shine/src/lib \
          $(HERE) $(HERE)/shim $(HERE)/tools $(HERE)/tests

CORE_OBJ := $(OBJ)/audio_format.o $(OBJ)/library.o $(OBJ)/mi_encode.o $(OBJ)/mi_decode.o $(OBJ)/mi_ring.o $(OBJ)/mi_resample.o $(OBJ)/mi_station.o $(OBJ)/tone_eq.o $(SHINE_OBJ) $(OBJ)/shim.o
BURST_OBJ := $(OBJ)/mi_burst.o $(OBJ)/mi_icy.o
CASSETTE_OBJ := $(OBJ)/cassette.o $(OBJ)/cassette_layout.o $(OBJ)/timeline.o
ARTWORK_OBJ := $(OBJ)/artwork.o $(OBJ)/id3.o $(OBJ)/flac.o $(OBJ)/tags.o
MENU_OBJ := $(OBJ)/menu.o
SVC_OBJ := $(OBJ)/app_services.o $(OBJ)/library_http.o $(OBJ)/mcp.o $(OBJ)/snapshot.o
HTTP_OBJ := $(OBJ)/http_server.o $(OBJ)/http_parse.o $(OBJ)/mini_icecast.o $(OBJ)/mi_shuffle.o $(OBJ)/mini_files.o $(BURST_OBJ)
SERVER   := $(BIN)/icecast_host
SERVER_OBJ := $(CORE_OBJ) $(HTTP_OBJ) $(SVC_OBJ) $(OBJ)/board_host.o $(OBJ)/icecast_host.o

TOOLS := $(BIN)/mi_bench $(BIN)/mi_encode_file $(BIN)/mi_frames
# The harness helpers are Python; override PYTHON where the default python3 is not the
# one with the packages the tests need.
PYTHON ?= python3

TESTS := $(BIN)/t01_shine_behaviour $(BIN)/t02_encoder_pipeline \
         $(BIN)/t03_resampler_guard $(BIN)/t04_format_validation \
         $(BIN)/t05_stream_formats $(BIN)/t06_status $(BIN)/t07_burst_icy \
         $(BIN)/t08_http_parse $(BIN)/t10_cassette $(BIN)/t11_ring \
         $(BIN)/t12_transport $(BIN)/t14_library $(BIN)/t15_artwork \
         $(BIN)/t16_layout $(BIN)/t17_tone_eq \
         $(BIN)/t18_menu $(BIN)/t19_shuffle \
         $(BIN)/t21_button $(BIN)/t22_knob $(BIN)/t23_pad $(BIN)/t24_controls \
         $(BIN)/t25_rgb $(BIN)/t26_groups $(BIN)/t27_timeline $(BIN)/t28_entrance \
         $(BIN)/t29_snapshot

.PHONY: all test integration layering check tools fixtures bench control control-check clean help

all: $(SERVER)

# A separate object tree, so the default binary and the control build can coexist.
control:
	$(MAKE) -C $(HERE) BUILD=$(BUILD)/control XTAPE_MCP_CONTROL=1 all

control-check: control
	CONTROL_BIN=$(BUILD)/control/bin/icecast_host sh $(HERE)/tests/control_tests.sh

# Vendored encoder: compiled unmodified, so its warnings are not ours to fix.
$(SHINE_OBJ): CFLAGS += -w

$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -MMD -MP $(INC) -c $< -o $@

# Header dependencies: a change to artwork.h or mini_icecast.h has to rebuild every
# object that includes it. Without this, a struct's layout changed in a header while a
# stale .o kept the old one, and the test linked two disagreeing views of the same type.
-include $(wildcard $(OBJ)/*.d)

$(SERVER): $(SERVER_OBJ)
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/mi_bench: $(CORE_OBJ) $(OBJ)/mi_bench.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/mi_encode_file: $(CORE_OBJ) $(OBJ)/mi_encode_file.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/mi_frames: $(OBJ)/audio_format.o $(OBJ)/mi_decode.o $(OBJ)/mi_ring.o $(OBJ)/shim.o $(SHINE_OBJ) $(OBJ)/mi_frames.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t01_shine_behaviour: $(CORE_OBJ) $(OBJ)/t01_shine_behaviour.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t02_encoder_pipeline: $(CORE_OBJ) $(OBJ)/t02_encoder_pipeline.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t03_resampler_guard: $(CORE_OBJ) $(OBJ)/t03_resampler_guard.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t04_format_validation: $(CORE_OBJ) $(HTTP_OBJ) $(OBJ)/t04_format_validation.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t05_stream_formats: $(CORE_OBJ) $(OBJ)/t05_stream_formats.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN)/t06_status: $(CORE_OBJ) $(HTTP_OBJ) $(OBJ)/t06_status.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t07_burst_icy: $(CORE_OBJ) $(BURST_OBJ) $(OBJ)/t07_burst_icy.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t08_http_parse: $(OBJ)/http_parse.o $(OBJ)/t08_http_parse.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t10_cassette: $(CORE_OBJ) $(CASSETTE_OBJ) $(OBJ)/t10_cassette.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t11_ring: $(CORE_OBJ) $(OBJ)/t11_ring.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t12_transport: $(CORE_OBJ) $(HTTP_OBJ) $(OBJ)/t12_transport.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t14_library: $(OBJ)/library.o $(OBJ)/audio_format.o $(OBJ)/shim.o $(OBJ)/t14_library.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t15_artwork: $(ARTWORK_OBJ) $(OBJ)/audio_format.o $(OBJ)/t15_artwork.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t16_layout: $(OBJ)/cassette_layout.o $(OBJ)/t16_layout.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t17_tone_eq: $(OBJ)/tone_eq.o $(OBJ)/t17_tone_eq.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t18_menu: $(MENU_OBJ) $(CASSETTE_OBJ) $(OBJ)/library.o $(OBJ)/audio_format.o $(OBJ)/shim.o $(OBJ)/t18_menu.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t19_shuffle: $(OBJ)/mi_shuffle.o $(OBJ)/t19_shuffle.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t21_button: $(OBJ)/cassette_button_rule.o $(OBJ)/t21_button.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t22_knob: $(OBJ)/knob.o $(OBJ)/t22_knob.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t23_pad: $(OBJ)/pad.o $(OBJ)/t23_pad.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t24_controls: $(OBJ)/controls.o $(OBJ)/t24_controls.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t25_rgb: $(OBJ)/rgb.o $(OBJ)/t25_rgb.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t26_groups: $(OBJ)/groups.o $(OBJ)/t26_groups.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t27_timeline: $(OBJ)/timeline.o $(OBJ)/t27_timeline.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t29_snapshot: $(OBJ)/snapshot.o $(OBJ)/t29_snapshot.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t28_entrance: $(OBJ)/entrance.o $(OBJ)/t28_entrance.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

tools: $(TOOLS)

test: $(TESTS)
	@echo "--- unit tests"
	@for t in $(TESTS); do printf '%-40s' "$$(basename $$t)"; if $$t >/tmp/host_test.log 2>&1; then echo "ok"; else echo "FAILED"; sed 's/^/    /' /tmp/host_test.log; exit 1; fi; done
	@echo "all unit tests passed"

integration: $(SERVER)
	@HOST_BIN=$(SERVER) HOST_DIR=$(HERE) sh $(HERE)/tests/http_tests.sh
	@echo "--- awkward file names (a real library is not called tone-44100.flac)"
	@$(PYTHON) $(HERE)/tests/hostile_names.py $(SERVER) $(HERE)/fixtures/tone-44100.flac
	@echo "--- the snapshot over MCP"
	@$(PYTHON) $(HERE)/tests/snapshot_tests.py $(SERVER) $(HERE)/fixtures

layering:
	@ROOT=$(ROOT) sh $(HERE)/tests/layering.sh

check: test layering integration

fixtures:
	@$(MAKE) -C $(HERE)/fixtures

bench: $(BIN)/mi_bench
	@$(BIN)/mi_bench 1000000

clean:
	rm -rf $(BUILD)

help:
	@sed -n '2,20p' $(lastword $(MAKEFILE_LIST))
