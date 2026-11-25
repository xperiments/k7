# Host build for the K7Player's portable core (macOS and Linux).
#
#   make -C player/host           build the tests
#   make -C player/host test      run the unit tests
#   make -C player/host layering  the source-shape checks
#   make -C player/host check     both
#
# It compiles the SAME player sources the firmware compiles, against the K7's host shim
# (../host/include, ../host/shim) rather than a second one: the shim is the only thing that
# knows it is a Mac, and a second copy of it would be a second thing to disagree. Nothing
# here forks a component, and the board adapter (components/board_player) is not compiled:
# it can only compile against ESP-IDF.

HERE   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
PLAYER := $(HERE)/..
ROOT   := $(PLAYER)/..
BUILD  ?= $(HERE)/build
OBJ    := $(BUILD)/obj
BIN    := $(BUILD)/bin

CC     ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
LDLIBS := -lpthread -lm

INC := -I$(PLAYER)/components/player/include \
       -I$(ROOT)/components/portable/handover/include \
       -I$(ROOT)/components/portable/espnow_stream/include \
       -I$(ROOT)/components/portable/player_update/include \
       -I$(ROOT)/components/portable/device_name/include \
       -I$(ROOT)/components/portable/tone_eq/include \
       -I$(ROOT)/components/portable/dr_libs/include \
       -I$(ROOT)/host/include \
       -I$(ROOT)/host/tests

# The production sources this harness compiles - the same files the firmware compiles. A
# source that is not named here is not tested, and layering.sh P2 fails the build.
CORE_SRC := $(PLAYER)/components/player/player_recv.c \
            $(ROOT)/components/portable/espnow_stream/espnow_stream.c \
            $(PLAYER)/components/player/player_status.c \
            $(PLAYER)/components/player/player_net.c \
            $(PLAYER)/components/player/player_ring.c \
            $(PLAYER)/components/player/player_mp3.c \
            $(PLAYER)/components/player/player_stream.c \
            $(PLAYER)/components/player/player_playout.c \
            $(PLAYER)/components/player/player_tone.c \
            $(ROOT)/components/portable/tone_eq/tone_eq.c \
            $(PLAYER)/components/player/player_light.c \
            $(PLAYER)/components/player/player_ota.c \
            $(ROOT)/components/portable/player_update/player_update.c
CORE_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(notdir $(CORE_SRC)))

# The shared handover module, the same files the firmware and the K7 compile.
HANDOVER_SRC := $(ROOT)/components/portable/handover/handover_frame.c \
                $(ROOT)/components/portable/handover/handover_key.c
HANDOVER_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(notdir $(HANDOVER_SRC)))

# The deck's name rules, shared with the K7 (device_name.h): the player resolves the name
# the record gave it, so it reads the same rule the K7 writes.
DEVICE_SRC := $(ROOT)/components/portable/device_name/device_name.c
DEVICE_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(notdir $(DEVICE_SRC)))

TESTS := $(BIN)/t01_status $(BIN)/t02_net $(BIN)/t04_output \
         $(BIN)/t05_light $(BIN)/t06_recv $(BIN)/t07_tone $(BIN)/t08_ota

.PHONY: all test layering check clean

all: $(TESTS)

$(BIN)/t01_status: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t01_status.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t02_net: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t02_net.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t04_output: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t04_output.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t06_recv: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t06_recv.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t07_tone: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t07_tone.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t08_ota: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t08_ota.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BIN)/t05_light: $(CORE_OBJ) $(HANDOVER_OBJ) $(DEVICE_OBJ) $(OBJ)/board_player_host.o $(OBJ)/shim.o $(OBJ)/t05_light.o
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@


$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -MMD -MP $(INC) -c $< -o $@

vpath %.c $(PLAYER)/components/player \
            $(ROOT)/components/portable/espnow_stream $(ROOT)/components/portable/tone_eq \
            $(ROOT)/components/portable/player_update \
            $(ROOT)/components/portable/handover $(ROOT)/components/portable/device_name \
            $(HERE) $(ROOT)/host/shim $(HERE)/tests
-include $(wildcard $(OBJ)/*.d)

test: $(TESTS)
	@echo "--- player unit tests"
	@for t in $(TESTS); do printf '%-40s' "$$(basename $$t)"; \
		if $$t >/tmp/player_test.log 2>&1; then echo "ok"; \
		else echo "FAILED"; sed 's/^/    /' /tmp/player_test.log; exit 1; fi; done
	@echo "all player unit tests passed"

layering:
	@ROOT=$(ROOT) PLAYER=$(PLAYER) sh $(HERE)/tests/layering.sh

check: test layering

clean:
	rm -rf $(BUILD)
