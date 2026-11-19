import io
import struct
import unittest
from unittest.mock import Mock, patch

from runtime.macos_worker import HEADER, MatteCache, serve


class MatteCacheTests(unittest.TestCase):
    def setUp(self):
        self.rgb = struct.pack("<3f", 0.2, 0.4, 0.8)
        self.rgba = struct.pack("<4f", -0.02, 1.1, 0.13, 0.376)
        self.keyer = Mock(return_value=self.rgba)
        self.cache = MatteCache(self.keyer, max_bytes=32, max_frames=2)
        self.addCleanup(self.cache.close)

    def test_protocol_returns_identical_float_bytes_and_keys_only_once(self):
        request = HEADER.pack(1, 1) + self.rgb
        output = io.BytesIO()
        serve(io.BytesIO(request * 2), output, self.cache)
        expected = HEADER.pack(0, len(self.rgba)) + self.rgba
        self.assertEqual(output.getvalue(), expected * 2)
        self.keyer.assert_called_once_with(1, 1, self.rgb)

    def test_input_shape_is_part_of_the_cache_key(self):
        self.keyer.return_value = self.rgba * 2
        self.cache(1, 2, self.rgb * 2)
        self.cache(2, 1, self.rgb * 2)
        self.assertEqual(self.keyer.call_count, 2)

    def test_rgb_changes_and_least_recently_used_entries_miss(self):
        second = struct.pack("<3f", 0.6, 0.4, 0.8)
        third = struct.pack("<3f", 0.9, 0.4, 0.8)
        for payload in (self.rgb, second, self.rgb, third, self.rgb):
            self.cache(1, 1, payload)
        self.assertEqual(self.keyer.call_count, 3)
        self.cache(1, 1, second)
        self.assertEqual(self.keyer.call_count, 4)

    def test_failed_inference_is_not_cached(self):
        self.keyer.side_effect = [RuntimeError("worker failed"), self.rgba]
        with self.assertRaisesRegex(RuntimeError, "worker failed"):
            self.cache(1, 1, self.rgb)
        self.assertEqual(self.cache(1, 1, self.rgb), self.rgba)
        self.assertEqual(self.cache(1, 1, self.rgb), self.rgba)
        self.assertEqual(self.keyer.call_count, 2)

    def test_cache_storage_failure_keeps_rendering(self):
        with patch("runtime.macos_worker.tempfile.TemporaryFile", side_effect=OSError("disk full")):
            self.assertEqual(self.cache(1, 1, self.rgb), self.rgba)
            self.assertEqual(self.cache(1, 1, self.rgb), self.rgba)
        self.assertEqual(self.keyer.call_count, 2)
