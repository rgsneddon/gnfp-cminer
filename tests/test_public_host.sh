#!/bin/sh
# Fail-closed: unauthenticated GitHub must see this repo (criterion 3 public host).
set -e
URL="https://github.com/rgsneddon/gnfp-cminer"
hdr=$(curl -sI "$URL") || {
  echo "curl failed for $URL" >&2
  exit 1
}
code=$(printf '%s\n' "$hdr" | awk 'NR==1 { print $2 }')
if [ "$code" != "200" ]; then
  echo "unauth GET $URL -> HTTP $code (want 200)" >&2
  printf '%s\n' "$hdr" >&2
  exit 1
fi
view=$(gh repo view rgsneddon/gnfp-cminer --json isPrivate,visibility,url)
printf '%s\n' "$view" | grep -q '"isPrivate":false' || {
  echo "gh isPrivate is not false: $view" >&2
  exit 1
}
printf '%s\n' "$view" | grep -q '"visibility":"PUBLIC"' || {
  echo "gh visibility is not PUBLIC: $view" >&2
  exit 1
}
echo "unauth GET $URL"
printf '%s\n' "$hdr" | awk 'NR<=8'
echo
echo "$view"
if [ -n "$CMINER_PACKS_OUT" ]; then
  {
    echo "unauth GET $URL -> HTTP $code"
    printf '%s\n' "$hdr" | awk 'NR<=8'
    echo
    echo "$view"
    echo
    echo "https://github.com/rgsneddon/gnfp-cminer/releases/tag/v1.1.0"
    echo "gnfp-cminer-1.1.0-macos.tar.gz"
    echo "gnfp-cminer-1.1.0-linux.tar.gz"
    echo "gnfp-cminer-1.1.0-windows.zip"
  } > "$CMINER_PACKS_OUT"
fi
echo "public host ok"
