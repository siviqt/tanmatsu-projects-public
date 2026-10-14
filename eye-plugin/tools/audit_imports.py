#!/usr/bin/env python3
"""Check dynamic imports against only the ELF-declared host libraries."""
import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf', type=pathlib.Path)
parser.add_argument('launcher', type=pathlib.Path)
args = parser.parse_args()
output = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(args.elf)], text=True)
imports = {line.split()[-1] for line in output.splitlines()
           if ' UND ' in line and len(line.split()) >= 8}
source = args.launcher / 'managed_components/badgeteam__badge-elf/src'
dynamic = subprocess.check_output(['readelf', '-d', '--wide', str(args.elf)], text=True)
needed = set(re.findall(r'\(NEEDED\).*?\[([^]]+)\]', dynamic))
libraries = {}
for path in source.glob('kbelf_lib_*.c'):
    text = path.read_text()
    library = re.search(r'\.path\s*=\s*"([^"]+)"', text)
    if library:
        libraries[library.group(1)] = set(re.findall(r'\.name\s*=\s*"([^"]+)"', text))
if not imports or not libraries or not needed:
    raise SystemExit('Missing import, dependency or export data; audit cannot pass.')
unknown = needed - libraries.keys()
exports = set().union(*(libraries.get(name, set()) for name in needed))
missing = imports - exports
print(f'{len(imports)} imports checked against declared libraries: {", ".join(sorted(needed))}')
for symbol in sorted(imports):
    owners = sorted(name for name, symbols in libraries.items() if symbol in symbols)
    print(('MISSING ' if symbol in missing else 'OK ') + symbol + ' [' + ', '.join(owners) + ']')
if unknown:
    raise SystemExit(f'Unknown declared libraries: {sorted(unknown)}')
forbidden = {name for name in imports if any(x in name for x in ('input_', 'nokia', 'catt', 'disp_write', 'theme'))}
if missing or forbidden:
    raise SystemExit(f'Audit failed: missing={sorted(missing)}, forbidden={sorted(forbidden)}')
print('PASS: source export audit only; installed firmware compatibility remains unverified.')
