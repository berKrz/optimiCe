CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -I$(SRCDIR) -g -D_GNU_SOURCE \
				 -finput-charset=UTF-8 -fexec-charset=UTF-8
LDLIBS = -lm -ltomlc17

TARGET = optimice
SRCDIR = src
SRC    = $(SRCDIR)/main.c \
         $(SRCDIR)/utils.c \
         $(SRCDIR)/rng.c \
         $(SRCDIR)/fitness.c \
         $(SRCDIR)/config.c \
         $(SRCDIR)/toml.c \
         $(SRCDIR)/args.c \
         $(SRCDIR)/algorithms/pso.c \
         $(SRCDIR)/algorithms/ga.c \
         $(SRCDIR)/algorithms/sma.c
OBJ    = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
