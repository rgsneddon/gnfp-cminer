#!/bin/sh
# Official public pin. Fail if the host is still private / 404.
set -e
URL="https://github.com/rgsneddon/gnfp-cminer"
hdr=$(curl -sI "$URL") || {
  echo "curl failed for $URL" >&2
  exit 1
}
code=$(printf '%s\n' "$hdr")
http=$(printf '%s\n' "$hdr" | awk 'NR==1 { print $2 }')
if [ "$http" = "404" ]; then
  echo "unauth GET $URL -> HTTP $http (want not 404; repo must be public)" >&2
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
echo "unauth GET $URL -> HTTP $http"
printf '%s\n' "$hdr" | awk 'NR<=6'
echo
echo "$view"
if [ -n "$CMINER_PACKS_OUT" ]; then
  {
    echo "visibility: PUBLIC (official C miner pin)"
    echo "unauth GET $URL -> HTTP $http"
    printf '%s\n' "$hdr" | awk 'NR<=6'
    echo
    echo "$view"
    echo
    echo "public packs on tag v1.1.0"
    echo "gnfp-cminer-1.1.0-macos.tar.gz"
    echo "gnfp-cminer-1.1.0-linux.tar.gz"
    echo "gnfp-cminer-1.1.0-windows.zip"
  } > "$CMINER_PACKS_OUT"
fi
echo "public host ok"
