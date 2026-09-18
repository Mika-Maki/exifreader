#!/usr/bin/env sh
# publish.sh - put this repository on GitHub.
#
# The repository ships with the placeholder "OWNER" in badge URLs, clone URLs
# and advisory links. This script rewrites those placeholders to the real
# account, commits the result, and pushes - creating the GitHub repository first
# when the `gh` CLI is available.
#
# Usage:
#   scripts/publish.sh --owner MYUSER [--repo exifreader] [--private] [--tag v1.0.0]
#   scripts/publish.sh --owner MYUSER --dry-run
#
# Options:
#   --owner NAME    GitHub user or organisation (required)
#   --repo NAME     repository name                  (default: exifreader)
#   --tag TAG       create and push this tag         (default: none)
#   --remote NAME   git remote to use                (default: origin)
#   --branch NAME   branch to publish                (default: main)
#   --url URL       remote URL, for GitHub Enterprise or a local test remote
#   --public/--private   repository visibility       (default: public)
#   --no-create     do not create the GitHub repository, only push
#   --no-rewrite    do not touch the OWNER placeholders
#   --dry-run       print what would happen, change nothing
#
# Environment fallbacks: OWNER, REPO_NAME, VISIBILITY, REMOTE, BRANCH, TAG, URL.
set -eu

REPO_NAME=${REPO_NAME:-exifreader}
OWNER=${OWNER:-}
VISIBILITY=${VISIBILITY:-public}
REMOTE=${REMOTE:-origin}
BRANCH=${BRANCH:-main}
TAG=${TAG:-}
URL=${URL:-}
DRY_RUN=0
CREATE=1
REWRITE=1

usage() {
    sed -n 's/^#   //p' "$0"
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --owner)      OWNER=${2:?--owner needs a value}; shift 2 ;;
        --repo)       REPO_NAME=${2:?--repo needs a value}; shift 2 ;;
        --tag)        TAG=${2:?--tag needs a value}; shift 2 ;;
        --remote)     REMOTE=${2:?--remote needs a value}; shift 2 ;;
        --branch)     BRANCH=${2:?--branch needs a value}; shift 2 ;;
        --url)        URL=${2:?--url needs a value}; shift 2 ;;
        --private)    VISIBILITY=private; shift ;;
        --public)     VISIBILITY=public; shift ;;
        --no-create)  CREATE=0; shift ;;
        --no-rewrite) REWRITE=0; shift ;;
        --dry-run)    DRY_RUN=1; shift ;;
        -h|--help)    usage 0 ;;
        *)            echo "publish.sh: unknown option '$1'" >&2; usage 1 ;;
    esac
done

cd "$(dirname "$0")/.."
ROOT=$(pwd)
echo "publish.sh: repository root $ROOT"

[ -n "$OWNER" ] || { echo "publish.sh: --owner (or \$OWNER) is required" >&2; exit 1; }
[ -n "$URL" ] || URL="https://github.com/$OWNER/$REPO_NAME.git"

run() {
    if [ "$DRY_RUN" = 1 ]; then
        printf '  [dry-run] %s\n' "$*"
    else
        "$@"
    fi
}

# Paths that never hold placeholders meant for the published repository:
# version control, build output, release artifacts, and this script itself
# (whose sed expressions contain the literal placeholder).
FIND_PLACEHOLDER='github.com/OWNER/exifreader'
EXCLUDES='--exclude-dir=.git --exclude-dir=build --exclude-dir=build-cmake --exclude-dir=dist --exclude-dir=scripts'

