CC = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = server
SRC = src/server.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(TARGET)
