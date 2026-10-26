CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Werror -Iinclude -O2
LDFLAGS = -lm -lpthread

SRC = src/main.c src/render.c src/audio.c
BIN = cscape

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: all
	./$(BIN)

clean:
	rm -f $(BIN)
