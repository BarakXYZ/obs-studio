#!/bin/sh
set -eu

FRAMEWORKS_DIR="${1:-}"
LIB_FILENAME="${2:-}"

if [ -z "$FRAMEWORKS_DIR" ] || [ -z "$LIB_FILENAME" ]; then
	exit 1
fi

LIB_PATH="${FRAMEWORKS_DIR}/${LIB_FILENAME}"
if [ ! -f "$LIB_PATH" ]; then
	exit 0
fi

SONAME_LINE="$(otool -D "$LIB_PATH" 2>/dev/null | tail -n 1 || true)"
SONAME_BASENAME="${SONAME_LINE##*/}"

if [ -n "$SONAME_BASENAME" ] && [ "$SONAME_BASENAME" != "$LIB_FILENAME" ]; then
	cp -f "$LIB_PATH" "${FRAMEWORKS_DIR}/${SONAME_BASENAME}"
fi

if command -v /usr/bin/codesign >/dev/null 2>&1; then
	/usr/bin/codesign --force --sign - --timestamp=none "$LIB_PATH" >/dev/null 2>&1 || true
	if [ -n "$SONAME_BASENAME" ] && [ "$SONAME_BASENAME" != "$LIB_FILENAME" ]; then
		/usr/bin/codesign --force --sign - --timestamp=none \
			"${FRAMEWORKS_DIR}/${SONAME_BASENAME}" >/dev/null 2>&1 || true
	fi
fi