# --- 1. rewrite the OWNER placeholder -------------------------------------
if [ "$REWRITE" = 1 ]; then
    echo "-> rewriting placeholders to $OWNER/$REPO_NAME"
    # Discovery, not a fixed list: every tracked text file is covered, including
    # translations added later.
    files=$(grep -RIl -e 'github.com/OWNER' -e '@OWNER' . $EXCLUDES 2>/dev/null || true)
    if [ -z "$files" ]; then
        echo "   nothing to patch"
    fi
    for f in $files; do
        if [ "$DRY_RUN" = 1 ]; then
            echo "  [dry-run] patch ${f#./}"
            continue
        fi
        tmp="$f.tmp.$$"
        sed -e "s|github.com/OWNER/|github.com/$OWNER/|g" \
            -e "s|@OWNER|@$OWNER|g" "$f" > "$tmp"
        mv "$tmp" "$f"
        echo "   patched ${f#./}"
    done

    # A leftover placeholder in the working tree means a broken link would ship.
    leftover=$(grep -RIl -e "$FIND_PLACEHOLDER" . $EXCLUDES 2>/dev/null || true)
    if [ -n "$leftover" ]; then
        echo "   WARNING: these files still reference $FIND_PLACEHOLDER:" >&2
        printf '     %s\n' $leftover >&2
        if [ "$DRY_RUN" = 0 ]; then
            echo "   refusing to publish with broken links" >&2
            exit 1
        fi
    fi
fi

# --- 2. git identity and repository ---------------------------------------
if [ ! -d .git ]; then
    echo "-> git init"
    run git init -q
fi
if ! git config user.name >/dev/null 2>&1; then
    echo "   setting a local commit identity (change it if you like)"
    run git config user.name  "exifreader bot"
    run git config user.email "exifreader@users.noreply.github.com"
fi

# --- 3. commit -------------------------------------------------------------
run git add -A
if git diff --cached --quiet 2>/dev/null; then
    echo "-> nothing to commit"
else
    run git commit -q -m "chore(repo): point the repository at $OWNER/$REPO_NAME

Rewrite the OWNER placeholder in the badge, clone and advisory links after
publishing.

Assisted-by: DeepSeek Harness (deepseek-flash)"
fi
run git branch -M "$BRANCH"

# --- 4. tag ----------------------------------------------------------------
if [ -n "$TAG" ]; then
    if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
        echo "-> tag $TAG already exists, leaving it alone"
    else
        run git tag -a "$TAG" -m "exifreader $TAG"
    fi
fi

# --- 5. remote -------------------------------------------------------------
set_remote() { # name, url
    if git remote get-url "$1" >/dev/null 2>&1; then
        run git remote set-url "$1" "$2"
    else
        run git remote add "$1" "$2"
    fi
}

if [ "$CREATE" = 1 ] && command -v gh >/dev/null 2>&1; then
    if gh repo view "$OWNER/$REPO_NAME" >/dev/null 2>&1; then
        echo "-> $OWNER/$REPO_NAME already exists on GitHub"
        set_remote "$REMOTE" "$URL"
    else
        echo "-> creating $OWNER/$REPO_NAME ($VISIBILITY)"
        run gh repo create "$OWNER/$REPO_NAME" "--$VISIBILITY" \
            --source=. --remote="$REMOTE" \
            --description "Dependency-free C++17 EXIF/TIFF information-tree reader (AI-generated)"
    fi
    run gh repo edit "$OWNER/$REPO_NAME" \
        --add-topic exif --add-topic metadata --add-topic cpp17 --add-topic tiff \
        --add-topic image-forensics --add-topic ai-generated 2>/dev/null || true
else
    if [ "$CREATE" = 1 ]; then
        echo "-> no 'gh' CLI found; create https://github.com/new?name=$REPO_NAME first,"
        echo "   or install gh (https://cli.github.com) and re-run"
    fi
    set_remote "$REMOTE" "$URL"
    echo "-> pushing to $URL"
fi

# --- 6. push ---------------------------------------------------------------
run git push -u "$REMOTE" "$BRANCH"
if [ -n "$TAG" ]; then
    run git push "$REMOTE" "$TAG"
    echo "-> the Release workflow will build and attach the binaries for $TAG"
fi

echo "publish.sh: done"
