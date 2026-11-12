#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Reject private assets, local state and obvious credentials in public Git sources."""
import argparse
import pathlib
import re
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
args = parser.parse_args()
repo = args.repo.resolve()

def git(*arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments])

def forbidden(path):
    parts = pathlib.PurePosixPath(path).parts
    if any(part in {'dist', 'build'} or part.startswith('build-') for part in parts):
        return True
    if any(part.startswith('.') and part != '.github' for part in parts[:-1]):
        return True
    return any(re.fullmatch(r'avt[^/]*|mtask2250[^/]*|bios\.inc|const\.inc', part.lower()) for part in parts)

files = git('ls-files', '-z').decode('utf-8').strip('\0').split('\0')
files = [name for name in files if name]
problems = []
for name in files:
    if forbidden(name):
        problems.append(f'Forbidden tracked path: {name}')
    if name.startswith('third_party/'):
        continue
    data = (repo / name).read_bytes()
    if re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----', data):
        problems.append(f'Private-key marker: {name}')
    if re.search(rb'/' + rb'Users/[^/\s]+/|https?://[^/\s]+\.local(?=[:/])', data):
        problems.append(f'Private workstation path/host: {name}')

# Inspect paths in all reachable history, including files deleted from HEAD.
head = subprocess.run(['git', '-C', str(repo), 'rev-parse', '--verify', 'HEAD'], capture_output=True)
if head.returncode == 0:
    for line in git('rev-list', '--objects', '--all').decode('utf-8').splitlines():
        _object_id, separator, name = line.partition(' ')
        if separator and forbidden(name):
            problems.append(f'Forbidden history path: {name}')
if problems:
    print('Public source audit failed:', file=sys.stderr)
    for problem in sorted(set(problems)):
        print(f'  {problem}', file=sys.stderr)
    sys.exit(1)
print(f'Public source audit passed ({len(files)} tracked files and all reachable history paths).')
