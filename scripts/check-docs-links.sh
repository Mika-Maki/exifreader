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

# Documents that tell people where to report something must not point at a
# placeholder address: a published SECURITY.md that says "email
# security@example.com" is worse than no SECURITY.md at all, because the
# reporter believes private contact was made and waits for a reply that never
# comes. Only .invalid is allowed for illustrative examples (RFC 2606).
contact_docs = ['SECURITY.md', 'SECURITY.zh-CN.md', 'CODE_OF_CONDUCT.md',
                'CONTRIBUTING.md', 'CONTRIBUTING.zh-CN.md']
placeholder_re = re.compile(r'example\.(com|org|net)\b')
for path in contact_docs:
    if not os.path.exists(path):
        continue
    with open(path, encoding='utf-8') as handle:
        for number, line in enumerate(handle, 1):
            if placeholder_re.search(line):
                problems.append(
                    f'{path}:{number}: placeholder contact address -> {line.strip()}')

print(f'docs links: checked {len(files)} markdown files, {checked} relative links')
if problems:
    for problem in problems:
        print(f'  FAIL {problem}')
    sys.exit(1)
print('docs links: all relative links resolve, all code fences balanced')
PY
