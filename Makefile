# gnfp-cminer — community GNFPHash CPU miner (declared 5% dual-login fee)
VERSION ?= 1.1.0
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)
ifeq ($(OPENSSL_PREFIX),)
  SSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
  SSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
else
  SSL_CFLAGS := -I$(OPENSSL_PREFIX)/include
  SSL_LIBS := -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
endif
LIBS ?= $(SSL_LIBS) -pthread

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
	sh tests/test_public_host.sh

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
	cp README.md LICENSE Makefile pack-unavailable.log dist/gnfp-cminer-$(VERSION)/
	cp -R src tests pack dist/gnfp-cminer-$(VERSION)/
	tar -C dist -czf dist/gnfp-cminer-$(VERSION)-linux.tar.gz gnfp-cminer-$(VERSION)
	@echo packed dist/gnfp-cminer-$(VERSION)-linux.tar.gz

pack-windows:
	rm -rf dist/gnfp-cminer-$(VERSION)
	mkdir -p dist/gnfp-cminer-$(VERSION)
	cp README.md LICENSE Makefile pack-unavailable.log dist/gnfp-cminer-$(VERSION)/
	cp -R src tests pack dist/gnfp-cminer-$(VERSION)/
	rm -f dist/gnfp-cminer-$(VERSION)-windows.zip
	cd dist && zip -r -q gnfp-cminer-$(VERSION)-windows.zip gnfp-cminer-$(VERSION)
	@echo packed dist/gnfp-cminer-$(VERSION)-windows.zip

pack-all: pack-macos pack-linux pack-windows

clean:
	rm -f $(BIN) $(BIN).exe
	rm -rf dist/gnfp-cminer-$(VERSION)
