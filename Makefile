# gnfp-cminer — official $GNFP CPU miner (declared 5% dual-login miner fee)
VERSION ?= 1.1.1
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
ifeq ($(OPENSSL_PREFIX),)
  SSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
  SSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
  ifeq ($(strip $(SSL_LIBS)),)
    SSL_LIBS := -lssl -lcrypto
  endif
else
  SSL_CFLAGS := -I$(OPENSSL_PREFIX)/include
  SSL_LIBS := -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
endif
LIBS ?= $(SSL_LIBS) -pthread
LINUX_BIN ?=
WIN_BIN ?=
WIN_DLLS ?=

BIN := gnfp-cminer
SRC := src/gnfp_cminer.c src/gnfp_hash.c

.PHONY: all clean selftest test pack-macos pack-linux pack-windows pack-all

all: $(BIN)

$(BIN): $(SRC) src/gnfp_hash.h
	$(CC) $(CFLAGS) $(SSL_CFLAGS) -o $@ $(SRC) $(LIBS)

selftest: $(BIN)
	./$(BIN) --selftest

test: selftest
	node tests/test_admit.mjs
	node tests/test_local_stratum.mjs
	node tests/test_readme.mjs
	sh tests/test_private_host.sh

pack-macos: $(BIN)
	rm -rf dist/gnfp-cminer-$(VERSION)
	mkdir -p dist/gnfp-cminer-$(VERSION)
	cp $(BIN) README.md LICENSE Makefile dist/gnfp-cminer-$(VERSION)/
	cp -R src tests pack dist/gnfp-cminer-$(VERSION)/
	tar -C dist -czf dist/gnfp-cminer-$(VERSION)-macos.tar.gz gnfp-cminer-$(VERSION)
	@echo packed dist/gnfp-cminer-$(VERSION)-macos.tar.gz

pack-linux:
	rm -rf dist/gnfp-cminer-$(VERSION)
	mkdir -p dist/gnfp-cminer-$(VERSION)
	cp README.md LICENSE Makefile dist/gnfp-cminer-$(VERSION)/
	cp -R src tests pack dist/gnfp-cminer-$(VERSION)/
	@if [ -n "$(LINUX_BIN)" ] && [ -f "$(LINUX_BIN)" ]; then \
	  cp "$(LINUX_BIN)" dist/gnfp-cminer-$(VERSION)/gnfp-cminer; \
	  chmod +x dist/gnfp-cminer-$(VERSION)/gnfp-cminer; \
	  echo included linux ELF $(LINUX_BIN); \
	else \
	  cp pack-unavailable.log dist/gnfp-cminer-$(VERSION)/; \
	  echo linux pack is source — set LINUX_BIN=path-to-elf; \
	fi
	tar -C dist -czf dist/gnfp-cminer-$(VERSION)-linux.tar.gz gnfp-cminer-$(VERSION)
	@echo packed dist/gnfp-cminer-$(VERSION)-linux.tar.gz

pack-windows:
	rm -rf dist/gnfp-cminer-$(VERSION)
	mkdir -p dist/gnfp-cminer-$(VERSION)
	cp README.md LICENSE Makefile dist/gnfp-cminer-$(VERSION)/
	cp -R src tests pack dist/gnfp-cminer-$(VERSION)/
	@if [ -n "$(WIN_BIN)" ] && [ -f "$(WIN_BIN)" ]; then \
	  cp "$(WIN_BIN)" dist/gnfp-cminer-$(VERSION)/gnfp-cminer.exe; \
	  echo included windows PE $(WIN_BIN); \
	  if [ -n "$(WIN_DLLS)" ]; then cp $(WIN_DLLS) dist/gnfp-cminer-$(VERSION)/; fi; \
	else \
	  cp pack-unavailable.log dist/gnfp-cminer-$(VERSION)/; \
	  echo windows pack is source — set WIN_BIN=path-to-exe; \
	fi
	rm -f dist/gnfp-cminer-$(VERSION)-windows.zip
	cd dist && zip -r -q gnfp-cminer-$(VERSION)-windows.zip gnfp-cminer-$(VERSION)
	@echo packed dist/gnfp-cminer-$(VERSION)-windows.zip

pack-all: pack-macos pack-linux pack-windows

clean:
	rm -f $(BIN) $(BIN).exe
	rm -rf dist/gnfp-cminer-$(VERSION)
