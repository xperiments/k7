# Host build for macOS.
#
# This target compiles the SAME component sources the firmware compiles, in the
# same files, against host/include instead of ESP-IDF. It may add files (the
# shim, the host main, the test mains); it may not fork a component. There is no
# target conditional compilation anywhere in components/ - the shim is the only
# thing that knows it is a Mac.
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
BUILD := $(HERE)/build
OBJ   := $(BUILD)/obj
BIN   := $(BUILD)/bin

CC     ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-gnu-zero-variadic-macro-arguments
LDLIBS := -lpthread -lm

INC := -I$(HERE)/include \
       -I$(ROOT)/components/http_server/include \
       -I$(ROOT)/components/mini_icecast/include \
       -I$(ROOT)/components/mini_files/include \
       -I$(ROOT)/components/dr_libs/include \
       -I$(ROOT)/components/shine/src/lib \
       -I$(HERE) \
       -I$(HERE)/tests

SHINE_SRC := $(wildcard $(ROOT)/components/shine/src/lib/*.c)
SHINE_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(notdir $(SHINE_SRC)))

# The production sources. The firmware target compiles exactly these files.
PROD_SRC := \
  $(ROOT)/components/http_server/http_server.c \
  $(ROOT)/components/mini_icecast/mini_icecast.c \
  $(ROOT)/components/mini_icecast/mi_encode.c \
  $(ROOT)/components/mini_icecast/mi_decode.c \
  $(ROOT)/components/mini_icecast/mi_resample.c \
  $(ROOT)/components/mini_files/mini_files.c \
  $(SHINE_SRC)

vpath %.c $(ROOT)/components/http_server \
          $(ROOT)/components/mini_icecast \
          $(ROOT)/components/mini_files \
          $(ROOT)/components/shine/src/lib \
          $(HERE) $(HERE)/shim $(HERE)/tools $(HERE)/tests

CORE_OBJ := $(OBJ)/mi_encode.o $(OBJ)/mi_decode.o $(OBJ)/mi_resample.o $(SHINE_OBJ) $(OBJ)/shim.o
HTTP_OBJ := $(OBJ)/http_server.o $(OBJ)/mini_icecast.o $(OBJ)/mini_files.o
SERVER   := $(BIN)/icecast_host
SERVER_OBJ := $(CORE_OBJ) $(HTTP_OBJ) $(OBJ)/icecast_host.o

TOOLS := $(BIN)/mi_bench $(BIN)/mi_encode_file $(BIN)/mi_frames
# The harness helpers are Python; override PYTHON where the default python3 is not the
# one with the packages the tests need.
PYTHON ?= python3

TESTS := $(BIN)/t01_shine_behaviour $(BIN)/t02_encoder_pipeline \
         $(BIN)/t03_resampler_guard $(BIN)/t04_format_validation \
         $(BIN)/t05_stream_formats $(BIN)/t06_status

.PHONY: all test integration layering check tools fixtures bench clean help

all: $(SERVER)

# Vendored encoder: compiled unmodified, so its warnings are not ours to fix.
$(SHINE_OBJ): CFLAGS += -w

$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(SERVER): $(SERVER_OBJ)
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/mi_bench: $(CORE_OBJ) $(OBJ)/mi_bench.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/mi_encode_file: $(CORE_OBJ) $(OBJ)/mi_encode_file.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/mi_frames: $(OBJ)/mi_decode.o $(OBJ)/shim.o $(SHINE_OBJ) $(OBJ)/mi_frames.o
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

$(BIN)/t04_format_validation: $(CORE_OBJ) $(OBJ)/t04_format_validation.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t05_stream_formats: $(CORE_OBJ) $(OBJ)/t05_stream_formats.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN)/t06_status: $(CORE_OBJ) $(HTTP_OBJ) $(OBJ)/t06_status.o
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
