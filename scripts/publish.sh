#!/usr/bin/env sh
# publish.sh - put this repository on GitHub.
#
# The repository ships with the placeholder "OWNER" in badge URLs, clone URLs
# and advisory links. This script rewrites those placeholders to the real
# account, commits the result, and pushes (creating the GitHub repository first
# when the `gh` CLI is available).
#
# Usage:
#   scripts/publish.sh --owner MYUSER [--repo exifreader] [--private] [--tag v1.0.0]
#   scripts/publish.sh --owner MYUSER --dry-run
#
# Environment fallbacks: OWNER, REPO_NAME, VISIBILITY, REMOTE, BRANCH, TAG.
set -eu

REPO_NAME=${REPO_NAME:-exifreader}
OWNER=${OWNER:-}
VISIBILITY=${VISIBILITY:-public}
REMOTE=${REMOTE:-origin}
BRANCH=${BRANCH:-main}
TAG=${TAG:-}
DRY_RUN=0
CREATE=1
REWRITE=1

usage() {
    sed -n 's/^#   //p' "$0"
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --owner)     OWNER=${2:?--owner needs a value}; shift 2 ;;
        --repo)      REPO_NAME=${2:?--repo needs a value}; shift 2 ;;
        --tag)       TAG=${2:?--tag needs a value}; shift 2 ;;
        --remote)    REMOTE=${2:?--remote needs a value}; shift 2 ;;
        --branch)    BRANCH=${2:?--branch needs a value}; shift 2 ;;
        --private)   VISIBILITY=private; shift ;;
        --public)    VISIBILITY=public; shift ;;
        --no-create) CREATE=0; shift ;;
        --no-rewrite) REWRITE=0; shift ;;
        --dry-run)   DRY_RUN=1; shift ;;
        -h|--help)   usage 0 ;;
        *)           echo "publish.sh: unknown option '$1'" >&2; usage 1 ;;
    esac
done

cd "$(dirname "$0")/.."
ROOT=$(pwd)
echo "publish.sh: repository root $ROOT"

[ -n "$OWNER" ] || { echo "publish.sh: --owner (or $OWNER) is required" >&2; exit 1; }

run() {
    if [ "$DRY_RUN" = 1 ]; then
        printf '  [dry-run] %s\n' "$*"
    else
        "$@"
    fi
}

# --- 1. rewrite the OWNER placeholder -------------------------------------
if [ "$REWRITE" = 1 ]; then
    echo "-> rewriting placeholders to $OWNER/$REPO_NAME"
    files="README.md CONTRIBUTING.md SECURITY.md CHANGELOG.md CMakeLists.txt
           .github/ISSUE_TEMPLATE/config.yml .github/ISSUE_TEMPLATE/bug_report.yml
           .github/CODEOWNERS"
    for f in $files; do
        [ -f "$f" ] || continue
        grep -q 'OWNER' "$f" || continue
        if [ "$DRY_RUN" = 1 ]; then
            echo "  [dry-run] patch $f"
            continue
        fi
        tmp="$f.tmp.$$"
        sed -e "s|github.com/OWNER/exifreader|github.com/$OWNER/$REPO_NAME|g" \
            -e "s|@OWNER|@$OWNER|g" "$f" > "$tmp"
        mv "$tmp" "$f"
        echo "   patched $f"
    done
    if grep -Rqs 'github.com/OWNER' . --exclude-dir=.git --exclude-dir=build --exclude-dir=build-cmake; then
        echo "   note: some files still mention github.com/OWNER (see above)"
        grep -Rls 'github.com/OWNER' . --exclude-dir=.git --exclude-dir=build --exclude-dir=build-cmake || true
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
    run git commit -q -m "chore(repo): prepare GitHub repository layout

Add the community files, GitHub Actions workflows, issue and pull request
templates, editor/format configuration and release tooling needed to publish
exifreader as a standalone repository.

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
URL="https://github.com/$OWNER/$REPO_NAME.git"
if [ "$CREATE" = 1 ] && command -v gh >/dev/null 2>&1; then
    if gh repo view "$OWNER/$REPO_NAME" >/dev/null 2>&1; then
        echo "-> $OWNER/$REPO_NAME already exists on GitHub"
        run git remote set-url "$REMOTE" "$URL" 2>/dev/null || run git remote add "$REMOTE" "$URL"
    else
        echo "-> creating $OWNER/$REPO_NAME ($VISIBILITY)"
        run gh repo create "$OWNER/$REPO_NAME" "--$VISIBILITY" \
            --source=. --remote="$REMOTE" --description "Dependency-free C++17 EXIF/TIFF information-tree reader (AI-generated)"
    fi
    run gh repo edit "$OWNER/$REPO_NAME" --add-topic exif --add-topic metadata \
        --add-topic cpp17 --add-topic tiff --add-topic image-forensics \
        --add-topic ai-generated 2>/dev/null || true
    run gh repo edit "$OWNER/$REPO_NAME" --homepage "https://github.com/$OWNER/$REPO_NAME#readme" 2>/dev/null || true
else
    if git remote get-url "$REMOTE" >/dev/null 2>&1; then
        run git remote set-url "$REMOTE" "$URL"
    else
        run git remote add "$REMOTE" "$URL"
    fi
    echo "-> push with: git push -u $REMOTE $BRANCH"
fi

# --- 6. push ---------------------------------------------------------------
run git push -u "$REMOTE" "$BRANCH"
if [ -n "$TAG" ]; then
    run git push "$REMOTE" "$TAG"
    echo "-> the Release workflow will build and attach the binaries for $TAG"
fi

echo "publish.sh: done"
