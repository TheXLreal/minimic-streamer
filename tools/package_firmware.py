"""Validate Intel HEX, merge the two disjoint cores and write a build receipt. No dependencies."""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import zipfile
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent.parent


def read_hex(path):
    data, base, eof = {}, 0, False
    for line in pathlib.Path(path).read_text().splitlines():
        if not line or not line.startswith(':') or eof:
            raise ValueError(f'Invalid record in {path}')
        row = bytes.fromhex(line[1:])
        if len(row) != row[0] + 5 or sum(row) & 255:
            raise ValueError(f'Invalid checksum/length in {path}')
        count, kind = row[0], row[3]
        addr = int.from_bytes(row[1:3], 'big')
        payload = row[4:-1]
        if kind == 0:
            for i, value in enumerate(payload):
                target = base + addr + i
                if target in data:
                    raise ValueError(f'Overlapping data at {target:#x}')
                data[target] = value
        elif kind == 1:
            if count or addr:
                raise ValueError('Invalid EOF')
            eof = True
        elif kind in (2, 4):
            if count != 2 or addr:
                raise ValueError('Invalid extended address')
            base = int.from_bytes(payload, 'big') << (4 if kind == 2 else 16)
        elif kind in (3, 5):
            if count != 4 or addr:
                raise ValueError('Invalid start address')
        else:
            raise ValueError(f'Unsupported HEX type {kind}')
    if not eof or not data:
        raise ValueError('Empty/incomplete HEX')
    return data


def record(kind, addr, payload):
    row = bytes([len(payload)]) + addr.to_bytes(2, 'big') + bytes([kind]) + bytes(payload)
    return ':' + (row + bytes([-sum(row) & 255])).hex().upper()


def write_hex(path, data):
    lines, upper, addresses = [], None, sorted(data)
    i = 0
    while i < len(addresses):
        addr = addresses[i]
        if upper != addr >> 16:
            upper = addr >> 16
            lines.append(record(4, 0, upper.to_bytes(2, 'big')))
        chunk = bytearray([data[addr]])
        i += 1
        while i < len(addresses) and len(chunk) < 16 and addresses[i] == addr + len(chunk) and addresses[i] >> 16 == upper:
            chunk.append(data[addresses[i]])
            i += 1
        lines.append(record(0, addr & 65535, chunk))
    lines.append(record(1, 0, b''))
    pathlib.Path(path).write_text('\n'.join(lines) + '\n', encoding='ascii')


