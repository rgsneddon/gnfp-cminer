#!/bin/sh
# Example launch for gnfp-cminer 0.5.
# 1) Replace gnfp1YOURADDRESS with your payout address.
# 2) Change .worker to a unique name per box.
# 3) Set --threads to this machine's logical CPUs (no 256 farm cap).
# TLS to de.restoreprivacy.online:1474 is the default.

cd "$(dirname "$0")"
if [ ! -x ./gnfp-cminer ]; then
  echo "gnfp-cminer missing or not executable. Unpack the linux or macos pack, or run make."
  exit 1
fi
exec ./gnfp-cminer --user gnfp1YOURADDRESS.worker --threads 8
