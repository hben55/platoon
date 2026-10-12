CFLAGS ?= -std=c11 -Wall -Wextra -O2

RAYLIB_VERSION = 5.5
RAYLIB_DIR = vendor/raylib
RAYLIB_LIB = $(RAYLIB_DIR)/src/libraylib.a
GUI_LIBS = $(RAYLIB_LIB) -lGL -lm -lpthread -ldl -lrt -lX11

all: platoon platoon-gui

platoon: src/main.c src/platoon.c src/platoon.h
	$(CC) $(CFLAGS) -o $@ src/main.c src/platoon.c

platoon-gui: src/gui.c src/gui_draw.c src/gui_draw.h src/gui_input.c src/gui_input.h src/platoon.c src/platoon.h $(RAYLIB_LIB)
	$(CC) $(CFLAGS) -I$(RAYLIB_DIR)/src -o $@ src/gui.c src/gui_draw.c src/gui_input.c src/platoon.c $(GUI_LIBS)

# raylib is fetched and built on first use; it lives in vendor/ (not committed).
$(RAYLIB_LIB):
	test -d $(RAYLIB_DIR) || git clone --depth 1 --branch $(RAYLIB_VERSION) https://github.com/raysan5/raylib.git $(RAYLIB_DIR)
	$(MAKE) -C $(RAYLIB_DIR)/src PLATFORM=PLATFORM_DESKTOP

test: src/test.c src/platoon.c src/platoon.h
	$(CC) $(CFLAGS) -o test_platoon src/test.c src/platoon.c && ./test_platoon

clean:
	rm -f platoon platoon-gui test_platoon

distclean: clean
	rm -rf vendor

.PHONY: all test clean distclean
