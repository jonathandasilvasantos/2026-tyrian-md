"""Public builds fetch data locally and never require a bundled title bitmap."""
import importlib.util
from pathlib import Path
import sys
import zipfile

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_download_extraction_is_flat_and_excludes_executables(tmp_path):
    bootstrap = load_tool("bootstrap")
    archive = tmp_path / "sample.zip"
    with zipfile.ZipFile(archive, "w") as zipped:
        zipped.writestr("tyrian21/PALETTE.DAT", b"palette")
        zipped.writestr("tyrian21/TYRIAN.HDT", b"items")
        zipped.writestr("tyrian21/TYRIAN.EXE", b"not game data")
        zipped.writestr("../../LICENSE.DOC", b"original terms")
    target = tmp_path / "data"
    bootstrap.extract_data(archive, target)
    assert sorted(path.name for path in target.iterdir()) == ["license.doc", "palette.dat", "tyrian.hdt"]
    assert not (tmp_path.parent / "LICENSE.DOC").exists()


def test_invalid_archive_is_rejected(tmp_path):
    bootstrap = load_tool("bootstrap")
    archive = tmp_path / "bad.zip"
    with zipfile.ZipFile(archive, "w") as zipped:
        zipped.writestr("game.exe", b"not a Tyrian data archive")
    with pytest.raises(RuntimeError, match="palette"):
        bootstrap.extract_data(archive, tmp_path / "data")


def test_bad_download_hash_never_extracts(tmp_path):
    bootstrap = load_tool("bootstrap")
    cache = tmp_path / "out/downloads"
    cache.mkdir(parents=True)
    (cache / "tyrian21.zip").write_bytes(b"corrupt")
    with pytest.raises(RuntimeError, match="SHA-256"):
        bootstrap.game_data(tmp_path)
    assert not (tmp_path / "tyrian-data").exists()


def test_title_uses_downloaded_data_deterministically():
    from gen import title_image
    from tyrdata import data_path
    if not Path(data_path("tyrian.pic")).exists():
        pytest.skip("local Tyrian data not installed")
    first, second = title_image(), title_image()
    assert first.size == (320, 224)
    assert first.tobytes() == second.tobytes()
    assert np.array(first).max() > 0


def test_public_path_policy():
    policy = load_tool("audit_public")
    for path in ("sgdk/src/math.c", "open-tyrian", "res/art/title_md.png", "release/tyrian-md.bin", "tyrian-data/music.mus"):
        assert policy.blocked(path)
    for path in ("src/game/player.c", "docs/screenshots/01-title.png", "release/README.md", "LICENSES/SGDK-MIT.txt"):
        assert not policy.blocked(path)
