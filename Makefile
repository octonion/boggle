CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -Wpedantic -pthread

.PHONY: all test clean
all: boggle

boggle: boggle.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test: boggle
	python3 tests/test_boggle.py ./boggle

clean:
	rm -f boggle
