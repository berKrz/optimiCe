CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_GNU_SOURCE
LDLIBS = -lm

TARGET = optimice
SRCDIR = src
SRC    = $(SRCDIR)/main.c
OBJ    = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
