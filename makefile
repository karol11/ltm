EXAMPLES := \
	1-hello-world \
	2-composition \
	3-association \

all: $(EXAMPLES)

$(EXAMPLES): %: examples/%.cc src/ltm.cc src/ltm.h
	clang++ -g -Wall -Wextra -pedantic -std=c++14 -I src -o $@ $< src/ltm.cc
