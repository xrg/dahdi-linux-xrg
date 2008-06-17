#
# DAHDI
# 
# Makefile for firmware downloading/installation
#
# Copyright (C) 2007-2008, Digium, Inc.
#
# Joshua Colp <jcolp@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.PHONY: dist-clean all uninstall have_download install

PWD:=$(shell pwd)

OCT6114_064_VERSION:=1.05.01
OCT6114_128_VERSION:=1.05.01
TC400M_VERSION:=MR6.12
VPMADT032_VERSION:=1.07

FIRMWARE_URL:=http://downloads.digium.com/pub/telephony/firmware/releases

ALL_FIRMWARE="FIRMWARE-OCT6114-064 FIRMWARE-OCT6114-128 FIRMWARE-TC400M FIRMWARE-VPMADT032"

# Firmware files should use the naming convention: dahdi-fw-<base name>-<sub name>-<version> or dahdi-fw-<base name>-<version>
# First example: dahdi-fw-oct6114-064-1.05.01
# This means this is version 1.05.01 of the oct6114-064 firmware
# Second example: dahdi-fw-tc400m-MR5.6
# This means this is version MR5.6 of the tc400m firmware

# Build a list of firmware package filenames we need
FIRMWARE:=$(ALL_FIRMWARE:FIRMWARE-OCT6114-064=dahdi-fw-oct6114-064-$(OCT6114_064_VERSION).tar.gz)
FIRMWARE:=$(FIRMWARE:FIRMWARE-OCT6114-128=dahdi-fw-oct6114-128-$(OCT6114_128_VERSION).tar.gz)
FIRMWARE:=$(FIRMWARE:FIRMWARE-TC400M=dahdi-fw-tc400m-$(TC400M_VERSION).tar.gz)
FIRMWARE:=$(FIRMWARE:FIRMWARE-VPMADT032=dahdi-fw-vpmadt032-$(VPMADT032_VERSION).tar.gz)

# Build a list of object files if hotplug will not be used
OBJECT_FILES:=$(ALL_FIRMWARE:FIRMWARE-OCT6114-064=dahdi-fw-oct6114-064.o)
OBJECT_FILES:=$(OBJECT_FILES:FIRMWARE-OCT6114-128=dahdi-fw-oct6114-128.o)
OBJECT_FILES:=$(OBJECT_FILES:FIRMWARE-TC400M=dahdi-fw-tc400m.o)
OBJECT_FILES:=$(OBJECT_FILES:FIRMWARE-VPMADT032=dahdi-fw-vpmadt032.o)

# If "fetch" is used, --continue is not a valid option.
ifeq ($(WGET),wget)
WGET_ARGS:=--continue
endif

all: $(FIRMWARE)

have_download:
	@if test "$(DOWNLOAD)" = ":" ; then \
		echo "**************************************************"; \
		echo "***                                            ***"; \
		echo "*** You must have either wget or fetch to be   ***"; \
		echo "*** able to automatically download and install ***"; \
		echo "*** the requested firmware packages.           ***"; \
		echo "***                                            ***"; \
		echo "**************************************************"; \
		exit 1; \
	fi

# Download and extract firmware tarballs
dahdi-fw-oct6114-064-%.tar.gz: have_download
ifeq ($(shell if ( [ "$(HOTPLUG_FIRMWARE)" = "no" ] ) || ( [ -d $(DESTDIR)/usr/lib/hotplug/firmware ] && ! [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-oct6114-064-$(OCT6114_064_VERSION) ] ) || ( [ -d $(DESTDIR)/lib/firmware ] && ! [ -f $(DESTDIR)/lib/firmware/.dahdi-fw-oct6114-064-$(OCT6114_064_VERSION) ] ); then echo "yes"; else echo "no"; fi),yes)
	@echo "Attempting to download $@"
	@if test ! -f $@; then $(DOWNLOAD) $(WGET_ARGS) $(FIRMWARE_URL)/$@; fi;
	@if test ! -f $@; then exit 1; fi; \
	(cat $@ | gzip -d | tar -xf -)
