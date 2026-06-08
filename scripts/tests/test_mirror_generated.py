"""Round-trip drift guard for the generated ``scripts/bachlib/mirror.py``.

``mirror.py`` is a committed, byte-stable Python mirror of the C++ harness
fixtures, regenerated from the C++ source by :mod:`bachlib.gen_mirror`. This
guard regenerates the file in memory and asserts byte-equality with the
committed copy, so any fixture edit that is not reflected in ``mirror.py``
fails here with a one-command fix (``python3 scripts/bach_tools.py gen-mirror``).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import gen_mirror  # noqa: E402


class MirrorGeneratedTest(unittest.TestCase):
    """The committed mirror.py must equal a fresh regeneration."""

    def test_committed_mirror_is_byte_identical_to_regeneration(self) -> None:
        generated = gen_mirror.render(gen_mirror.extract())
        committed = gen_mirror.MIRROR_PY.read_text(encoding="utf-8")
        self.assertEqual(
            committed,
            generated,
            "scripts/bachlib/mirror.py is stale; regenerate with "
            "`python3 scripts/bach_tools.py gen-mirror`",
        )

    def test_extracted_values_match_imported_mirror(self) -> None:
        """The extracted values must equal the imported mirror constants."""
        import bachlib as rpc  # noqa: PLC0415  (deferred to keep extract path clean)

        values = gen_mirror.extract()
        for name, value in values.items():
            if name == "AFFEKT_CURVE_BIT":
                continue  # generator-internal scaffold, not a mirror constant.
            self.assertTrue(hasattr(rpc, name), f"mirror is missing {name}")
            self.assertEqual(getattr(rpc, name), value, f"{name} drifted from the C++ source")


if __name__ == "__main__":
    unittest.main()
