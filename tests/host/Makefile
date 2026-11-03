# Host-side tests for the .wux parsing and decryption code in src/wux.
# They build the same sources with the host compiler under AddressSanitizer
# and UndefinedBehaviorSanitizer, which the console toolchain lacks.
#   make -C tests/host        build and run
CXX      ?= g++
CC       ?= gcc
SRC      := ../../src
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wshadow -Wformat=2 \
            -Wformat-nonliteral -fsanitize=address,undefined \
            -fno-sanitize-recover=all -fno-omit-frame-pointer -I$(SRC) -MMD -MP
MODULES  := aes_cbc wux_container wud_toc fst tmd ticket key_file
OBJS     := $(addprefix build/,$(addsuffix .o,$(MODULES))) build/tiny_aes.o
CFLAGS   := -std=c11 -O1 -g -Wall -Wextra -Wshadow -Wformat=2 \
            -fsanitize=address,undefined -fno-sanitize-recover=all -MMD -MP
TESTS    := $(patsubst %.cpp,build/%.o,$(filter-out test_guitext.cpp,$(wildcard test_*.cpp)))

# GuiText and GuiElement build against the stand-ins in gui_stubs/ (searched
# before src/) and glm from the console toolchain's portlibs (header-only).
DEVKITPRO ?= /opt/devkitpro
GUIFLAGS := $(filter-out -I$(SRC),$(CXXFLAGS)) -Wno-unused-parameter -Igui_stubs \
            -I$(SRC) -idirafter $(DEVKITPRO)/portlibs/ppc/include
GUIOBJS  := build/GuiText.o build/GuiElement.o build/test_guitext.o

.PHONY: run clean
run: build/host_tests
	./build/host_tests

build/host_tests: $(OBJS) $(TESTS) $(GUIOBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

build/%.o: $(SRC)/wux/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/tiny_aes.o: $(SRC)/wux/tiny_aes.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/test_%.o: test_%.cpp check.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/Gui%.o: $(SRC)/gui/Gui%.cpp | build
	$(CXX) $(GUIFLAGS) -c $< -o $@

build/test_guitext.o: test_guitext.cpp check.h | build
	$(CXX) $(GUIFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build

-include $(wildcard build/*.d)
