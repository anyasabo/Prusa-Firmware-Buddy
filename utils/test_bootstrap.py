# Tests of the .tar.zst install path of bootstrap.py (fork only).
# Run: .venv/bin/python -m pytest utils/test_bootstrap.py
import hashlib
import io
import shutil
import stat
import sys
import tarfile
from pathlib import Path

import pytest
import zstandard

sys.path.insert(0, str(Path(__file__).parent))
import bootstrap  # noqa: E402


def tar_bytes(members):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w',
                      format=tarfile.GNU_FORMAT) as tar:
        for name, data in members:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def multi_frame_package(path: Path):
    """A package in two zstd frames, split at a tar member boundary (as pzstd or `cat` make)."""
    first = tar_bytes([('usr/bin/first', b'1' * 100)])
    # Without the end-of-archive blocks: the second frame continues the same tar stream.
    first = first[:1024]
    rest = tar_bytes([('usr/bin/second', b'2' * 100)])
    compressor = zstandard.ZstdCompressor()
    path.write_bytes(compressor.compress(first) + compressor.compress(rest))
    return hashlib.sha256(path.read_bytes()).hexdigest()


@pytest.fixture
def package(tmp_path, monkeypatch):
    source = tmp_path / 'src' / 'pkg.tar.zst'
    source.parent.mkdir()
    sha256 = multi_frame_package(source)
    deps = tmp_path / 'deps'
    deps.mkdir()
    monkeypatch.setattr(bootstrap, 'dependencies_dir', deps)
    monkeypatch.setattr(bootstrap, 'download_url',
                        lambda url, filename: shutil.copy(source, filename))
    archives = [{
        'url': 'https://example.invalid/pkg.tar.zst',
        'sha256': sha256
    }]
    return archives, deps / 'tool'


def force_subprocess(monkeypatch, python=None):
    """Make open_zstd use the subprocess path (no zstd module in this process, no zstd binary)."""
    monkeypatch.setitem(sys.modules, 'zstandard', None)
    monkeypatch.setitem(sys.modules, 'compression.zstd', None)
    monkeypatch.setattr(bootstrap.shutil, 'which', lambda name: None)
    if python:
        monkeypatch.setattr(bootstrap, 'venv_bin_dir', python.parent)


def test_multi_frame_zstandard(package):
    archives, target = package
    bootstrap.install_archives(archives, target, strip_prefix='usr')
    assert (target / 'bin' / 'first').read_bytes() == b'1' * 100
    assert (target / 'bin' / 'second').read_bytes() == b'2' * 100


def test_multi_frame_subprocess(package, monkeypatch):
    archives, target = package
    force_subprocess(monkeypatch, Path(sys.executable))
    bootstrap.install_archives(archives, target, strip_prefix='usr')
    assert (target / 'bin' / 'second').read_bytes() == b'2' * 100


def test_failed_decompressor_stops_the_install(package, tmp_path, monkeypatch):
    archives, target = package
    # A decompressor that writes one complete tar member and then fails, as an OOM kill does.
    fake = tmp_path / 'fakebin' / 'python'
    fake.parent.mkdir()
    partial = tar_bytes([('usr/bin/first', b'1' * 100)])[:1024]
    data = tmp_path / 'partial.tar'
    data.write_bytes(partial)
    fake.write_text(f'#!/bin/sh\ncat {data}\nexit 1\n')
    fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
    force_subprocess(monkeypatch, fake)
    with pytest.raises(RuntimeError):
        bootstrap.install_archives(archives, target, strip_prefix='usr')
    assert not target.exists()
