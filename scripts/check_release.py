#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build a local binary archive and verify its extracted, relocated contents."""
import argparse
import pathlib
import platform
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', default='build')
parser.add_argument('--output', default='dist')
parser.add_argument('--config', default='Release')
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[1]
build = pathlib.Path(args.build).resolve()
output = pathlib.Path(args.output).resolve()
subprocess.run(['cpack', '--config', str(build / 'CPackConfig.cmake'),
                '-C', args.config, '-B', str(output)], check=True)
archives = list(output.glob('emu51-*.tar.gz')) + list(output.glob('emu51-*.zip'))
if len(archives) != 1:
    raise SystemExit(f'Expected one archive in {output}, found {archives}; use an empty output directory.')
work = build / 'archive-check'
subprocess.run(['cmake', f'-DARCHIVE={archives[0]}', f'-DWORK_DIR={work}',
                '-P', str(repo / 'cmake/VerifyPackage.cmake')], check=True)
manifest, = (work / 'extract').rglob('SHA256SUMS')
root = manifest.parent
if platform.system() == 'Linux':
    for executable in (root / 'bin').glob('emu51*'):
        result = subprocess.run(['ldd', str(executable)], text=True, capture_output=True)
        if 'not found' in result.stdout or (result.returncode and 'not a dynamic executable' not in result.stderr):
            raise SystemExit(f'Unresolved runtime dependencies for {executable}:\n{result.stdout}\n{result.stderr}')
if platform.system() == 'Darwin':
    for signed in [root / 'emu51_imgui.app', *((root / 'bin') / name for name in ('emu51', 'emu51_asm', 'emu51_bench'))]:
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(signed)], check=True)
print(f'Archive verified: {archives[0]}')
