ifneq ($(KBUILD_EXTMOD),)
# We only get here on kernels 2.6.0-2.6.9 .
# For newer kernels, Kbuild will be included directly by the kernel
# build system.
include $(src)/Kbuild

else

tests: codec_test

codec_test: codec_test.c ../zaptel.h
	$(CC) -o $@ $< $(CFLAGS)

clean:
	rm -rf codec_test
endif
