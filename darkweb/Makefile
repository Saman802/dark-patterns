# Darkweb build. Requires the Emscripten SDK (emcc) for `make wasm`; g++ for `make test`.
EMCC     ?= em++
CXX      ?= g++
SRC      := cpp/engine.cpp
HDR      := $(wildcard cpp/*.hpp)
WASM_OUT := extension/wasm
EXPORTS  := _dw_init,_dw_scan,_dw_trace_text,_dw_rules_dump,_dw_trie_dump,_dw_hash_dump,_malloc,_free
RUNTIME  := UTF8ToString,stringToUTF8,lengthBytesUTF8

.PHONY: all wasm test chrome firefox clean
all: wasm

wasm: $(WASM_OUT)/darkweb_engine.js

$(WASM_OUT)/darkweb_engine.js: $(SRC) $(HDR)
	mkdir -p $(WASM_OUT)
	$(EMCC) $(SRC) -o $@ -std=c++17 -O2 \
	    -sMODULARIZE=1 -sEXPORT_NAME=createDarkweb -sENVIRONMENT=web,worker \
	    -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
	    -sEXPORTED_FUNCTIONS=$(EXPORTS) -sEXPORTED_RUNTIME_METHODS=$(RUNTIME)

# native build of the same C++ for quick testing / viva demos without a browser
test: build/test
	./build/test tests/sample_nodes.tsv | python3 -m json.tool | head -60

build/test: $(SRC) $(HDR) cpp/test_main.cpp
	mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall -Wextra $(SRC) cpp/test_main.cpp -o $@

chrome: wasm
	mkdir -p dist && cd extension && zip -r ../dist/darkweb-chrome.zip . -x "manifest.firefox.json"

firefox: wasm
	rm -rf build/firefox && mkdir -p build/firefox dist
	cp -r extension/. build/firefox/ && mv build/firefox/manifest.firefox.json build/firefox/manifest.json
	cd build/firefox && zip -r ../../dist/darkweb-firefox.zip .

clean:
	rm -rf build dist $(WASM_OUT)/darkweb_engine.*
