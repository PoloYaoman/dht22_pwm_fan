CC = gcc
CFLAGS = -I./src/types -Wall -Wextra
SRC = src/main.c
TARGET = tcp-client

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)