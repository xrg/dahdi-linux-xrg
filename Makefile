#
# Makefile for DAHDI Linux kernel modules
#
# Copyright (C) 2001-2008 Digium, Inc.
#
#

# This Makefile is used both by the kernel build system (to set
# a number of variables before descending into the real module
# build directory) and by the top-level 'make' process. We can
# determine which mode is needed by checking the KBUILD_EXTMOD
# variable, although on reasonably new kernels the kernel build
# system won't even use this file, it will just use the Kbuild
# file directly.

ifdef KBUILD_EXTMOD

include $(src)/Kbuild

else

PWD:=$(shell pwd)

ifndef ARCH
ARCH:=$(shell uname -m | sed -e s/i.86/i386/)
endif

ifndef DEB_HOST_GNU_TYPE
UNAME_M:=$(shell uname -m)
else
UNAME_M:=$(DEB_HOST_GNU_TYPE)
endif

# If you want to build for a kernel other than the current kernel, set KVERS
ifndef KVERS
KVERS:=$(shell uname -r)
endif
ifndef KSRC
  ifneq (,$(wildcard /lib/modules/$(KVERS)/build))
    KSRC:=/lib/modules/$(KVERS)/build
  else
    KSRC_SEARCH_PATH:=/usr/src/linux
    KSRC:=$(shell for dir in $(KSRC_SEARCH_PATH); do if [ -d $$dir ]; then echo $$dir; break; fi; done)
  endif
endif
KVERS_MAJ:=$(shell echo $(KVERS) | cut -d. -f1-2)
KINCLUDES:=$(KSRC)/include

# We use the kernel's .config file as an indication that the KSRC
# directory is indeed a valid and configured kernel source (or partial
# source) directory.
#
# We also source it, as it has the format of Makefile variables list.
# Thus we will have many CONFIG_* variables from there.
KCONFIG:=$(KSRC)/.config
ifneq (,$(wildcard $(KCONFIG)))
  HAS_KSRC=yes
  include $(KCONFIG)
else
  HAS_KSRC=no
endif

# Set HOTPLUG_FIRMWARE=no to override automatic building with hotplug support
# if it is enabled in the kernel.

ifeq (yes,$(HAS_KSRC))
  HOTPLUG_FIRMWARE:=$(shell if grep -q '^CONFIG_FW_LOADER=[ym]' $(KCONFIG); then echo "yes"; else echo "no"; fi)
endif

MODULE_ALIASES=wcfxs wctdm8xxp wct2xxp

KMAKE = $(MAKE) -C $(KSRC) ARCH=$(ARCH) SUBDIRS=$(PWD) DAHDI_INCLUDE=$(PWD)/include HOTPLUG_FIRMWARE=$(HOTPLUG_FIRMWARE)

ifneq (,$(wildcard $(DESTDIR)/etc/udev/rules.d))
  DYNFS=yes
endif

ROOT_PREFIX=

CHKCONFIG	:= $(wildcard /sbin/chkconfig)
UPDATE_RCD	:= $(wildcard /usr/sbin/update-rc.d)
ifndef DESTDIR
  ifdef CHKCONFIG
    ADD_INITD	:= $(CHKCONFIG) --add dahdi
  else
    ifdef UPDATE_RCD
      ADD_INITD	:= $(UPDATE_RCD) dahdi defaults 15 30
    endif
  endif
endif

INITRD_DIR	:= $(firstword $(wildcard /etc/rc.d/init.d /etc/init.d))
ifdef INITRD_DIR
  INIT_TARGET	:= $(DESTDIR)$(INITRD_DIR)/dahdi
  COPY_INITD	:= install -D dahdi.init $(INIT_TARGET)
endif
RCCONF_DIR	:= $(firstword $(wildcard /etc/sysconfig /etc/default))

NETSCR_DIR	:= $(firstword $(wildcard /etc/sysconfig/network-scripts ))
ifdef NETSCR_DIR
  NETSCR_TARGET	:= $(DESTDIR)$(NETSCR_DIR)/ifup-hdlc
  COPY_NETSCR	:= install -D ifup-hdlc $(NETSCR_TARGET)
endif

ifneq ($(wildcard .version),)
  DAHDIVERSION:=$(shell cat .version)
else
ifneq ($(wildcard .svn),)
  DAHDIVERSION=$(shell build_tools/make_version . dahdi/linux)
endif
endif

all: modules

modules: prereq
ifeq (no,$(HAS_KSRC))
	echo "You do not appear to have the sources for the $(KVERS) kernel installed."
	exit 1
endif
	$(KMAKE) modules DAHDI_BUILD_ALL=m

include/dahdi/version.h:
	@DAHDIVERSION="${DAHDIVERSION}" build_tools/make_version_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then :; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

prereq: include/dahdi/version.h

stackcheck: checkstack modules
	./checkstack kernel/*.ko kernel/*/*.ko

