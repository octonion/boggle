CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic -pthread
# Optional instruction-set target, e.g. make ARCH=znver3 or make ARCH=native.
# Leave unset for the compiler's portable default. Rebuild after changing ARCH.
ARCH ?=
ARCH_FLAGS = $(if $(strip $(ARCH)),-march=$(ARCH))

.PHONY: all test clean
all: boggle

boggle: boggle.cpp
	$(CXX) $(CXXFLAGS) $(ARCH_FLAGS) -o $@ $<

test: boggle
	python3 tests/test_boggle.py ./boggle

clean:
	rm -f boggle
