# mupen64plus-VR — standalone headless VR unit-test harness.
#
# Compiles src/vr/*.c + tests/vr/*.c with -DENABLE_VR -DVR_TESTING and NO core
# link (the #ifndef VR_TESTING seam excludes every g_dev/RDRAM/OpenXR dependency),
# so `make -C tests/vr run` works on any box with a C compiler — no rig, no SDL,
# no OpenXR. Packet S4 may extend this + wire it into CI.
#
#   make -C tests/vr run     # build + run
#   make -C tests/vr clean

CC      ?= gcc
ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC     := $(wildcard $(ROOT)/src/vr/*.c) $(wildcard $(ROOT)/tests/vr/*.c)
CFLAGS  := -std=gnu99 -Wall -Wextra -Wno-unused-parameter \
           -DENABLE_VR -DVR_TESTING \
           -I$(ROOT)/src -I$(ROOT)/src/api -I$(ROOT)/tests/vr
LDLIBS  := -lm

vrtest: $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LDLIBS) -o $@

run: vrtest
	./vrtest

clean:
	rm -f vrtest

.PHONY: run clean
