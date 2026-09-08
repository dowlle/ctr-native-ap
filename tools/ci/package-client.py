#!/usr/bin/env python3
"""Verify native artifacts without executing them; package exact build bytes."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile
import zlib


def output(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(platform, exe):
    debug = Path(str(exe) + '.debug')
    if not exe.is_file() or not debug.is_file():
        raise ValueError('Missing executable or exact debug sidecar')
    sections = output('objdump', '-h', str(exe))
    if re.search(r'\s\.debug_\S+', sections):
        raise ValueError('Shipped executable still contains debug sections')
    if '.debug_info' not in output('objdump', '-h', str(debug)):
        raise ValueError('Sidecar has no DWARF debug information')
    with tempfile.TemporaryDirectory() as tmp:
        section = Path(tmp) / 'debuglink'
        # A scratch output prevents objcopy from modifying the gated binary.
        subprocess.run(['objcopy', '--dump-section', f'.gnu_debuglink={section}',
                        str(exe), str(Path(tmp) / 'copy')], check=True)
        data = section.read_bytes()
    end = data.index(b'\0')
    crc_at = (end + 1 + 3) & ~3
    if data[:end].decode() != debug.name or len(data) != crc_at + 4:
        raise ValueError('Debuglink name/size mismatch')
    if struct.unpack_from('<I', data, crc_at)[0] != zlib.crc32(debug.read_bytes()):
        raise ValueError('Debuglink CRC does not match the exact sidecar')
    result = {}
    if platform == 'windows':
        pe = exe.read_bytes()
        if pe[:2] != b'MZ':
            raise ValueError('Not a Windows executable')
        offset = struct.unpack_from('<I', pe, 0x3c)[0]
        if pe[offset:offset + 4] != b'PE\0\0' or struct.unpack_from('<H', pe, offset + 4)[0] != 0x14c:
            raise ValueError('Expected PE32 i386 client')
        if struct.unpack_from('<H', pe, offset + 24)[0] != 0x10b:
            raise ValueError('Expected PE32 optional header')
        flags = struct.unpack_from('<H', pe, offset + 24 + 70)[0]
        if flags & 0x140 != 0x140:
            raise ValueError('Windows ASLR/NX flags missing')
        imports = re.findall(r'DLL Name:\s*(\S+)', output('objdump', '-p', str(exe)))
        system = {'advapi32.dll', 'bcrypt.dll', 'cfgmgr32.dll', 'crypt32.dll',
                  'dinput8.dll', 'dwmapi.dll', 'gdi32.dll', 'hid.dll', 'imm32.dll',
                  'kernel32.dll', 'msvcrt.dll', 'ole32.dll', 'oleaut32.dll',
                  'setupapi.dll', 'shell32.dll', 'shlwapi.dll', 'user32.dll',
                  'uuid.dll', 'version.dll', 'winhttp.dll', 'winmm.dll', 'ws2_32.dll'}
        if not imports or any(name.lower() not in system for name in imports):
            raise ValueError(f'Unexpected runtime DLL dependency: {imports}')
        result['imported_dlls'] = imports
        result['aslr_nx'] = True
    else:
        elf = exe.read_bytes()[:20]
        if elf[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', elf, 18)[0] != 3:
            raise ValueError('Expected little-endian ELF32 i386 client')
        versions = re.findall(r'\bGLIBC_(\d+(?:\.\d+)+)', output('readelf', '-V', str(exe)))
        highest = max((tuple(map(int, v.split('.'))) for v in versions), default=(0,))
        if highest > (2, 38):
            raise ValueError(f'glibc requirement exceeds 2.38: {highest}')
        dynamic = output('readelf', '-d', str(exe))
        if '(RPATH)' in dynamic or '(RUNPATH)' in dynamic:
            raise ValueError('Unexpected embedded runtime search path')
        stack = next((s for s in output('readelf', '-W', '-l', str(exe)).splitlines() if 'GNU_STACK' in s), '')
        if not stack or re.search(r'\bRWE\b', stack):
            raise ValueError('Missing or executable GNU_STACK')
        result['maximum_glibc'] = '.'.join(map(str, highest))
        result['needed_libraries'] = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
    return result


def main():
    platform, variant, executable, destination = sys.argv[1:]
    if platform not in ('windows', 'linux') or variant not in ('ap', 'vanilla'):
        raise ValueError('Invalid platform/configuration')
    exe, out = Path(executable), Path(destination)
    checks = verify(platform, exe)
    commit = output('git', 'rev-parse', 'HEAD').strip()
    if output('git', 'diff', '--name-only', 'HEAD').strip():
        raise ValueError('Tracked source differs from the recorded commit')
    name = f'ctr-{variant}-{platform}-x86-{commit[:12]}'
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary) / name
        root.mkdir()
        shutil.copy2(exe, root / exe.name)
        for file in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'SETUP.md', 'ap-config.example.txt'):
            shutil.copy2(file, root / file)
        shutil.copy2('tools/extract-assets/extract_assets.py', root / 'extract_assets.py')
        for file in (('support-bundle.bat', 'support-bundle.ps1') if platform == 'windows' else ('support-bundle.sh',)):
            shutil.copy2(file, root / file)
        (root / 'versions.txt').write_text(output('bash', 'tools/release-versions.sh'))
        evidence = {'source_commit': commit, 'platform': platform, 'variant': variant,
                    'custom_tracks': variant == 'ap', 'authoring': False,
                    'executable_sha256': digest(exe), 'debug_sha256': digest(Path(str(exe) + '.debug')),
                    'vendor_lock_sha256': digest(Path('ap/vendor/versions.lock')),
                    'checks': checks, 'compiler': output('gcc', '--version').splitlines()[0],
                    'cmake': output('cmake', '--version').splitlines()[0],
                    'run_id': os.environ.get('GITHUB_RUN_ID'),
                    'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
                    'workflow_sha': os.environ.get('GITHUB_WORKFLOW_SHA')}
        evidence['installed_packages'] = output('pacman', '-Q') if platform == 'windows' else output('dpkg-query', '-W')
        (root / 'BUILD.json').write_text(json.dumps(evidence, indent=2) + '\n')
        (root / 'BUILD-NOTICE.txt').write_text(
            'CI build artifact, not a complete tested release. No game assets included.\n'
            'Use the matching ctr.apworld from the reviewed release pair.\n'
            'No gameplay acceptance or antivirus clearance is implied.\n')
        archive = out / (name + ('.zip' if platform == 'windows' else '.tar.gz'))
        if platform == 'windows':
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
                for file in sorted(root.iterdir()):
                    bundle.write(file, f'{name}/{file.name}')
        else:
            with tarfile.open(archive, 'w:gz') as bundle:
                bundle.add(root, arcname=name)
    sidecar = out / f'{exe.name}.debug'
    shutil.copy2(str(exe) + '.debug', sidecar)
    for file in (archive, sidecar):
        Path(str(file) + '.sha256').write_text(f'{digest(file)}  {file.name}\n')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
