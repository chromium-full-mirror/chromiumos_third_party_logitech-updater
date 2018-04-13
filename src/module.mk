# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include common.mk

BINARY_NAME := logitech-updater

PC_DEPS = libbrillo-$(BASE_VER) libchrome-$(BASE_VER) libcrypto libssl
PC_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PC_DEPS))
PC_LIBS := $(shell $(PKG_CONFIG) --libs $(PC_DEPS))
LDLIBS += $(PC_LIBS)

CPPFLAGS += $(PC_CFLAGS)

all: CXX_BINARY($(BINARY_NAME))

CXX_BINARY($(BINARY_NAME)): \
	src/main.o \
	src/utilities.o \
	src/usb_device.o \
	src/video_device.o \
	src/eeprom_device.o \
	src/mcu2_device.o \
	src/composite_device.o

clean: CLEAN(CXX_BINARY($(BINARY_NAME)))
