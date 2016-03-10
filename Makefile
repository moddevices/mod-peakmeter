
# export CXX=/tmp/mbs/modduo/rootfs/host/usr/bin/arm-mod-linux-gnueabihf-g++
# export PATH=/tmp/mbs/modduo/rootfs/host/usr/bin:$PATH
# export PKG_CONFIG_PATH=/tmp/mbs/modduo/rootfs/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/pkgconfig/

CC  ?= gcc
CXX ?= g++

BASEFLAGS = -O3 -Wall -Wextra -fPIC

CFLAGS   += $(BASEFLAGS) -std=gnu99
CXXFLAGS += $(BASEFLAGS) -std=gnu++11
LDFLAGS  += -Wl,--no-undefined

mod-peakmeter: mod-peakmeter.cpp jacktools/*
	$(CXX) $< $(CXXFLAGS) $(LDFLAGS) $(shell pkg-config --cflags --libs jack) -o $@

clean:
	rm -f mod-peakmeter
