INCLUDE := include
SOURCE  := source
BUILD   := build
DEBUG   := build/debug

PROG   = mash
CSRC   = $(shell find $(SOURCE) -type f -name '*.c')
CXXSRC = $(shell find $(SOURCE) -type f -name '*.cpp')
OBJS   = $(CSRC:$(SOURCE)/%.c=$(BUILD)/%.o) $(CXXSRC:$(SOURCE)/%.cpp=$(BUILD)/%.o)
D_OBJS = $(CSRC:$(SOURCE)/%.c=$(DEBUG)/%.o) $(CXXSRC:$(SOURCE)/%.cpp=$(DEBUG)/%.o)

CFLAGS   =
CXXFLAGS =
CPPFLAGS = -c -I$(INCLUDE) -Wall
LDFLAGS  =
LDLIBS   =

all: $(BUILD)
	@$(MAKE) $(BUILD)/$(PROG) --no-print-directory
	@ln -sf $(BUILD)/$(PROG) $(PROG)

debug: CPPFLAGS += -g
debug: $(DEBUG)
	@$(MAKE) $(DEBUG)/$(PROG) --no-print-directory
	@ln -sf $(DEBUG)/$(PROG) $(PROG)

clean:
	$(RM) $(PROG) $(OBJS) $(D_OBJS) $(BUILD)/$(PROG) $(DEBUG)/$(PROG)
	@/bin/echo -e '\e[1;32mClean...\e[0m'

.PHONY: all clean debug

$(BUILD)/$(PROG): $(OBJS)
	$(CC) $(LDLIBS) $^ -o $@

$(DEBUG)/$(PROG): $(D_OBJS)
	$(CC) $(LDLIBS) $^ -o $@

$(BUILD)/%.o $(DEBUG)/%.o: $(SOURCE)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD)/%.o $(DEBUG)/%.o: $(SOURCE)/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD) $(DEBUG):
	mkdir -p $@
