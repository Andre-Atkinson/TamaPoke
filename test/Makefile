# Tests de la logica del firmware en el PC (sin placa).
#   make        -> compila y ejecuta
#   make FILTER=evolve  -> solo los tests cuyo nombre contenga "evolve"
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
            -fno-omit-frame-pointer
LDFLAGS  ?=
# -funsigned-char: char is unsigned on the ESP32-S3 (Xtensa) and signed on the Mac
# (Apple arm64, x86), so without it a byte >= 0x80 (the PT accents) would behave
# differently here than on the board. It stays out of CXXFLAGS so that a CXXFLAGS
# given on the command line or in the environment keeps it; both recipes below
# use it, and host.char_is_unsigned_like_the_esp32s3 fails if it is ever dropped.
HOSTFLAGS := -funsigned-char
SRC_DIR  := ..
BUILD    := build
BIN      := $(BUILD)/tamapoke_tests

SOURCES := main.cpp shim/shim.cpp test_pet.cpp test_dex.cpp test_i18n.cpp test_blit.cpp test_batcurve.cpp test_power.cpp test_bootbutton.cpp test_screenlock.cpp test_trainingtouch.cpp test_touchlock.cpp test_touchgesture.cpp test_panelpower.cpp test_bedtime.cpp test_attention.cpp test_clockprompt.cpp test_clockset.cpp test_sim.cpp test_minigames.cpp test_settingslayout.cpp test_idleplan.cpp test_bubble.cpp test_owner.cpp test_weather.cpp test_skylayout.cpp test_playarea.cpp test_standby.cpp test_holiday.cpp test_batdiary.cpp test_ending.cpp test_sickness.cpp test_garden.cpp test_accessory.cpp test_walks.cpp \
           $(SRC_DIR)/pet.cpp $(SRC_DIR)/i18n.cpp $(SRC_DIR)/owner.cpp $(SRC_DIR)/holiday.cpp $(SRC_DIR)/batdiary.cpp $(SRC_DIR)/screenlock.cpp $(SRC_DIR)/trainingtouch.cpp \
           $(SRC_DIR)/sim/script.cpp $(SRC_DIR)/sim/png.cpp $(SRC_DIR)/sim/rtcbat_sim.cpp $(SRC_DIR)/sim/interactive.cpp

.PHONY: all run clean asan
all: run

# every repo-root header (bedtime.h, power.h, batcurve.h, blit.h...), so a
# header-only edit rebuilds
HEADERS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(SRC_DIR)/sim/*.h)

$(BIN): $(SOURCES) framework.h $(HEADERS) $(wildcard shim/*.h) Makefile
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(HOSTFLAGS) -I shim -o $@ $(SOURCES) $(LDFLAGS)

run: $(BIN)
	@./$(BIN) $(FILTER)

# misma suite bajo AddressSanitizer + UBSan: caza desbordes e indices malos
asan: CXXFLAGS += -fsanitize=address,undefined -fno-sanitize-recover=all
asan: BIN := $(BUILD)/tamapoke_tests_asan
asan:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(HOSTFLAGS) -I shim -o $(BUILD)/tamapoke_tests_asan $(SOURCES) $(LDFLAGS)
	@./$(BUILD)/tamapoke_tests_asan $(FILTER)

clean:
	rm -rf $(BUILD)
