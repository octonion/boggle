# Optimized local builds for AMD/Intel CPUs, including GCC 14 on Xeon Phi.
.DEFAULT_GOAL := all
# ARCH=auto (default) detects a Xeon Phi and otherwise uses -march=native.
# Explicit examples: make ARCH=znver3; make ARCH=knl; make ARCH=portable.
ARCH ?= auto
CPU_MODEL ?= $(shell awk -F: '/^model name[[:space:]]*:/ { sub(/^[[:space:]]+/, "", $$2); print $$2; exit }' /proc/cpuinfo 2>/dev/null)

# GCC 15 removed Xeon Phi support. Never use -march=native under GCC 15 on KNL.
ifeq ($(ARCH),auto)
  ifneq ($(findstring Xeon Phi,$(CPU_MODEL)),)
    TARGET_ARCH := knl
  else
    TARGET_ARCH := native
  endif
else
  TARGET_ARCH := $(ARCH)
endif

# GNU make predefines CXX=g++. Replace that *built-in default* only for KNL.
# Environment and command-line CXX overrides remain authoritative.
ifeq ($(origin CXX),default)
  ifeq ($(TARGET_ARCH),knl)
    CXX := g++-14
  else
    CXX := g++
  endif
endif

CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic -pthread
COMPILER_VERSION := $(shell $(CXX) -dumpfullversion -dumpversion 2>/dev/null | head -n 1)
ARCH_FLAGS := $(if $(filter portable,$(TARGET_ARCH)),,-march=$(TARGET_ARCH))

# Make notices compiler/flag changes even if boggle.cpp is unchanged.
CONFIG_FILE := .boggle-build-config
.PHONY: all test clean show-config FORCE
all: boggle

$(CONFIG_FILE): FORCE
	@printf '%s\n' 'CXX=$(CXX)' 'CXX_VERSION=$(COMPILER_VERSION)' 'CXXFLAGS=$(CXXFLAGS)' 'CPPFLAGS=$(CPPFLAGS)' 'ARCH_FLAGS=$(ARCH_FLAGS)' 'LDFLAGS=$(LDFLAGS)' 'LDLIBS=$(LDLIBS)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

# Order-only so that a toolchain check does not cause unnecessary recompilation.
boggle: boggle.cpp $(CONFIG_FILE) | check-toolchain
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ARCH_FLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

.PHONY: check-toolchain
check-toolchain:
	@command -v $(firstword $(CXX)) >/dev/null 2>&1 || { \
	  echo 'Compiler $(CXX) not found. For Xeon Phi, install g++-14, or override CXX and ARCH explicitly.' >&2; exit 1; }
	@printf 'int main() { return 0; }\n' | $(CXX) $(ARCH_FLAGS) -x c++ -c -o /dev/null - 2>/dev/null || { \
	  echo 'Compiler $(CXX) does not support $(ARCH_FLAGS). On Xeon Phi use g++-14 with ARCH=knl.' >&2; exit 1; }

test: boggle
	python3 tests/test_boggle.py ./boggle

show-config:
	@printf 'CPU: %s\nArchitecture: %s\nCompiler: %s\nFlags: %s %s\n' \
	  '$(CPU_MODEL)' '$(TARGET_ARCH)' '$(CXX)' '$(CXXFLAGS)' '$(ARCH_FLAGS)'
	@$(CXX) --version | head -n 1

clean:
	rm -f boggle $(CONFIG_FILE) $(CONFIG_FILE).tmp
