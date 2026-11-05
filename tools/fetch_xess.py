"""Download the pinned, unmodified Intel XeSS runtime and its license next to totk.exe (Windows).

    python tools/fetch_xess.py --dest build/v143
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile
import urllib.request

COMMIT = '8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0'  # Intel XeSS SDK 3.0.2
BASE = f'https://raw.githubusercontent.com/intel/xess/{COMMIT}'
FILES = (
    ('bin/libxess.dll', 'libxess.dll', '251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7'),
    ('LICENSE.txt', 'licenses/Intel-XeSS-LICENSE.txt', '784be42c39a4a4d03cf85d88e15e59d75828fca773ce45b8dbfffbd7eaf208df'),
)


def install(dest):
    for source, name, expected in FILES:
        target = Path(dest) / name
        if target.is_file():
            if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
                raise ValueError(f'{target}: an existing file differs; move it aside before installing the pinned runtime')
            print(f'{target}: already verified')
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        part = None
        try:
            digest = hashlib.sha256()
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as out:
                part = Path(out.name)
                with urllib.request.urlopen(f'{BASE}/{source}', timeout=60) as response:
                    while block := response.read(1 << 20):
                        digest.update(block)
                        out.write(block)
            if digest.hexdigest() != expected:
                raise ValueError(f'{source}: SHA-256 mismatch; the downloaded file was not installed')
            os.replace(part, target)
            print(f'{target}: installed and verified')
        finally:
            if part is not None:
                part.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dest', type=Path, required=True, help='folder containing totk.exe')
    install(parser.parse_args().dest)
