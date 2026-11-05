"""The Windows artifact gate must reject a client without RT_VERSION."""

import importlib.util
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "ci" / "package-client.py"
SPEC = importlib.util.spec_from_file_location("package_client", SCRIPT)
assert SPEC and SPEC.loader
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def sample_pe(resource_type=16, data_size=4):
    pe = bytearray(0x300)
    pe[:2] = b"MZ"
    struct.pack_into("<I", pe, 0x3c, 0x80)
    pe[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", pe, 0x84, 0x14c, 1)
    struct.pack_into("<H", pe, 0x94, 224)
    struct.pack_into("<H", pe, 0x98, 0x10b)
    struct.pack_into("<I", pe, 0x108, 0x1000)  # resource data directory RVA
    struct.pack_into("<IIII", pe, 0x180, 0x100, 0x1000, 0x100, 0x200)
    struct.pack_into("<H", pe, 0x20e, 1)
    struct.pack_into("<II", pe, 0x210, resource_type, 0x80000020)
    struct.pack_into("<H", pe, 0x22e, 1)
    struct.pack_into("<II", pe, 0x230, 1, 0x80000040)
    struct.pack_into("<H", pe, 0x24e, 1)
    struct.pack_into("<II", pe, 0x250, 0x409, 0x60)
    struct.pack_into("<II", pe, 0x260, 0x1080, data_size)
    pe[0x280:0x284] = b"data"
    return bytes(pe)


class VersionResourceGateTests(unittest.TestCase):
    def test_version_resource_present(self):
        PACKAGE.require_version_resource(sample_pe())

    def test_other_resource_type_does_not_pass(self):
        with self.assertRaisesRegex(ValueError, "VERSIONINFO"):
            PACKAGE.require_version_resource(sample_pe(resource_type=24))

    def test_empty_version_resource_does_not_pass(self):
        with self.assertRaisesRegex(ValueError, "VERSIONINFO"):
            PACKAGE.require_version_resource(sample_pe(data_size=0))


class ReleaseTitleGateTests(unittest.TestCase):
    def test_plain_title_passes(self):
        PACKAGE.require_release_title(b"\0Crash Team Racing - CTR-AP v0.3.0-alpha2\0")

    def test_internal_title_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Internal"):
            PACKAGE.require_release_title(b"\0%s | Internal\0")


if __name__ == "__main__":
    unittest.main()
