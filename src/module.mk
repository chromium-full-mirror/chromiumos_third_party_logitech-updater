# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include common.mk

BINARY_NAME := logitech-updater

PC_DEPS = libbrillo libchrome libcrypto libssl \
  libusb-1.0  cfm-dfu-notification
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
  src/audio_device.o \
  src/codec_device.o \
  src/ble_device.o \
  src/hdmi_device.o \
  src/composite_device.o \
  src/tablehub_device.o

clean: CLEAN(CXX_BINARY($(BINARY_NAME)))
