include ../common.mk

# This component builds on the course's precompiled heap-file library
# (lib/libhp_file.so), which exposes record-level access to heap files.
LDFLAGS += -L./lib -Wl,-rpath,$(CURDIR)/lib
LDLIBS  := -lhp_file $(LDLIBS)

SRC := src/sort.c src/merge.c src/chunk.c src/record.c
HDR := $(wildcard include/*.h)

all: $(BUILD)/sort_main $(BUILD)/sort_test

$(BUILD)/sort_main: examples/sort_main.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) examples/sort_main.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/sort_test: tests/sort_test.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/sort_test.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

test: all
	cd $(BUILD) && ./sort_test

demo: $(BUILD)/sort_main
	cd $(BUILD) && ./sort_main

clean:
	rm -rf $(BUILD)

.PHONY: all test demo clean
