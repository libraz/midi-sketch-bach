"""Tests for MIDI loader."""

import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).parent.parent.parent))


class TestMidiLoader(unittest.TestCase):
    """Test MIDI loading with a mocked mido module."""

    def _make_mock_midi(self):
        """Build a mock mido.MidiFile with a simple 2-voice pattern."""
        mock_midi = MagicMock()
        # Simulate messages: ch0 soprano, ch3 pedal
        msgs = [
            MagicMock(type="program_change", channel=0, program=19, time=0),
            MagicMock(type="program_change", channel=3, program=19, time=0),
            # Soprano: C5 at tick 0, dur 480
            MagicMock(type="note_on", channel=0, note=72, velocity=80, time=0),
            MagicMock(type="note_off", channel=0, note=72, velocity=0, time=480),
            # Pedal: C3 at tick 0, dur 960
            MagicMock(type="note_on", channel=3, note=48, velocity=80, time=0),
            MagicMock(type="note_off", channel=3, note=48, velocity=0, time=480),
        ]
        # Only one track in the mock
        mock_track = MagicMock()
        mock_track.__iter__ = lambda self: iter(msgs)
        mock_midi.tracks = [mock_track]
        return mock_midi

    @patch.dict("sys.modules", {"mido": MagicMock()})
    def test_load_midi(self):
        import sys as _sys
        mock_mido = _sys.modules["mido"]
        mock_mido.MidiFile.return_value = self._make_mock_midi()

        from scripts.bach_analyzer.loaders.midi_loader import load_midi
        score = load_midi("/fake/path.mid")

        self.assertEqual(len(score.tracks), 2)
        names = {t.name for t in score.tracks}
        self.assertIn("manual_i", names)
        self.assertIn("pedal", names)

    def test_import_error(self):
        """Ensure ImportError when mido is not available."""
        with patch.dict("sys.modules", {"mido": None}):
            # Need to reimport to trigger the error path
            import importlib
            from scripts.bach_analyzer.loaders import midi_loader
            importlib.reload(midi_loader)
            with self.assertRaises(ImportError):
                midi_loader.load_midi("/fake/path.mid")


if __name__ == "__main__":
    unittest.main()
