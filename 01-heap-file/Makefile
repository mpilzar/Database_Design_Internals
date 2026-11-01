include ../common.mk

SRC := src/hp_file.c src/record.c
HDR := $(wildcard include/*.h)

all: $(BUILD)/hp_test $(BUILD)/hp_main $(BUILD)/bf_main

$(BUILD)/hp_test: tests/hp_test.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/hp_test.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/hp_main: examples/hp_main.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) examples/hp_main.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/bf_main: examples/bf_main.c src/record.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) examples/bf_main.c src/record.c -o $@ $(LDFLAGS) $(LDLIBS)

# Binaries run inside build/ so the .db files they create stay out of the tree.
test: all
	cd $(BUILD) && ./hp_test

demo: all
	cd $(BUILD) && ./bf_main | tail -6 && ./hp_main 1000

clean:
	rm -rf $(BUILD)

.PHONY: all test demo clean
