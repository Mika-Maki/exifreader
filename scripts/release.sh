#!/usr/bin/env sh
# release.sh - cut a version: bump, changelog, commit, tag, push.
#
# Usage:
#   scripts/release.sh 1.1.0
#   scripts/release.sh patch|minor|major
#   scripts/release.sh 1.1.0 --no-push --dry-run
#
# The version is stored in three places and this script keeps them in sync:
#   src/main.cpp   const char* kVersion = "x.y.z";
#   CMakeLists.txt project(exifreader VERSION x.y.z ...)
#   CHANGELOG.md   ## [x.y.z] - YYYY-MM-DD
set -eu

cd "$(dirname "$0")/.."

DRY_RUN=0
PUSH=1
while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        --no-push) PUSH=0; shift ;;
        -h|--help) sed -n 's/^#   //p' "$0"; exit 0 ;;
        -*) echo "release.sh: unknown option '$1'" >&2; exit 1 ;;
        *)  REQUEST=${1:-}; shift ;;
    esac
done

[ -n "${REQUEST:-}" ] || { echo "release.sh: give a version or bump kind" >&2; exit 1; }

CURRENT=$(sed -n 's/.*kVersion = "\([^"]*\)".*/\1/p' src/main.cpp)
[ -n "$CURRENT" ] || { echo "release.sh: cannot read the current version" >&2; exit 1; }

major=${CURRENT%%.*}
rest=${CURRENT#*.}
minor=${rest%%.*}
patch=${rest#*.}

case "$REQUEST" in
    major) NEXT="$((major + 1)).0.0" ;;
    minor) NEXT="$major.$((minor + 1)).0" ;;
    patch) NEXT="$major.$minor.$((patch + 1))" ;;
    [0-9]*.[0-9]*.[0-9]*) NEXT="$REQUEST" ;;
    *) echo "release.sh: '$REQUEST' is not a version or bump kind" >&2; exit 1 ;;
esac

TAG="v$NEXT"
DATE=$(date -u +%Y-%m-%d)
echo "release.sh: $CURRENT -> $NEXT ($TAG, $DATE)"

[ "$DRY_RUN" = 1 ] || [ -z "$(git status --porcelain)" ] || {
    echo "release.sh: working tree is dirty; commit or stash first" >&2; exit 1; }

run() {
    if [ "$DRY_RUN" = 1 ]; then printf '  [dry-run] %s\n' "$*"; else "$@"; fi
}

# --- bump the version ------------------------------------------------------
ed() { # file, sed-expression
    if [ "$DRY_RUN" = 1 ]; then
        echo "  [dry-run] edit $1: $2"
        return
    fi
    tmp="$1.tmp.$$"
    sed "$2" "$1" > "$tmp" && mv "$tmp" "$1"
}

ed src/main.cpp    "s/kVersion = \"$CURRENT\"/kVersion = \"$NEXT\"/"
ed CMakeLists.txt  "s/VERSION $CURRENT/VERSION $NEXT/"

# The section insert and the link refresh are deliberately separate: a maintainer
# may already have written the release notes by hand, and the compare links still
# have to be refreshed in that case.
insert=1
grep -q "^## \[$NEXT\]" CHANGELOG.md && insert=0

if [ "$DRY_RUN" = 1 ]; then
    [ "$insert" = 1 ] && echo "  [dry-run] add ## [$NEXT] - $DATE to CHANGELOG.md"
    echo "  [dry-run] point the [Unreleased] compare link at v$NEXT"
else
    slug=$(git remote get-url origin 2>/dev/null | sed -e 's|.*github\.com[:/]||' -e 's|\.git$||')
    python3 - "$NEXT" "$DATE" "$slug" "$insert" <<'PY'
import re
import sys

next_version, date, slug, insert = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == '1'
path = 'CHANGELOG.md'
text = open(path, encoding='utf-8').read()

if insert:
    # Insert the new section immediately before the first released version, so
    # the [Unreleased] heading and everything under it survive untouched. The
    # previous implementation spliced from '## [Unreleased]' to the first
    # '## [x.y.z]' and silently deleted whatever sat between the two.
    section = (f'## [{next_version}] - {date}\n\n'
               '### Added\n\n- TODO: describe the release.\n\n'
               '### Changed\n\n- TODO\n\n'
               '### Fixed\n\n- TODO\n\n')
    match = re.search(r'(?m)^## \[\d', text)
    if match:
        text = text[:match.start()] + section + text[match.start():]
    else:
        text = text.rstrip('\n') + '\n\n' + section

# Keep the compare links honest: [Unreleased] has to compare from the version we
# just cut, and the new version needs its release link.
if slug:
    text = re.sub(r'(?m)^\[Unreleased\]:.*$',
                  f'[Unreleased]: https://github.com/{slug}/compare/v{next_version}...HEAD',
                  text)
    if not re.search(r'(?m)^\[' + re.escape(next_version) + r'\]:', text):
        text = re.sub(r'(?m)^(\[Unreleased\]:.*)$',
                      r'\1\n[' + next_version + f']: https://github.com/{slug}/releases/tag/v{next_version}',
                      text, count=1)

open(path, 'w', encoding='utf-8').write(text)
PY
fi

# --- commit, tag, push -----------------------------------------------------
run git add src/main.cpp CMakeLists.txt CHANGELOG.md
run git commit -q -m "chore(release): $TAG"
run git tag -a "$TAG" -m "exifreader $NEXT"
if [ "$PUSH" = 1 ]; then
    run git push
    run git push origin "$TAG"
    echo "release.sh: pushed $TAG - the Release workflow will publish the binaries"
else
    echo "release.sh: commit and tag created locally; push with 'git push --follow-tags'"
fi
