CFLAGS ?= -std=c11 -Wall -Wextra -O2

RAYLIB_VERSION = 5.5
RAYLIB_DIR = vendor/raylib
RAYLIB_LIB = $(RAYLIB_DIR)/src/libraylib.a
GUI_LIBS = $(RAYLIB_LIB) -lGL -lm -lpthread -ldl -lrt -lX11

all: platoon platoon-gui

platoon: src/main.c src/platoon.c src/platoon.h
	$(CC) $(CFLAGS) -o $@ src/main.c src/platoon.c

GUI_SRC = src/gui.c src/gui_draw.c src/gui_input.c src/gui_audio.c src/gui_help.c src/platoon.c
GUI_HDR = src/gui_draw.h src/gui_input.h src/gui_audio.h src/gui_help.h src/platoon.h

platoon-gui: $(GUI_SRC) $(GUI_HDR) $(RAYLIB_LIB)
	$(CC) $(CFLAGS) -I$(RAYLIB_DIR)/src -I$(RAYLIB_DIR)/src/external/glfw/include -o $@ $(GUI_SRC) $(GUI_LIBS)

# raylib is fetched and built on first use; it lives in vendor/ (not committed).
$(RAYLIB_DIR)/src/raylib.h:
	git clone --depth 1 --branch $(RAYLIB_VERSION) https://github.com/raysan5/raylib.git $(RAYLIB_DIR)

$(RAYLIB_LIB): $(RAYLIB_DIR)/src/raylib.h
	$(MAKE) -C $(RAYLIB_DIR)/src PLATFORM=PLATFORM_DESKTOP

# ---- browser build: `make web` writes web/index.html (+ .js, .wasm, .data) ----
# The Emscripten SDK is fetched into vendor/emsdk on first use (a large download).
EMSDK_VERSION = 3.1.74
EMSDK_DIR = vendor/emsdk
EMSCRIPTEN = $(EMSDK_DIR)/upstream/emscripten
EMENV = EM_CONFIG=$(abspath $(EMSDK_DIR)/.emscripten)
RAYLIB_WEB_DIR = vendor/raylib-web
RAYLIB_WEB_LIB = $(RAYLIB_WEB_DIR)/libraylib.a
RAYLIB_MODULES = rcore rshapes rtextures rtext rmodels utils raudio

$(EMSCRIPTEN)/emcc:
	test -d $(EMSDK_DIR) || git clone --depth 1 https://github.com/emscripten-core/emsdk.git $(EMSDK_DIR)
	cd $(EMSDK_DIR) && ./emsdk install $(EMSDK_VERSION) && ./emsdk activate $(EMSDK_VERSION)

$(RAYLIB_WEB_LIB): $(EMSCRIPTEN)/emcc $(RAYLIB_DIR)/src/raylib.h
	mkdir -p $(RAYLIB_WEB_DIR)
	for m in $(RAYLIB_MODULES); do \
	  $(EMENV) $(EMSCRIPTEN)/emcc -c $(RAYLIB_DIR)/src/$$m.c -o $(RAYLIB_WEB_DIR)/$$m.o -Os \
	    -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2 -I$(RAYLIB_DIR)/src || exit 1; \
	done
	$(EMENV) $(EMSCRIPTEN)/emar rcs $@ $(RAYLIB_WEB_DIR)/*.o

web: web/index.html

web/index.html: $(GUI_SRC) $(GUI_HDR) src/web_shell.html $(RAYLIB_WEB_LIB)
	mkdir -p web
	$(EMENV) $(EMSCRIPTEN)/emcc -o $@ $(GUI_SRC) $(RAYLIB_WEB_LIB) -std=c11 -Wall -Os \
	  -DPLATFORM_WEB -I$(RAYLIB_DIR)/src -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 \
	  -sSTACK_SIZE=1048576 --preload-file assets/fonts@assets/fonts \
	  --shell-file src/web_shell.html

test: src/test.c src/platoon.c src/platoon.h
	$(CC) $(CFLAGS) -o test_platoon src/test.c src/platoon.c && ./test_platoon

# Publishes web/ to the gh-pages branch (served by GitHub Pages).
deploy: web
	rm -rf vendor/gh-pages
	git worktree prune
	git fetch origin gh-pages 2>/dev/null || true
	if git rev-parse --verify -q origin/gh-pages >/dev/null; then \
	  git worktree add -B gh-pages vendor/gh-pages origin/gh-pages; \
	else \
	  git worktree add --detach vendor/gh-pages && git -C vendor/gh-pages checkout --orphan gh-pages; \
	fi
	git -C vendor/gh-pages rm -rqf --ignore-unmatch .
	cp web/index.html web/index.js web/index.wasm web/index.data vendor/gh-pages/
	touch vendor/gh-pages/.nojekyll
	git -C vendor/gh-pages add -A
	git -C vendor/gh-pages commit -qm "Deploy browser build" || true
	git -C vendor/gh-pages push -q origin gh-pages
	git worktree remove --force vendor/gh-pages

clean:
	rm -f platoon platoon-gui test_platoon
	rm -rf web

distclean: clean
	rm -rf vendor

.PHONY: all test web deploy clean distclean
