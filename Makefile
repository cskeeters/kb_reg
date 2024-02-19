DESTDIR ?= /usr/local
bindir ?= $(DESTDIR)/bin

CC=g++
PKG_CONFIG ?= pkg-config
INSTALL ?= install

PKGS = libusb-1.0 tomlplusplus spdlog hidapi cxxopts

CXXFLAGS=-std=c++20 -g -Wall -Wextra `pkg-config --cflags $(PKGS)`
LDFLAGS=`pkg-config --libs $(PKGS)`

default: kb_detect kb_reg

install: kb_detect kb_reg
	$(INSTALL) -m 0755 kb_detect $(bindir)/kb_detect
	$(INSTALL) -m 0755 kb_reg $(bindir)/kb_reg

uninstall:
	$(RM) $(bindir)/kb_detect
	$(RM) $(bindir)/kb_reg

clean:
	rm src/*.o
	rm kb_detect
	rm kb_reg

%.o: %.cc
	$(CC) $(OUTPUT_OPTION) $(CXXFLAGS) -c $< $(DEPFLAGS)

kb_detect: src/kb_detect.o src/utf8util.o
	$(CC) $(OUTPUT_OPTION) $(LDFLAGS) $^ $(DEPFLAGS)

kb_reg: src/kb_reg.o src/utf8util.o
	$(CC) $(OUTPUT_OPTION) $(LDFLAGS) $^ $(DEPFLAGS)
