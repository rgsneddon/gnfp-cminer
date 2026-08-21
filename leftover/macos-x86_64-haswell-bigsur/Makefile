# Native gnfp_cminer for this MacBook Air (MacBookAir6,1, Haswell i5-4250U).
# Port of https://github.com/rvp-design/gnfp_cminer (Linux ELF, no source).
SDKROOT ?= $(shell xcrun --show-sdk-path)
CC ?= clang
CFLAGS ?= -O3 -march=native -mavx2 -mbmi2 -mbmi -mpopcnt -pthread \
	-fomit-frame-pointer -fno-stack-protector -Wall -Wextra -std=c11 \
	-Wno-deprecated-declarations \
	-isysroot $(SDKROOT) -Iinclude -Isrc
LDFLAGS ?= -pthread -framework Security -framework CoreFoundation \
	-isysroot $(SDKROOT)

BIN := gnfp_cminer
SRC := src/gnfp_cminer.c src/gnfphash.c src/sha256.c src/apple_ssl.c

.PHONY: all clean selftest bench

all: $(BIN)

$(BIN): $(SRC) src/gnfphash.h src/sha256.h include/openssl/ssl.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

selftest: $(BIN)
	./$(BIN) --selftest

bench: $(BIN)
	./$(BIN) --bench 3 --threads 2

clean:
	rm -f $(BIN)
