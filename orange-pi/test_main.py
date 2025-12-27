import sys
import tempfile
import textwrap
from pathlib import Path


THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from main import set_crop_in_config


def _run_case(crop: str, *, ensure_exists: bool, initial: str):
    with tempfile.TemporaryDirectory() as temp_dir:
        config_path = Path(temp_dir) / "majestic.yaml"
        config_path.write_text(initial, encoding="utf-8")

        set_crop_in_config(crop, ensure_exists=ensure_exists, config_path=str(config_path))

        return config_path.read_text(encoding="utf-8")


def test_updates_existing_crop_line():
    initial = textwrap.dedent(
        """\
        video0:
          enabled: true
          crop: 0x0x3840x2160
        video1:
          enabled: false
        """
    )
    expected = textwrap.dedent(
        """\
        video0:
          enabled: true
          crop: 320x180x3520x1980
        video1:
          enabled: false
        """
    )
    contents = _run_case(
        "320x180x3520x1980",
        ensure_exists=False,
        initial=initial,
    )
    assert contents == expected


def test_inserts_crop_when_missing_and_ensured():
    initial = textwrap.dedent(
        """\
        video0:
          enabled: true
        video1:
          enabled: false
        """
    )
    expected = textwrap.dedent(
        """\
        video0:
          crop: 640x360x3200x1800
          enabled: true
        video1:
          enabled: false
        """
    )
    contents = _run_case(
        "640x360x3200x1800",
        ensure_exists=True,
        initial=initial,
    )
    assert contents == expected


def test_leaves_file_when_crop_missing_and_not_ensured():
    initial = textwrap.dedent(
        """\
        video0:
          enabled: true
        video1:
          enabled: false
        """
    )
    contents = _run_case(
        "640x360x3200x1800",
        ensure_exists=False,
        initial=initial,
    )
    assert contents == initial


def run_tests():
    test_updates_existing_crop_line()
    test_inserts_crop_when_missing_and_ensured()
    test_leaves_file_when_crop_missing_and_not_ensured()
    print("All tests passed.")


if __name__ == "__main__":
    run_tests()
