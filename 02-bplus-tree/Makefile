include ../common.mk

SRC := src/bp_file.c src/bp_datanode.c src/bp_indexnode.c src/record.c
HDR := $(wildcard include/*.h)

all: $(BUILD)/bp_main $(BUILD)/bp_test $(BUILD)/bp_test_order4 $(BUILD)/bp_test_order3

$(BUILD)/bp_main: examples/bp_main.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) examples/bp_main.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

# Default geometry, derived from the 512-byte block: 8 records per leaf, fan-out 63.
$(BUILD)/bp_test: tests/bp_test.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/bp_test.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

# The original assignment's order-4 tree (3 records per leaf, 3 keys per index node):
# same code, much deeper trees, far more splits and merges per operation.
$(BUILD)/bp_test_order4: tests/bp_test.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBP_DATA_CAPACITY=3 -DBP_INDEX_CAPACITY=3 tests/bp_test.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

# Smallest legal geometry: 2 records per leaf, fan-out 3.
$(BUILD)/bp_test_order3: tests/bp_test.c $(SRC) $(HDR) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBP_DATA_CAPACITY=2 -DBP_INDEX_CAPACITY=2 tests/bp_test.c $(SRC) -o $@ $(LDFLAGS) $(LDLIBS)

test: all
	cd $(BUILD) && ./bp_test 20000 && ./bp_test_order4 5000 && ./bp_test_order3 3000

demo: $(BUILD)/bp_main
	cd $(BUILD) && ./bp_main

clean:
	rm -rf $(BUILD)

.PHONY: all test demo clean
