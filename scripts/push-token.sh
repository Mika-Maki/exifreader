#!/usr/bin/env sh
# push-token.sh - push this repository with a GitHub token, without leaking it.
#
# The token is read from $GIT_TOKEN, or from a file holding GIT_TOKEN=... (by
# default ~/.env). It is never written into .git/config, never embedded in a
# remote URL, never passed on a command line and never printed: git receives it
# through a temporary GIT_ASKPASS helper that is deleted when the script exits.
#
# Usage:
#   scripts/push-token.sh --user Mika-Maki --tag v1.0.0
#   scripts/push-token.sh --check
#
# Options:
#   --user NAME        GitHub login (default: git config user.name)
#   --env-file FILE    file containing GIT_TOKEN=... (default: $GIT_TOKEN, then ~/.env)
#   --remote NAME      git remote (default: origin)
#   --branch NAME      branch to push (default: main)
#   --tag TAG          also push this tag
#   --repo OWNER/NAME  expected repository (default: parsed from the remote)
#   --check            verify token, identity, remote and repository; push nothing
#   --dry-run          show what would be pushed
set -eu

cd "$(dirname "$0")/.."

USER_NAME=""
ENV_FILE=""
REMOTE="origin"
BRANCH="main"
TAG=""
REPO=""
CHECK=0
DRY_RUN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --user)     USER_NAME=${2:?--user needs a value}; shift 2 ;;
        --env-file) ENV_FILE=${2:?--env-file needs a value}; shift 2 ;;
        --remote)   REMOTE=${2:?--remote needs a value}; shift 2 ;;
        --branch)   BRANCH=${2:?--branch needs a value}; shift 2 ;;
        --tag)      TAG=${2:?--tag needs a value}; shift 2 ;;
        --repo)     REPO=${2:?--repo needs a value}; shift 2 ;;
        --check)    CHECK=1; shift ;;
        --dry-run)  DRY_RUN=1; shift ;;
        -h|--help)  sed -n 's/^#   //p' "$0"; exit 0 ;;
        *)          echo "push-token.sh: unknown option '$1'" >&2; exit 1 ;;
    esac
done

token_help() {
    cat >&2 <<'EOF'
push-token.sh: no GitHub token found.

Provide one of:
  * export GIT_TOKEN=<token>        (this shell only)
  * GIT_TOKEN=<token> in ~/.env     (the default lookup)
  * --env-file /path/to/file        (any file containing GIT_TOKEN=...)

The token needs, for the repository being pushed:
  fine-grained:  Contents = Read and write, and Workflows = Read and write
                 (the repository must already exist - a token cannot create one)
  classic:       scopes "repo" and "workflow"
EOF
}

read_token() {
    if [ -n "${GIT_TOKEN:-}" ]; then
        printf '%s' "$GIT_TOKEN"
        return 0
    fi
    for f in "$ENV_FILE" "$HOME/.env" "$HOME/.config/git/github-token"; do
        [ -n "$f" ] || continue
        [ -f "$f" ] || continue
        v=$(grep -m1 'GIT_TOKEN' "$f" 2>/dev/null | cut -d= -f2- | tr -d '"' | tr -d "'" | tr -d '\r' | sed 's/^ *//; s/ *$//')
        if [ -n "$v" ]; then
            printf '%s' "$v"
            return 0
        fi
    done
    return 1
}

TOKEN=$(read_token) || { token_help; exit 1; }
[ -n "$TOKEN" ] || { token_help; exit 1; }

[ -n "$USER_NAME" ] || USER_NAME=$(git config user.name 2>/dev/null || true)
[ -n "$USER_NAME" ] || { echo "push-token.sh: --user is required (git config user.name is unset)" >&2; exit 1; }

if [ -z "$REPO" ]; then
    REPO=$(git remote get-url "$REMOTE" 2>/dev/null | sed -e 's|.*github\.com[:/]||' -e 's|\.git$||' || true)
