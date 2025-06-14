INCLUDE := include
SOURCE  := source
BUILD   := build
DEBUG   := build/debug

PROG   = mash
CSRC   = $(shell find $(SOURCE) -type f -name '*.c')
CXXSRC = $(shell find $(SOURCE) -type f -name '*.cpp')
OBJS   = $(CSRC:$(SOURCE)/%.c=$(BUILD)/%.o) $(CXXSRC:$(SOURCE)/%.cpp=$(BUILD)/%.o)
D_OBJS = $(CSRC:$(SOURCE)/%.c=$(DEBUG)/%.o) $(CXXSRC:$(SOURCE)/%.cpp=$(DEBUG)/%.o)
DIRS   = $(shell find $(SOURCE) -mindepth 1 -type d -printf '$(BUILD)/%f\n')
D_DIRS = $(shell find $(SOURCE) -mindepth 1 -type d -printf '$(DEBUG)/%f\n')

CFLAGS   =
CXXFLAGS =
CPPFLAGS = -c -I$(INCLUDE) -Wall -Werror=implicit-function-declaration -std=c99
LDFLAGS  =
LDLIBS   = -pthread -lreadline

all: $(BUILD) $(DIRS)
	@$(MAKE) $(BUILD)/$(PROG) --no-print-directory
	@ln -sf $(BUILD)/$(PROG) $(PROG)

debug: $(DEBUG) $(D_DIRS)
	@$(MAKE) $(DEBUG)/$(PROG) --no-print-directory
	@ln -sf $(DEBUG)/$(PROG) $(PROG)

clean:
	$(RM) $(PROG) $(OBJS) $(D_OBJS) $(BUILD)/$(PROG) $(DEBUG)/$(PROG)
	@/bin/echo -e '\e[1;32mClean...\e[0m'

install:
	mkdir -p "$${DESTDIR-/usr/bin}"
	cp -f mash "$${DESTDIR-/usr/bin}/mash"
	@echo "For \`chsh' to allow you to use mash as your shell, you must add it to"
	@echo "/etc/shells"
	@echo "Mash has been installed to $${DESTDIR-/usr/bin}/mash"

uninstall:
	$(RM) "$${DESTDIR-/usr/bin}/mash"
	@echo "If you modified /etc/shells don't forget to change it back."
	@echo "Mash has been uninstalled from your system"

.PHONY: all clean debug install uninstall

$(BUILD)/$(PROG): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

$(DEBUG)/$(PROG): CPPFLAGS += -g -DDEBUG
$(DEBUG)/$(PROG): $(D_OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o $(DEBUG)/%.o: $(SOURCE)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD)/%.o $(DEBUG)/%.o: $(SOURCE)/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD) $(DEBUG) $(DIRS) $(D_DIRS):
	mkdir -p $@
