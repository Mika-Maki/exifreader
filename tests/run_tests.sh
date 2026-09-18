#!/bin/sh
# End-to-end tests for exifreader.
#
# Usage: tests/run_tests.sh [path-to-binary]
# Builds nothing: run "make" first (or pass a binary path).
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${1:-$ROOT/build/exifreader}
FIX=$ROOT/tests/fixtures
TMP=$(mktemp -d)
PASS=0
FAIL=0
PY=${PYTHON:-python3}

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT INT TERM

if [ ! -x "$BIN" ]; then
    echo "FAIL: binary not found at $BIN (run make first)"
    exit 1
fi
if [ ! -f "$FIX/sample.jpg" ]; then
    echo "generating fixtures..."
    $PY "$ROOT/tests/make_fixtures.py" "$FIX" >/dev/null || exit 1
fi

ok() { PASS=$((PASS + 1)); printf '  ok   %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; }

expect_contains() { # file, needle, label
    if grep -qF -- "$2" "$1"; then ok "$3"; else bad "$3 (missing: $2)"; fi
}

expect_exit() { # expected, actual, label
    if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (expected exit $1, got $2)"; fi
}

echo "exifreader test suite"
echo "binary: $BIN"

# ---------------------------------------------------------------- JPEG tree
"$BIN" "$FIX/sample.jpg" > "$TMP/tree.txt" 2>&1
expect_exit 0 $? "sample.jpg parses cleanly"
expect_contains "$TMP/tree.txt" "TIFF header @ 0xC: II (little-endian)" "little-endian TIFF header"
expect_contains "$TMP/tree.txt" "IFD0" "IFD0 present"
expect_contains "$TMP/tree.txt" "ExifIFD" "Exif sub-IFD present"
expect_contains "$TMP/tree.txt" "GPSIFD" "GPS sub-IFD present"
expect_contains "$TMP/tree.txt" "InteropIFD" "Interop sub-IFD present"
expect_contains "$TMP/tree.txt" "IFD1" "thumbnail IFD present"
expect_contains "$TMP/tree.txt" "Make (0x010F)" "tag name from dictionary"
expect_contains "$TMP/tree.txt" "NIKON CORPORATION" "ASCII value decoded"
expect_contains "$TMP/tree.txt" "0.008 (1/125)" "rational decoded"

# ---------------------------------------------------------------- tags mode
"$BIN" --tags "$FIX/sample.jpg" > "$TMP/tags.txt" 2>&1
expect_contains "$TMP/tags.txt" "IFD0/ExifIFD | ExposureTime (0x829A) | 0.008 (1/125) | RATIONAL[1]" "flat tag line format"
"$BIN" --tags --no-values "$FIX/sample.jpg" > "$TMP/tags_nv.txt" 2>&1
if grep -q "RATIONAL\[1\]" "$TMP/tags_nv.txt" && ! grep -q "1/125" "$TMP/tags_nv.txt"; then
    ok "--no-values omits values"
else
    bad "--no-values omits values"
fi

# ---------------------------------------------------------------- JSON mode
"$BIN" --json "$FIX/sample.jpg" > "$TMP/tree.json" 2>&1
if $PY -c "
import json
d=json.load(open('$TMP/tree.json'))
def walk(n):
    yield n
    for c in n['children']:
        yield from walk(c)
nodes=[x for r in d['exif']['ifds'] for x in walk(r)]
assert d['container']['width']==16
assert d['exif']['tagCount']>40
assert any(n['ifd']=='ExifIFD' for n in nodes)
assert any(n['ifd'].startswith('MakerNote') for n in nodes)
" >/dev/null 2>&1; then
    ok "JSON is valid and contains the expected structure"
else
    bad "JSON is valid and contains the expected structure"
fi
if $PY -c "
import json
d=json.load(open('$TMP/tree.json'))
def walk(n):
    yield n
    for c in n['children']:
        yield from walk(c)
tags=[t for r in d['exif']['ifds'] for n in walk(r) for t in n['tags']]
v=[t for t in tags if t['id']=='0xA002'][0]['value']
assert v==8256, v
" >/dev/null 2>&1; then
    ok "JSON numeric value typed as number"
else
    bad "JSON numeric value typed as number"
fi

# ---------------------------------------------------------------- containers
"$BIN" --summary "$FIX/sample.png" > "$TMP/png.txt" 2>&1
expect_contains "$TMP/png.txt" "PNG 16x16" "PNG container + dimensions"
expect_contains "$TMP/png.txt" "Make=NIKON CORPORATION" "PNG eXIf chunk parsed"
"$BIN" --summary "$FIX/sample.tif" > "$TMP/tif.txt" 2>&1
expect_contains "$TMP/tif.txt" "TIFF, little-endian" "raw TIFF container"
"$BIN" --summary "$FIX/sample.heic" > "$TMP/heic.txt" 2>&1
expect_contains "$TMP/heic.txt" "HEIF, little-endian" "HEIF Exif item located"
"$BIN" --summary "$FIX/bigendian.tif" > "$TMP/be.txt" 2>&1
expect_contains "$TMP/be.txt" "big-endian" "big-endian TIFF endianness"
expect_contains "$TMP/be.txt" "Make=CANON" "big-endian ASCII value"
"$BIN" --summary "$FIX/sample_subifd.jpg" | grep -q "6 IFD" && ok "SubIFD recursion" || bad "SubIFD recursion"

# ------------------------------------------------------------- maker notes
"$BIN" "$FIX/makernote_nikon3.jpg" > "$TMP/mn_nikon.txt" 2>&1
expect_contains "$TMP/mn_nikon.txt" "MakerNote (Nikon type 3)" "Nikon type 3 maker note located"
expect_contains "$TMP/mn_nikon.txt" 'ShutterCount (0x00A7)  : LONG[1] inline = 4242' 'Nikon type 3 tag decoded'
"$BIN" "$FIX/makernote_sony.jpg" > "$TMP/mn_sony.txt" 2>&1
expect_contains "$TMP/mn_sony.txt" "ILCE-7M4" "vendor maker note (plain IFD) decoded"

# ---------------------------------------------------------------- robustness
"$BIN" "$FIX/cycle.tif" > "$TMP/cycle.txt" 2>&1
expect_exit 3 $? "IFD cycle detected (exit 3)"
expect_contains "$TMP/cycle.txt" "already visited" "cycle warning text"
"$BIN" "$FIX/badcount.tif" > "$TMP/badcount.txt" 2>&1
expect_contains "$TMP/badcount.txt" "claims 65535 entries" "absurd entry count reported"
"$BIN" "$FIX/badpointer.tif" > "$TMP/badptr.txt" 2>&1
expect_contains "$TMP/badptr.txt" "out of range" "out-of-file payload reported"
"$BIN" "$FIX/truncated.jpg" > "$TMP/trunc.txt" 2>&1
expect_contains "$TMP/trunc.txt" "no EXIF data" "truncated JPEG handled"
"$BIN" "$FIX/header_only.jpg" > "$TMP/head.txt" 2>&1
expect_contains "$TMP/head.txt" "container: JPEG" "header-only JPEG handled"
"$BIN" "$FIX/empty.bin" > "$TMP/empty.txt" 2>&1
expect_contains "$TMP/empty.txt" "container: unknown" "empty input handled"
"$BIN" "$FIX/notimage.bin" > "$TMP/notimg.txt" 2>&1
expect_contains "$TMP/notimg.txt" "no EXIF data" "non-image input handled"
"$BIN" "$FIX/nometa.jpg" > "$TMP/nometa.txt" 2>&1
expect_contains "$TMP/nometa.txt" "container: JPEG" "JPEG without EXIF"
"$BIN" /nonexistent/file.jpg > "$TMP/missing.txt" 2>&1
expect_exit 1 $? "missing file reports failure"

# ---------------------------------------------------------------- plumbing
cat "$FIX/sample.jpg" | "$BIN" - > "$TMP/stdin.txt" 2>&1
expect_contains "$TMP/stdin.txt" "NIKON" "reads from stdin"
mkdir -p "$TMP/out"
"$BIN" --extract "$TMP/out" "$FIX/sample.jpg" > "$TMP/extract.txt" 2>&1
if ls "$TMP/out" | grep -q thumb; then ok "thumbnail extraction writes a file"; else bad "thumbnail extraction writes a file"; fi
expect_contains "$TMP/extract.txt" "extracted thumbnail" "extraction reported in output"
"$BIN" --list-tags gps > "$TMP/list.txt" 2>&1
expect_contains "$TMP/list.txt" "GPSLatitudeRef" "--list-tags gps"
"$BIN" --list-tags nikon | grep -q "MakerNoteVersion" && ok "--list-tags nikon" || bad "--list-tags nikon"
"$BIN" --help | grep -q "Information-Tree" && ok "--help" || bad "--help"
"$BIN" --version | grep -q "exifreader" && ok "--version" || bad "--version"
"$BIN" --bogus-option > /dev/null 2>&1
expect_exit 1 $? "unknown option rejected"
"$BIN" > /dev/null 2>&1
expect_exit 1 $? "no arguments prints usage and fails"

echo
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ]
