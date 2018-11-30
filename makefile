EXAMPLES := \
	1-hello-world \
	2-composition \

BIN := $(patsubst %,$(OBJDIR)/%.o,$(basename $(SRCS)))
SRC := $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS)))

all: $(BIN)

$(BIN): $(OBJS)
	clang++ -g -Wall -Wextra -pedantic -std=c++14 -I src -o $@ $^ src\ltm.cc
