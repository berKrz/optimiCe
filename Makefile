# CC is left to make's default (cc): override it with `make CC=clang` or from the environment
PKG_CONFIG ?= pkg-config

# tomlc17 ships a pkg-config file (libtomlc17.pc). Fall back to a bare -ltomlc17
# when pkg-config is missing or can't find it (library on a default search path)
TOML_CFLAGS := $(shell $(PKG_CONFIG) --cflags libtomlc17 2>/dev/null)
TOML_LIBS   := $(shell $(PKG_CONFIG) --libs   libtomlc17 2>/dev/null || echo -ltomlc17)

# CFLAGS is the part meant to be overridden (`make CFLAGS="-O0 -g"`); BUILD_FLAGS is what the
# build needs whatever CFLAGS says
CFLAGS      ?= -O2 -g -Wall -Wextra
BUILD_FLAGS  = -std=c11 -pthread -I$(SRCDIR) -D_GNU_SOURCE -MMD -MP \
               -finput-charset=UTF-8 -fexec-charset=UTF-8 $(TOML_CFLAGS)
LDLIBS       = -lm $(TOML_LIBS)

TARGET = optimice
SRCDIR = src
SRC    = $(SRCDIR)/main.c \
         $(SRCDIR)/utils.c \
         $(SRCDIR)/rng.c \
         $(SRCDIR)/fitness.c \
         $(SRCDIR)/bounds.c \
         $(SRCDIR)/config.c \
         $(SRCDIR)/toml.c \
         $(SRCDIR)/batch.c \
         $(SRCDIR)/output.c \
         $(SRCDIR)/args.c \
         $(SRCDIR)/algorithms/pso.c \
         $(SRCDIR)/algorithms/ga.c \
         $(SRCDIR)/algorithms/sma.c
OBJ    = $(SRC:.c=.o)
DEP    = $(OBJ:.o=.d)

# Commit the binary was built from, for the results file's header: the short hash, plus "-dirty"
# when the tree had uncommitted changes (a git tag instead, once one exists)
# The flake passes it in, since a Nix build has no .git
VERSION ?= $(shell git describe --always --dirty 2>/dev/null || echo unknown)

all: $(TARGET)

# Regenerated on every make, but only rewritten when the version changed, so output.o is rebuilt
# exactly when the commit (or its dirty state) changes and nothing else is
$(SRCDIR)/version.h: FORCE
	@printf '#define OPTIMICE_VERSION "%s"\n' '$(VERSION)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

# Listed explicitly: on a first build there's no .d file yet to say output.c includes version.h
$(SRCDIR)/output.o: $(SRCDIR)/version.h

$(TARGET): $(OBJ)
	$(CC) $(BUILD_FLAGS) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(BUILD_FLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(DEP) $(TARGET) $(SRCDIR)/version.h

-include $(DEP)

.PHONY: all clean FORCE