fi
[ -n "$REPO" ] || { echo "push-token.sh: cannot determine the repository; pass --repo OWNER/NAME" >&2; exit 1; }

api() {
    curl -sS -H "Authorization: Bearer $TOKEN" -H 'Accept: application/vnd.github+json' "$1"
}
json_field() {
    python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
print(d.get(sys.argv[1], "") if isinstance(d, dict) else "")' "$1" 2>/dev/null || true
}

echo "token:      loaded (never printed)"
echo "user:       $USER_NAME"
echo "remote:     $REMOTE -> $(git remote get-url "$REMOTE" 2>/dev/null || echo '(unset)')"
echo "repository: $REPO"

status=$(curl -sS -o /dev/null -w '%{http_code}' -H "Authorization: Bearer $TOKEN" -H 'Accept: application/vnd.github+json' https://api.github.com/user)
if [ "$status" != "200" ]; then
    echo "push-token.sh: GitHub rejected the token (HTTP $status)" >&2
    exit 1
fi
echo "authenticated as: $(api https://api.github.com/user | json_field login)"

repo_status=$(curl -sS -o /dev/null -w '%{http_code}' -H "Authorization: Bearer $TOKEN" -H 'Accept: application/vnd.github+json' "https://api.github.com/repos/$REPO")
case "$repo_status" in
    200) echo "repository state: exists" ;;
    404) echo "repository state: NOT FOUND - create it empty first:" 
         echo "                  https://github.com/new?name=$(basename "$REPO")" ;;
    *)   echo "repository state: HTTP $repo_status" ;;
esac

# Refuse to push if the remote URL carries credentials.
url=$(git remote get-url "$REMOTE" 2>/dev/null || true)
case "$url" in
    https://*@github.com/*)
        echo "push-token.sh: warning: the remote URL embeds credentials; prefer" >&2
        echo "  git remote set-url $REMOTE https://github.com/$REPO.git" >&2 ;;
esac

if [ "$CHECK" = 1 ]; then
    echo
    echo "check only: nothing was pushed"
    [ "$repo_status" = "200" ] || exit 1
    exit 0
fi

# --- push with the token, via a throwaway askpass helper --------------------
askpass=$(mktemp "${TMPDIR:-/tmp}/push-token.XXXXXX")
chmod 700 "$askpass"
trap 'rm -f "$askpass"' EXIT INT TERM
cat > "$askpass" <<'ASKPASS'
#!/bin/sh
case "$1" in
    *[Uu]sername*) printf '%s\n' "$PUSH_TOKEN_USER" ;;
    *)             printf '%s\n' "$PUSH_TOKEN_SECRET" ;;
esac
ASKPASS

do_push() {
    if [ "$DRY_RUN" = 1 ]; then
        echo "  [dry-run] git push $*"
        return 0
    fi
    GIT_ASKPASS="$askpass" GIT_TERMINAL_PROMPT=0 \
    PUSH_TOKEN_USER="$USER_NAME" PUSH_TOKEN_SECRET="$TOKEN" \
        git push "$@"
}

if ! do_push -u "$REMOTE" "$BRANCH"; then
    cat >&2 <<'EOF'

push-token.sh: the push was refused. The usual causes:

  * the repository does not exist yet - a token cannot create one, so create it
    empty at https://github.com/new first (no README, no licence)
  * HTTP 403 mentioning .github/workflows - the token needs
    "Workflows: Read and write" (classic scope: "workflow")
  * HTTP 403 in general - the token needs "Contents: Read and write" on this
    repository, and the repository must be in the token's repository access list

Nothing local was modified, so re-running this script after fixing the token is
all that is needed.
EOF
    exit 1
fi

if [ -n "$TAG" ]; then
    if ! do_push "$REMOTE" "$TAG"; then
        echo "push-token.sh: the branch is pushed but the tag failed" >&2
        exit 1
    fi
    echo "-> pushed $TAG; the Release workflow will build and attach the binaries"
fi

echo "push-token.sh: done"
