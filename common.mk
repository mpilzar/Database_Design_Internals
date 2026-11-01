# Shared build settings, included by each component's Makefile.
.DEFAULT_GOAL := all
ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BF_DIR  := $(ROOT)/blockfile
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I./include -I$(BF_DIR)/include
LDFLAGS += -L$(BF_DIR)/lib -Wl,-rpath,$(BF_DIR)/lib
LDLIBS  += -lbf
BUILD   := build

$(BUILD):
	@mkdir -p $@