install: all devices install-modules install-firmware install-include
	@echo "###################################################"
	@echo "###"
	@echo "### DAHDI installed successfully."
	@echo "### If you have not done so before, install init scripts with:"
	@echo "###"
	@echo "###   make config"
	@echo "###"
	@echo "###################################################"

install-modconf:
	build_tools/genmodconf $(BUILDVER) "$(ROOT_PREFIX)" "$(filter-out dahdi dahdi_dummy xpp dahdi_transcode dahdi_dynamic,$(BUILD_MODULES)) $(MODULE_ALIASES)"
	@if [ -d /etc/modutils ]; then \
		/sbin/update-modules ; \
	fi

install-firmware:
ifeq ($(HOTPLUG_FIRMWARE),yes)
	$(MAKE) -C drivers/dahdi/firmware hotplug-install DESTDIR=$(DESTDIR) HOTPLUG_FIRMWARE=$(HOTPLUG_FIRMWARE)
endif

install-include:
	install -D -m 644 include/dahdi/kernel.h $(DESTDIR)/usr/include/dahdi/kernel.h
	install -D -m 644 include/dahdi/user.h $(DESTDIR)/usr/include/dahdi/user.h
# Include any driver-specific header files here
	install -D -m 644 include/dahdi/wctdm_user.h $(DESTDIR)/usr/include/dahdi/wctdm_user.h

devices:
ifneq (yes,$(DYNFS))
	mkdir -p $(DESTDIR)/dev/dahdi
	rm -f $(DESTDIR)/dev/dahdi/ctl
	rm -f $(DESTDIR)/dev/dahdi/channel
	rm -f $(DESTDIR)/dev/dahdi/pseudo
	rm -f $(DESTDIR)/dev/dahdi/timer
	rm -f $(DESTDIR)/dev/dahdi/transcode
	rm -f $(DESTDIR)/dev/dahdi/253
	rm -f $(DESTDIR)/dev/dahdi/252
	rm -f $(DESTDIR)/dev/dahdi/251
	rm -f $(DESTDIR)/dev/dahdi/250
	mknod $(DESTDIR)/dev/dahdi/ctl c 196 0
	mknod $(DESTDIR)/dev/dahdi/transcode c 196 250
	mknod $(DESTDIR)/dev/dahdi/timer c 196 253
	mknod $(DESTDIR)/dev/dahdi/channel c 196 254
	mknod $(DESTDIR)/dev/dahdi/pseudo c 196 255
	N=1; \
	while [ $$N -lt 250 ]; do \
		rm -f $(DESTDIR)/dev/dahdi/$$N; \
		mknod $(DESTDIR)/dev/dahdi/$$N c 196 $$N; \
		N=$$[$$N+1]; \
	done
else # DYNFS
	install -d $(DESTDIR)/etc/udev/rules.d
	build_tools/genudevrules > $(DESTDIR)/etc/udev/rules.d/dahdi.rules
endif

install-udev: devices

uninstall-hotplug:
	$(MAKE) -C drivers/dahdi/firmware hotplug-uninstall DESTDIR=$(DESTDIR)

uninstall-modules:
	@./build_tools/uninstall-modules $(DESTDIR)/lib/modules/$(KVERS) $(ALL_MODULES)

install-modules: # uninstall-modules
	$(KMAKE) INSTALL_MOD_PATH=$(DESTDIR) INSTALL_MOD_DIR=dahdi modules_install
	[ `id -u` = 0 ] && /sbin/depmod -a $(KVERS) || :

config:
ifdef COPY_INITD
	$(COPY_INITD)
endif
ifdef RCCONF_DIR
  ifeq (,$(wildcard $(DESTDIR)$(RCCONF_DIR)/dahdi))
	install -D -m 644 dahdi.sysconfig $(DESTDIR)$(RCCONF_DIR)/dahdi
  endif
endif
ifdef COPY_NETSCR
	$(COPY_NETSCR)
endif
ifdef ADD_INITD
	$(ADD_INITD)
endif
	@echo "DAHDI has been configured."
	@echo ""
	@echo "If you have any DAHDI hardware it is now recommended to "
	@echo "edit /etc/default/dahdi or /etc/sysconfig/dahdi and set there an "
	@echo "optimal value for the variable MODULES."
	@echo ""
	@echo "I think that the DAHDI hardware you have on your system is:"
	@kernel/xpp/utils/dahdi_hardware || true

update:
	@if [ -d .svn ]; then \
		echo "Updating from Subversion..." ; \
		svn update | tee update.out; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -b4- ; \
		fi ; \
		rm -f update.out; \
	else \
		echo "Not under version control";  \
	fi

clean:
	$(KMAKE) clean
	$(MAKE) -C drivers/dahdi/firmware clean

distclean: dist-clean

dist-clean: clean
	@rm -f include/dahdi/version.h
	@$(MAKE) -C drivers/dahdi/firmware dist-clean

.PHONY: distclean dist-clean clean version.h all install devices modules stackcheck install-udev config update install-modules install-include uninstall-modules

endif # ifdef KBUILD_EXTMOD
