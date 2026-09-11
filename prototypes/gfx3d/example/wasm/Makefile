EMCC ?= emcc

REPO_ROOT := ../../../..
LIB_DIR := ../../lib
COMMON_DIR := ../common
OUT_DIR := $(REPO_ROOT)/docs/prototypes/gfx3d
OUT := $(OUT_DIR)/gfx3d.wasm

SRCS := cpp/main.cpp $(COMMON_DIR)/scene.cpp $(LIB_DIR)/src/gfx3d.cpp
HDRS := $(wildcard $(LIB_DIR)/include/shapoco/gfx3d/*.hpp) $(COMMON_DIR)/scene.hpp

CFLAGS := -O2 -std=c++17 -Wall -Wextra -I $(LIB_DIR)/include -I $(COMMON_DIR)

.PHONY: all clean serve

all: $(OUT)

$(OUT): $(SRCS) $(HDRS) Makefile
	$(EMCC) $(CFLAGS) $(SRCS) --no-entry -sSTANDALONE_WASM=1 \
		-sEXPORTED_FUNCTIONS=_wcb_init,_wcb_frame,_wcb_get_fb,_wcb_get_width,_wcb_get_height \
		-o $@

clean:
	rm -f $(OUT)

# 動作確認用: docs/ をルートに HTTP サーバを立てて
# http://localhost:8000/prototypes/gfx3d/ を開く
serve:
	python3 -m http.server -d $(REPO_ROOT)/docs 8000