def validate_core(data, flash, flash_size, ram, ram_size):
    if not all(flash <= addr < flash + flash_size for addr in data):
        raise ValueError('Image writes outside its core flash')
    vector = bytes(data[flash + i] for i in range(8))
    sp, pc = struct.unpack('<II', vector)
    if not ram < sp <= ram + ram_size or sp % 8 or not pc & 1 or pc - 1 not in data:
        raise ValueError(f'Invalid boot vector SP={sp:#x}, PC={pc:#x}')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    args = argparse.ArgumentParser()
    args.add_argument('--compiler', required=True)
    compiler = args.parse_args().compiler
    build, out = ROOT / 'build/nrf5340dk', ROOT / 'dist/nrf5340dk'
    out.mkdir(parents=True, exist_ok=True)
    app, net = read_hex(build / 'minimic_app.hex'), read_hex(build / 'minimic_net.hex')
    validate_core(app, 0, 0x100000, 0x20000000, 0x70000)
    validate_core(net, 0x1000000, 0x40000, 0x21000000, 0x10000)
    assert not app.keys() & net.keys(), 'Core images overlap'
    merged = {**app, **net}
    write_hex(out / 'minimic_nrf5340dk.hex', merged)
    assert read_hex(out / 'minimic_nrf5340dk.hex') == merged
    for core in ('app', 'net'):
        for suffix in ('hex', 'elf', 'map'):
            source = build / f'minimic_{core}.{suffix}'
            # CMake executables use the .elf suffix explicitly on Windows.
            shutil.copyfile(source, out / source.name)
    source_paths = [ROOT / p for p in ('CMakeLists.txt', '.gitignore', 'README.md', 'NRF5340DK_RU.md', 'COMPATIBILITY.md', 'CODEC.md', 'LICENSE')]
    for directory in ('nrf5340', 'tools', 'codec', 'tests'):
        source_paths += [p for p in (ROOT / directory).rglob('*') if p.is_file() and '__pycache__' not in str(p)]
    source_paths += [ROOT / 'firmware/usb_descriptors.h']
    shutil.copyfile(ROOT / 'NRF5340DK_RU.md', out / 'START_HERE_RU.md')
    (out / 'tools').mkdir(exist_ok=True)
    for name in ('nrfutil.ps1', 'flash-nrf5340.ps1', 'control-nrf5340.ps1'):
        shutil.copyfile(ROOT / 'tools' / name, out / 'tools' / name)
    (out / 'licenses').mkdir(exist_ok=True)
    for source, name in [('LICENSE', 'MiniMic-MIT.txt'), ('third_party/nrfx/LICENSE', 'nrfx-BSD.txt'),
                         ('third_party/cmsis/LICENSE.txt', 'CMSIS-Apache.txt'), ('third_party/tinyusb/LICENSE', 'TinyUSB-MIT.txt')]:
        shutil.copyfile(ROOT / source, out / 'licenses' / name)
    tests = None
    tests_file = out / 'TESTS.json'
    if tests_file.exists():
        candidate = json.loads(tests_file.read_text())
        if candidate.get('success') and all(sha(out / name) == digest for name, digest in candidate['images'].items()):
            tests = candidate
        else:
            tests_file.unlink()  # Discard evidence for an older pair of images.
    hardware = None
    hardware_file = out / 'HARDWARE.json'
    if hardware_file.exists():
        candidate = json.loads(hardware_file.read_text(encoding='utf-8-sig'))
        if candidate.get('images') and all(sha(out / name) == digest for name, digest in candidate['images'].items()):
            hardware = candidate
        else:
            hardware_file.unlink()  # Hardware results also belong to exact images.
    receipt = {
        'built_utc': datetime.now(timezone.utc).isoformat(), 'board': 'nrf5340dk (PCA10095)',
        'radio': 'nRF5340 network core, proprietary 2 Mbps, +3 dBm default (0/+3 runtime), no external nRF24',
        'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'hardware_tested': False,
        'hardware_validation': hardware,
        'arm_emulation': tests,
        'sources': {str(p.relative_to(ROOT)): sha(p) for p in sorted(source_paths)},
        'dependencies': {name: subprocess.check_output(['git', '-C', str(ROOT / 'third_party' / name), 'rev-parse', 'HEAD'], text=True).strip() for name in ('tinyusb', 'nrfx', 'cmsis')},
        'artifacts': {str(p.relative_to(out)): sha(p) for p in sorted(out.rglob('*'))
                      if p.is_file() and p.name not in ('BUILD.json', 'SHA256SUMS.txt', 'TESTS.json')},
    }
    (out / 'BUILD.json').write_text(json.dumps(receipt, indent=2) + '\n')
    (out / 'SHA256SUMS.txt').write_text(''.join(f'{digest}  {name}\n' for name, digest in receipt['artifacts'].items()))
    bundle = ROOT / 'dist/minimic_nrf5340dk_bundle.zip'
    with zipfile.ZipFile(bundle, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.rglob('*')):
            if path.is_file():
                archive.write(path, str(path.relative_to(out)))
    with zipfile.ZipFile(bundle) as archive:
        assert archive.testzip() is None
    print(f'Validated cpuapp ({len(app)} bytes), cpunet ({len(net)} bytes), merged HEX and boot vectors.')
    print(out / 'minimic_nrf5340dk.hex')
    print(bundle)


if __name__ == '__main__':
    main()
