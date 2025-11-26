CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2
LDFLAGS ?= $(shell pkg-config --libs --cflags raylib) -lm
SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
TARGET ?= build/u8_fps

all: $(TARGET)

$(TARGET): $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@ $(LDFLAGS)

build:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean
