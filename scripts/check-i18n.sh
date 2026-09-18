#!/usr/bin/env sh
# check-i18n.sh - verify that every document with a translation links to it.
#
# Pairs are discovered, not listed: a Markdown file that has a counterpart in
# another language must carry a jump link to that counterpart (and the
# counterpart must link back). A document with no counterpart must point readers
# at the translated documentation instead.
#
# The policy is documented in docs/i18n.md.
set -eu

cd "$(dirname "$0")/.."

# The jump link is required near the top of the file, not buried in the middle.
MAX_LINK_LINE=6

total=0
failures=0

# link_line <file> <expected-link> -> prints the line number, or nothing
link_line() {
    grep -nF "]($2)" "$1" 2>/dev/null | head -1 | cut -d: -f1 || true
}

# check_pair <en> <translated> <link-in-en> <link-in-translated>
check_pair() {
    en="$1"
    tr="$2"
    en_link="$3"
    tr_link="$4"
    total=$((total + 1))

    if [ ! -f "$en" ]; then
        echo "  FAIL missing source page   $en"
        failures=$((failures + 1))
        return
    fi
    if [ ! -f "$tr" ]; then
        echo "  FAIL missing translation   $tr  (source: $en)"
        failures=$((failures + 1))
        return
    fi

    ok=1
    line=$(link_line "$en" "$en_link")
    if [ -z "$line" ]; then
        echo "  FAIL no jump link in       $en  (expected a link to $en_link)"
        ok=0
    elif [ "$line" -gt "$MAX_LINK_LINE" ]; then
        echo "  FAIL jump link too deep    $en  (line $line, expected <= $MAX_LINK_LINE)"
        ok=0
    fi

    line=$(link_line "$tr" "$tr_link")
    if [ -z "$line" ]; then
        echo "  FAIL no jump link in       $tr  (expected a link to $tr_link)"
        ok=0
    elif [ "$line" -gt "$MAX_LINK_LINE" ]; then
        echo "  FAIL jump link too deep    $tr  (line $line, expected <= $MAX_LINK_LINE)"
        ok=0
    fi

    if [ "$ok" = 1 ]; then
        echo "  ok   $en <-> $tr"
    else
        failures=$((failures + 1))
    fi
}

echo "i18n check: jump links between language versions"

# --- 1. root pairs: NAME.md <-> NAME.zh-CN.md ------------------------------
for tr in *.zh-CN.md; do
    [ -f "$tr" ] || continue
    en="${tr%.zh-CN.md}.md"
    check_pair "$en" "$tr" "$tr" "$en"
done

# --- 2. docs/ top-level pages (they do not follow the directory convention) -
check_pair docs/README.md docs/zh-CN/README.md zh-CN/README.md ../README.md
check_pair docs/i18n.md docs/zh-CN/i18n.md zh-CN/i18n.md ../i18n.md

# --- 3. docs/en/*.md <-> docs/zh-CN/*.md -----------------------------------
for en in docs/en/*.md; do
    [ -f "$en" ] || continue
    base=$(basename "$en")
    check_pair "$en" "docs/zh-CN/$base" "../zh-CN/$base" "../en/$base"
done

# --- 4. no orphan translations ---------------------------------------------
for tr in docs/zh-CN/*.md; do
    [ -f "$tr" ] || continue
    base=$(basename "$tr")
    case "$base" in
        README.md|i18n.md) continue ;;
    esac
    if [ ! -f "docs/en/$base" ]; then
        echo "  FAIL no English source for $tr"
        failures=$((failures + 1))
    fi
done

# --- 5. documents with no translation must point at the translated docs -----
orphans=0
for f in $(find . -name '*.md' -not -path './.git/*' | sort); do
    case "$f" in
        *.zh-CN.md|./docs/zh-CN/*) continue ;;
        ./README.md|./docs/README.md|./docs/i18n.md|./docs/en/*) continue ;;
    esac
    if [ ! -f "${f%.md}.zh-CN.md" ] && [ ! -f "docs/zh-CN/$(basename "$f")" ]; then
        orphans=$((orphans + 1))
        if grep -q 'zh-CN' "$f"; then
            echo "  ok   $f (no translation; links to the Chinese docs)"
        else
            echo "  FAIL no route to the translated docs in $f"
            failures=$((failures + 1))
        fi
    fi
done

echo
if [ "$failures" -gt 0 ]; then
    echo "i18n check: $failures problem(s) across $total language pair(s), $orphans untranslated document(s)"
    exit 1
fi
echo "i18n check: $total language pair(s) ok, $orphans untranslated document(s) link onward"
