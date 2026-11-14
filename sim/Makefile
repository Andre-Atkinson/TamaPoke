# Simulador de TamaPoke en el host: el TamaPoke.ino real + Arduino_GFX real + la placa
# simulada (simhw.h). Lo normal es llamarlo desde tools/sim.sh.
#   make -C sim -j3                 1.75C (por defecto)
#   make -C sim -j3 BOARD=175       1.75 original
#   make -C sim -j3 window          the interactive SDL2 window (build/<board>/tamapoke_window);
#                                   opt-in: needs sdl2-config, and nothing else here does
# ARDUINO_LIBS es la carpeta de librerias de arduino-cli (tools/sim.sh la busca).
BOARD        ?= 175c
ARDUINO_LIBS ?= $(HOME)/Documents/Arduino/libraries
CXX          ?= c++
CC           ?= cc
PYTHON       ?= python3

REPO  := ..
GFX   := $(ARDUINO_LIBS)/GFX_Library_for_Arduino/src
U8G2  := $(ARDUINO_LIBS)/U8g2/src
BUILD := build/$(BOARD)
BIN   := $(BUILD)/tamapoke_sim
WINBIN := $(BUILD)/tamapoke_window

ifeq ($(BOARD),175c)
BOARD_FLAG := -DBOARD_AMOLED_175C=1
else ifeq ($(BOARD),175)
BOARD_FLAG := -DBOARD_AMOLED_175C=0
else
$(error BOARD must be 175c or 175)
endif

DEFS     := $(BOARD_FLAG) -DHOST_SIM=1
INCLUDES := -Ishim -I$(REPO)/test/shim -I$(GFX) -I$(REPO)
# -funsigned-char: char es sin signo en el ESP32-S3 (Xtensa) y con signo en el Mac
# (arm64 de Apple, x86). Sin esto, el decodificador UTF-8 de Arduino_GFX
# (charBounds(char c) compara c >= 0xf0) mide mal el texto CJK y lo descentra.
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=gnu++17 -funsigned-char -MMD -MP $(DEFS) $(INCLUDES)
WARN     := -Wall -Wextra -Wno-unused-parameter
CFLAGS   ?= -O2
CFLAGS   += -funsigned-char

# el firmware tal cual (rtcbat.cpp y audio.cpp los sustituyen rtcbat_sim.cpp y el audio
# mudo de test/shim/shim.cpp)
FW_SRCS  := pet.cpp i18n.cpp sdmon.cpp screenlock.cpp bootbutton.cpp trainingcapture.cpp trainingtouch.cpp \
            serialcmd.cpp uitext.cpp scene.cpp gameui.cpp
# the fake board and the helpers both programs use (tamapoke_sim adds main.cpp)
COMMON_SRCS := simhw.cpp simfs.cpp rtcbat_sim.cpp script.cpp png.cpp boot.cpp
SIM_SRCS := main.cpp $(COMMON_SRCS)
# the window: only built by the `window` target, the only one that uses SDL
WIN_SRCS := interactive.cpp window.cpp main_window.cpp
GFX_SRCS := Arduino_GFX.cpp Arduino_G.cpp Arduino_DataBus.cpp canvas/Arduino_Canvas.cpp
# must list the fonts that uitext.cpp's CJK_FONT_JA and CJK_FONT_KO name
FONTS    := u8g2_font_unifont_t_japanese3 u8g2_font_unifont_t_korean2

# everything both programs link, without their main()
CORE_OBJS := $(BUILD)/sketch/TamaPoke_ino.o \
        $(addprefix $(BUILD)/fw/,$(FW_SRCS:.cpp=.o)) \
        $(addprefix $(BUILD)/sim/,$(COMMON_SRCS:.cpp=.o)) \
        $(BUILD)/shim/shim.o \
        $(addprefix $(BUILD)/gfx/,$(GFX_SRCS:.cpp=.o)) \
        $(BUILD)/fonts/u8g2_fonts.o
OBJS     := $(CORE_OBJS) $(BUILD)/sim/main.o
WIN_OBJS := $(addprefix $(BUILD)/win/,$(WIN_SRCS:.cpp=.o))

# SDL2 only for the window. Recursive variables: sdl2-config runs only when the window
# target is built, so a machine without SDL2 builds `all` as before. The headers go in
# with -isystem, so SDL cannot add warnings to ours; SDL2main is left out (window.h
# defines SDL_MAIN_HANDLED and window.cpp calls SDL_SetMainReady()). Without sdl2-config
# the shell's error is hidden: check-sdl says what is missing, once.
SDL_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null | sed 's/-I/-isystem /g')
SDL_LIBS   = $(shell sdl2-config --libs 2>/dev/null | sed 's/-lSDL2main//')

.PHONY: all window clean check-libs check-sdl
all: check-libs $(BIN)
window: check-libs check-sdl $(WINBIN)

check-libs:
	@test -f "$(GFX)/Arduino_GFX.h" || { echo "sim: GFX Library for Arduino not found in $(ARDUINO_LIBS) (set ARDUINO_LIBS)" >&2; exit 1; }
	@test -f "$(U8G2)/clib/u8g2_fonts.c" || { echo "sim: U8g2 not found in $(ARDUINO_LIBS) (set ARDUINO_LIBS)" >&2; exit 1; }

check-sdl:
	@sdl2-config --version >/dev/null 2>&1 || { echo "sim: SDL2 not found (no sdl2-config): the window needs it. Ask the owner before installing it." >&2; exit 1; }

# check-sdl first, also under -j, so a machine without SDL2 only sees its message
$(WIN_OBJS) $(WINBIN): | check-sdl

$(BIN): $(OBJS)
	$(CXX) -o $@ $(OBJS)

$(WINBIN): $(CORE_OBJS) $(WIN_OBJS)
	$(CXX) -o $@ $(CORE_OBJS) $(WIN_OBJS) $(SDL_LIBS)

# el .ino con los prototipos que genera arduino-cli (gen_sketch.py)
$(BUILD)/sketch/TamaPoke_ino.cpp: $(REPO)/TamaPoke.ino gen_sketch.py
	@mkdir -p $(dir $@)
	$(PYTHON) gen_sketch.py $(REPO)/TamaPoke.ino $@

$(BUILD)/sketch/TamaPoke_ino.o: $(BUILD)/sketch/TamaPoke_ino.cpp
	$(CXX) $(CXXFLAGS) $(WARN) -c -o $@ $<

$(BUILD)/fw/%.o: $(REPO)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(WARN) -c -o $@ $<

$(BUILD)/sim/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(WARN) -c -o $@ $<

$(BUILD)/win/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(WARN) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD)/shim/shim.o: $(REPO)/test/shim/shim.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(WARN) -c -o $@ $<

# libreria de terceros: sin avisos (no es codigo nuestro)
$(BUILD)/gfx/%.o: $(GFX)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -w -c -o $@ $<

$(BUILD)/fonts/u8g2_fonts.c: extract_fonts.py
	@mkdir -p $(dir $@)
	$(PYTHON) extract_fonts.py $(U8G2)/clib/u8g2_fonts.c $@ $(FONTS)

$(BUILD)/fonts/u8g2_fonts.o: $(BUILD)/fonts/u8g2_fonts.c
	$(CC) $(CFLAGS) -w -c -o $@ $<

clean:
	rm -rf build

-include $(OBJS:.o=.d) $(WIN_OBJS:.o=.d)
