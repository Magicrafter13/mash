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

all:
	@$(MAKE) $(BUILD)/$(PROG) --no-print-directory
	@ln -sf $(BUILD)/$(PROG) $(PROG)

debug: CPPFLAGS += -g
debug: $(D_OBJS)
	@$(MAKE) $(DEBUG)/$(PROG) --no-print-directory
	@ln -sf $(DEBUG)/$(PROG) $(PROG)

clean:
	$(RM) $(PROG) $(OBJS) $(D_OBJS) $(BUILD)/.compile_*
	@/bin/echo -e '\e[1;32mClean...\e[0m'

.PHONY: all clean debug

$(BUILD)/$(PROG): $(OBJS)
	$(CC) $(LDLIBS) $^ -o $@ # $^ is equivalent to $(OBJS)

$(DEBUG)/$(PROG): $(D_OBJS)
	$(CC) $(LDLIBS) $^ -o $@ # $^ is equivalent to $(OBJS)

$(BUILD)/%.o: $(SOURCE)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(DEBUG)/%.o: $(SOURCE)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD)/%.o: $(SOURCE)/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(DEBUG)/%.o: $(SOURCE)/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD):
	mkdir -p $@

$(BUILD)/.compile_normal: | $(BUILD)
ifneq ("$(wildcard $(BUILD)/.compile_debug)", "")
	@/bin/echo -e "\e[1;34mPreviously compiled with debug flags, recompiling...\e[0m"
	$(MAKE) clean --no-print-directory
endif
	@touch $(BUILD)/.compile_normal

$(BUILD)/.compile_debug: | $(BUILD)
ifneq ("$(wildcard $(BUILD)/.compile_normal)", "")
	@/bin/echo -e "\e[1;34mCode wasn't compiled with debug flags, recompiling...\e[0m"
	$(MAKE) clean --no-print-directory
endif
	@touch $(BUILD)/.compile_debug
