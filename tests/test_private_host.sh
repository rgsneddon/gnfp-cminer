#!/bin/sh
# Operator specified this repo stays private. Fail if it is public.
set -e
URL="https://github.com/rgsneddon/gnfp-cminer"
hdr=$(curl -sI "$URL") || {
  echo "curl failed for $URL" >&2
  exit 1
}
code=$(printf '%s\n' "$hdr" | awk 'NR==1 { print $2 }')
if [ "$code" != "404" ]; then
  echo "unauth GET $URL -> HTTP $code (want 404; repo must stay private)" >&2
  printf '%s\n' "$hdr" >&2
  exit 1
fi
view=$(gh repo view rgsneddon/gnfp-cminer --json isPrivate,visibility,url)
printf '%s\n' "$view" | grep -q '"isPrivate":true' || {
  echo "gh isPrivate is not true: $view" >&2
  exit 1
}
printf '%s\n' "$view" | grep -q '"visibility":"PRIVATE"' || {
  echo "gh visibility is not PRIVATE: $view" >&2
  exit 1
}
echo "unauth GET $URL -> HTTP $code"
printf '%s\n' "$hdr" | awk 'NR<=6'
echo
echo "$view"
if [ -n "$CMINER_PACKS_OUT" ]; then
  {
    echo "visibility: PRIVATE (operator specified; do not make public)"
    echo "unauth GET $URL -> HTTP $code"
    printf '%s\n' "$hdr" | awk 'NR<=6'
    echo
    echo "$view"
    echo
    echo "private auth-only packs on tag v1.1.0 (not a public pin)"
    echo "gnfp-cminer-1.1.0-macos.tar.gz"
    echo "gnfp-cminer-1.1.0-linux.tar.gz"
    echo "gnfp-cminer-1.1.0-windows.zip"
  } > "$CMINER_PACKS_OUT"
fi
echo "private host ok"