ifeq ($(HOTPLUG_FIRMWARE),yes)
	@echo "Installing dahdi-fw-oct6114-064.bin to hotplug firmware directories"
	if [ -d $(DESTDIR)/usr/lib/hotplug/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-oct6114-064.bin $(DESTDIR)/usr/lib/hotplug/firmware; \
		rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-oct6114-064-*; \
		touch $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-oct6114-064-$(OCT6114_064_VERSION); \
	fi
	if [ -d $(DESTDIR)/lib/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-oct6114-064.bin $(DESTDIR)/lib/firmware; \
		rm -rf $(DESTDIR)/lib/firmware/.dahdi-fw-oct6114-064-*; \
		touch $(DESTDIR)/lib/firmware/.dahdi-fw-oct6114-064-$(OCT6114_064_VERSION); \
	fi
endif
else
	@echo "Firmware dahdi-fw-oct6114-064.bin is already installed with required version $(OCT6114_064_VERSION)"
endif

dahdi-fw-oct6114-128-%.tar.gz: have_download
ifeq ($(shell if ( [ "$(HOTPLUG_FIRMWARE)" = "no" ] ) || ( [ -d $(DESTDIR)/usr/lib/hotplug/firmware ] && ! [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-oct6114-128-$(OCT6114_128_VERSION) ] ) || ( [ -d $(DESTDIR)/lib/firmware ] && ! [ -f $(DESTDIR)/lib/firmware/.dahdi-fw-oct6114-128-$(OCT6114_128_VERSION) ] ); then echo "yes"; else echo "no"; fi),yes)
	@echo "Attempting to download $@"
	@if test ! -f $@; then $(DOWNLOAD) $(WGET_ARGS) $(FIRMWARE_URL)/$@; fi;
	@if test ! -f $@; then exit 1; fi; \
	(cat $@ | gzip -d | tar -xf -)
ifeq ($(HOTPLUG_FIRMWARE),yes)
	@echo "Installing dahdi-fw-oct6114-128.bin to hotplug firmware directories"
	if [ -d $(DESTDIR)/usr/lib/hotplug/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-oct6114-128.bin $(DESTDIR)/usr/lib/hotplug/firmware; \
		rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-oct6114-128-*; \
		touch $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-oct6114-128-$(OCT6114_128_VERSION); \
	fi
	if [ -d $(DESTDIR)/lib/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-oct6114-128.bin $(DESTDIR)/lib/firmware; \
		rm -rf $(DESTDIR)/lib/firmware/.dahdi-fw-oct6114-128-*; \
		touch $(DESTDIR)/lib/firmware/.dahdi-fw-oct6114-128-$(OCT6114_128_VERSION); \
	fi
endif
else
	@echo "Firmware dahdi-fw-oct6114-128.bin is already installed with required version $(OCT6114_128_VERSION)"
endif

dahdi-fw-tc400m-%.tar.gz: have_download
ifeq ($(shell if ( [ "$(HOTPLUG_FIRMWARE)" = "no" ] ) || ( [ -d $(DESTDIR)/usr/lib/hotplug/firmware ] && ! [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-tc400m-$(TC400M_VERSION) ] ) || ( [ -d $(DESTDIR)/lib/firmware ] && ! [ -f $(DESTDIR)/lib/firmware/.dahdi-fw-tc400m-$(TC400M_VERSION) ] ); then echo "yes"; else echo "no"; fi),yes)
	@echo "Attempting to download $@"
	@if test ! -f $@; then $(DOWNLOAD) $(WGET_ARGS) $(FIRMWARE_URL)/$@; fi;
	@if test ! -f $@; then exit 1; fi; \
	(cat $@ | gzip -d | tar -xf -)
ifeq ($(HOTPLUG_FIRMWARE),yes)
	@echo "Installing dahdi-fw-tc400m.bin to hotplug firmware directories"
	if [ -d $(DESTDIR)/usr/lib/hotplug/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-tc400m.bin $(DESTDIR)/usr/lib/hotplug/firmware; \
		rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-tc400m-*; \
		touch $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-tc400m-$(TC400M_VERSION); \
	fi
	if [ -d $(DESTDIR)/lib/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-tc400m.bin $(DESTDIR)/lib/firmware; \
		rm -rf $(DESTDIR)/lib/firmware/.dahdi-fw-tc400m-*; \
		touch $(DESTDIR)/lib/firmware/.dahdi-fw-tc400m-$(TC400M_VERSION); \
	fi
endif
else
	@echo "Firmware dahdi-fw-tc400m.bin is already installed with required version $(TC400M_VERSION)"
endif

dahdi-fw-vpmadt032-%.tar.gz: have_download
ifeq ($(shell if ( [ "$(HOTPLUG_FIRMWARE)" = "no" ] ) || ( [ -d $(DESTDIR)/usr/lib/hotplug/firmware ] && ! [ -f $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-vpmadt032-$(VPMADT032_VERSION) ] ) || ( [ -d $(DESTDIR)/lib/firmware ] && ! [ -f $(DESTDIR)/lib/firmware/.dahdi-fw-vpmadt032-$(VPMADT032_VERSION) ] ); then echo "yes"; else echo "no"; fi),yes)
	@echo "Attempting to download $@"
	@if test ! -f $@; then $(DOWNLOAD) $(WGET_ARGS) $(FIRMWARE_URL)/$@; fi;
	@if test ! -f $@; then exit 1; fi; \
	(cat $@ | gzip -d | tar -xf -)
ifeq ($(HOTPLUG_FIRMWARE),yes)
	@echo "Installing dahdi-fw-vpmadt032.bin to hotplug firmware directories"
	if [ -d $(DESTDIR)/usr/lib/hotplug/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-vpmadt032.bin $(DESTDIR)/usr/lib/hotplug/firmware; \
		rm -rf $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-vpmadt032-*; \
		touch $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw-vpmadt032-$(VPMADT032_VERSION); \
	fi
	if [ -d $(DESTDIR)/lib/firmware ]; then \
		$(INSTALL) -m 644 dahdi-fw-vpmadt032.bin $(DESTDIR)/lib/firmware; \
		rm -rf $(DESTDIR)/lib/firmware/.dahdi-fw-vpmadt032-*; \
		touch $(DESTDIR)/lib/firmware/.dahdi-fw-vpmadt032-$(VPMADT032_VERSION); \
	fi
endif
else
	@echo "Firmware dahdi-fw-vpmadt032.bin is already installed with required version $(VPMADT032_VERSION)"
endif

# Clean up any downloaded/extracted firmware packages
dist-clean: clean
	rm -f dahdi-fw-*.bin
	rm -f dahdi-fw-*.tar.gz
	rm -f make_firmware_object

# Clean up anything we built
clean:
	rm -f dahdi-fw-*.o

# Create object files suitable for linking against
object-build: $(FIRMWARE) $(OBJECT_FILES)

# Install all downloaded firmware images for hotplug usage and build headers for inclusion
hotplug-install: $(FIRMWARE)

# Uninstall any installed dahdi firmware images from hotplug firmware directories
hotplug-uninstall:
	if [ -d $(DESTDIR)/usr/lib/hotplug/firmware ]; then \
		rm -f $(DESTDIR)/usr/lib/hotplug/firmware/dahdi-fw-*.bin; \
		rm -f $(DESTDIR)/usr/lib/hotplug/firmware/.dahdi-fw*; \
	fi
	if [ -d $(DESTDIR)/lib/firmware ]; then \
		rm -f $(DESTDIR)/lib/firmware/dahdi-fw-*.bin; \
		rm -f $(DESTDIR)/lib/firmware/.dahdi-fw*; \
	fi

make_firmware_object: make_firmware_object.in ../dahdi-base.o
	@export BFDNAME=`LANG=C objdump -f ../dahdi-base.o | grep -e "dahdi-base.o:" | sed "s/.*file format \(.*\)/\1/"`; \
	 export BFDARCH=`LANG=C objdump -f ../dahdi-base.o | grep -e "architecture:" | sed "s/.*ture: \(.*\),.*/\1/"`; \
	 sed -e s/BFDNAME/$${BFDNAME}/ -e s/BFDARCH/$${BFDARCH}/ $< > $@
	@chmod +x $@

# Build object file of an oct6114 064 firmware image for linking
dahdi-fw-oct6114-064.o: dahdi-fw-oct6114-064-$(OCT6114_064_VERSION).tar.gz dahdi-fw-oct6114-064.bin make_firmware_object
	@echo Making firmware object file for dahdi-fw-oct6114-064.bin
	./make_firmware_object dahdi-fw-oct6114-064.bin $@

# Build object file of an oct6114 128 firmware image for linking
dahdi-fw-oct6114-128.o: dahdi-fw-oct6114-128-$(OCT6114_128_VERSION).tar.gz dahdi-fw-oct6114-128.bin make_firmware_object
	@echo Making firmware object file for dahdi-fw-oct6114-128.bin
	./make_firmware_object dahdi-fw-oct6114-128.bin $@

# Build object file of a TC400M firmware image for linking
dahdi-fw-tc400m.o: dahdi-fw-tc400m-$(TC400M_VERSION).tar.gz dahdi-fw-tc400m.bin make_firmware_object
	@echo Making firmware object file for dahdi-fw-tc400m.bin
	./make_firmware_object dahdi-fw-tc400m.bin $@

# Build object file of a VPMADT032 firmware image for linking
dahdi-fw-vpmadt032.o: dahdi-fw-vpmadt032-$(VPMADT032_VERSION).tar.gz dahdi-fw-vpmadt032.bin make_firmware_object
	@echo Making firmware object file for dahdi-fw-vpmadt032.bin
	./make_firmware_object dahdi-fw-vpmadt032.bin $@
