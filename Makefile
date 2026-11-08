CC         = gcc
PKG_CONFIG ?= pkg-config

# tomlc17 ships a pkg-config file (libtomlc17.pc). Fall back to a bare -ltomlc17
# when pkg-config is missing or can't find it (library on a default search path)
TOML_CFLAGS := $(shell $(PKG_CONFIG) --cflags libtomlc17 2>/dev/null)
TOML_LIBS   := $(shell $(PKG_CONFIG) --libs   libtomlc17 2>/dev/null || echo -ltomlc17)

CFLAGS = -Wall -Wextra -std=c11 -I$(SRCDIR) -g -D_GNU_SOURCE -MMD -MP \
         -finput-charset=UTF-8 -fexec-charset=UTF-8 $(TOML_CFLAGS)
LDLIBS = -lm $(TOML_LIBS)

TARGET = optimice
SRCDIR = src
SRC    = $(SRCDIR)/main.c \
         $(SRCDIR)/utils.c \
         $(SRCDIR)/rng.c \
         $(SRCDIR)/fitness.c \
         $(SRCDIR)/bounds.c \
         $(SRCDIR)/config.c \
         $(SRCDIR)/toml.c \
         $(SRCDIR)/args.c \
         $(SRCDIR)/algorithms/pso.c \
         $(SRCDIR)/algorithms/ga.c \
         $(SRCDIR)/algorithms/sma.c
OBJ    = $(SRC:.c=.o)
DEP    = $(OBJ:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(DEP) $(TARGET)

-include $(DEP)

.PHONY: all clean
