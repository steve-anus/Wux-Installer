# Host-side tests for the .wux parsing and decryption code in src/wux.
# They build the same sources with the host compiler under AddressSanitizer
# and UndefinedBehaviorSanitizer, which the console toolchain lacks.
#   make -C tests/host        build and run
CXX      ?= g++
SRC      := ../../src
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wshadow -Wformat=2 \
            -Wformat-nonliteral -fsanitize=address,undefined \
            -fno-sanitize-recover=all -fno-omit-frame-pointer -I$(SRC)
MODULES  := aes_cbc wux_container wud_toc fst tmd ticket
OBJS     := $(addprefix build/,$(addsuffix .o,$(MODULES)))
TESTS    := $(patsubst %.cpp,build/%.o,$(wildcard test_*.cpp))

.PHONY: run clean
run: build/host_tests
	./build/host_tests

build/host_tests: $(OBJS) $(TESTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

build/%.o: $(SRC)/wux/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/test_%.o: test_%.cpp check.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build
