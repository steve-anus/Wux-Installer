.DEFAULT_GOAL := run
MAKEFLAGS += --no-print-directory
SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
CXX = g++
REF ?= ccc8efd
TREE ?= build/head
export REF TREE
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
            -fsanitize=address,undefined -fno-sanitize-recover=all -pthread
CPPFLAGS = -I"$$TREE/src" -I"$$TREE/src/sdk" -Ishim/sdk -I.
COMMON := model.cpp fakes.cpp

.PHONY: run old prepare-head prepare-old compile clean
run: prepare-head
	@$(MAKE) compile TREE=build/head
	@failed=0; for scenario in S1 S2 S3 S4 S5 S6; do \
		./build/head/lifecycle_test "$$scenario" || failed=1; \
	done; ./build/head/wuxflow_test || failed=1; exit $$failed

old: prepare-old
	@$(MAKE) compile TREE=build/old
	@failed=0; for scenario in S1 S2 S3 S4 S5 S6; do \
		code=0; output=$$(./build/old/lifecycle_test "$$scenario" 2>&1) || code=$$?; \
		printf '%s\n' "$$output"; \
		case "$$scenario" in \
			S1|S5) test "$$code" -eq 0 || failed=1 ;; \
			*) test "$$code" -eq 1 || failed=1; \
			   printf '%s\n' "$$output" | grep -Fxq \
			   '  violation 1: ProcUIShutdown in the release/switch window without EXITING' || failed=1 ;; \
		esac; \
		if printf '%s\n' "$$output" | grep -Eq 'Sanitizer|runtime error:'; then failed=1; fi; \
	done; \
	if test "$$failed" -eq 0; then printf '%s\n' 'old regression lock PASS'; \
	else printf '%s\n' 'old regression lock FAIL'; fi; exit $$failed

prepare-head:
	rm -rf build/head
	mkdir -p build/head
	cp -r ../../../src build/head/
	cp -r shim/. build/head/src/
	cmp ../../../src/Application.cpp build/head/src/Application.cpp
	cmp ../../../src/menu/WuxFlow.cpp build/head/src/menu/WuxFlow.cpp

prepare-old:
	rm -rf build/old
	mkdir -p build/old
	git -C ../../.. archive "$$REF" src | tar -x -C build/old
	cp -r shim/. build/old/src/

compile:
	$(CXX) $(CXXFLAGS) -ffile-prefix-map=$$(pwd)=. $(CPPFLAGS) \
		"$$TREE/src/Application.cpp" $(COMMON) lifecycle_test.cpp -o "$$TREE/lifecycle_test"
	$(CXX) $(CXXFLAGS) -ffile-prefix-map=$$(pwd)=. $(CPPFLAGS) \
		"$$TREE/src/menu/WuxFlow.cpp" $(COMMON) wuxflow_test.cpp -o "$$TREE/wuxflow_test"

clean:
	rm -rf build
