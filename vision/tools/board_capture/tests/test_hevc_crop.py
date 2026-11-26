import unittest

from hevc_crop import with_visible_width


# SPS from a synthetic libx265 stream with a 1472x1080 visible area and
# 1088 coded rows. The reference uses FFmpeg hevc_metadata=crop_right=32.
PADDED_SPS = bytes.fromhex(
    "420101016000000300900000030000030078a002e0801107cb965654a4c2f0168080000003008000000f04"
)
CROPPED_SPS = bytes.fromhex(
    "420101016000000300900000030000030078a002e080110708cb965654a4c2f0168080000003008000000f04"
)
START_3 = b"\x00\x00\x01"
START_4 = b"\x00\x00\x00\x01"


class HevcCropTest(unittest.TestCase):
    def test_rewrite_repeated_sps_and_preserve_other_nals(self):
        vps = START_4 + b"\x40\x01\xff"
        frame = START_3 + b"\x26\x01\xaa\x00\x00\x03\x00\x80"
        packet = vps + START_3 + PADDED_SPS + frame + START_4 + PADDED_SPS + b"\x00"
        expected = vps + START_3 + CROPPED_SPS + frame + START_4 + CROPPED_SPS + b"\x00"
        self.assertEqual(with_visible_width(packet, 1472, 1440, 1080), (expected, True))

    def test_pass_through_without_sps(self):
        packet = START_3 + b"\x02\x01\x80"
        self.assertEqual(with_visible_width(packet, 1472, 1440, 1080), (packet, False))

    def test_reject_unexpected_geometry(self):
        with self.assertRaisesRegex(ValueError, "Unexpected HEVC SPS"):
            with_visible_width(START_4 + PADDED_SPS, 1536, 1440, 1080)

    def test_reject_truncated_sps(self):
        with self.assertRaisesRegex(ValueError, "Truncated HEVC SPS"):
            with_visible_width(START_4 + PADDED_SPS[:8], 1472, 1440, 1080)
