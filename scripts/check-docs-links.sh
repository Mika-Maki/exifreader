#!/usr/bin/env sh
# check-docs-links.sh - validate relative links and code fences in the docs.
#
# Checks every Markdown file in the repository: relative link targets must
# exist, and fenced code blocks must be balanced. Absolute URLs are not fetched
# (CI has no business depending on the network of somebody else's wiki).
set -eu

cd "$(dirname "$0")/.."

python3 - <<'PY'
import glob
import os
import re
import sys

files = sorted(p for p in glob.glob('**/*.md', recursive=True) if '.git/' not in p)
link_re = re.compile(r'\]\(([^)]+)\)')
fence_re = re.compile(r'^\s*```')
comment_re = re.compile(r'<!--.*?-->', re.S)

problems = []
checked = 0

for path in files:
    text = open(path, encoding='utf-8').read()

    fences = sum(1 for line in text.splitlines() if fence_re.match(line))
    if fences % 2:
        problems.append(f'{path}: unbalanced code fences ({fences})')

    # Drop fenced blocks and HTML comments before looking for links.
    kept, inside = [], False
    for line in text.splitlines():
        if fence_re.match(line):
            inside = not inside
            continue
        if not inside:
            kept.append(line)
    body = comment_re.sub('', '\n'.join(kept))

    for match in link_re.finditer(body):
        target = match.group(1).strip()
        if target.startswith(('http://', 'https://', 'mailto:', '#')):
            continue
        checked += 1
        relative = target.split('#')[0]
        if not relative:
            continue
        resolved = os.path.normpath(os.path.join(os.path.dirname(path), relative))
        if not os.path.exists(resolved):
            problems.append(f'{path}: broken link -> {target}')

print(f'docs links: checked {len(files)} markdown files, {checked} relative links')
if problems:
    for problem in problems:
        print(f'  FAIL {problem}')
    sys.exit(1)
print('docs links: all relative links resolve, all code fences balanced')
PY
