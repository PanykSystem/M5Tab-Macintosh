"""Host tests for battery estimation and the guest Brightness module package."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "guest/control-strip-brightness"


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BoardPowerTests(unittest.TestCase):
    def test_estimator_and_backlight(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "A host C++ compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "board-power-tests"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined,address", "-fno-omit-frame-pointer",
                            "-I", str(ROOT / "test/power_stubs"), "-I", str(ROOT / "src/board"),
                            str(ROOT / "test/test_board_power.cpp"), str(ROOT / "src/board/board_power.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class BrightnessModuleTests(unittest.TestCase):
    """The committed build is what users install; check its packaging."""

    def setUp(self):
        self.package = load(MODULE / "package.py", "brightness_package")
        self.binary = MODULE / "dist/Brightness.bin"

    def test_macbinary_header(self):
        raw = self.binary.read_bytes()
        self.assertEqual(raw[1:1 + raw[1] + 1][1:], b"Brightness")
        self.assertEqual(raw[65:73], b"sdevM5Br")
        self.assertEqual((raw[73] << 8) | raw[101], self.package.HAS_BUNDLE)
        self.assertEqual(struct.unpack(">H", raw[124:126])[0], self.package.crc16_xmodem(raw[:124]))

    def test_shared_folder_matches_macbinary(self):
        file_type, creator, data, rsrc = self.package.read_macbinary(self.binary)
        folder = MODULE / "dist/M5Tab Extras"
        self.assertEqual((folder / "Brightness").read_bytes(), data)
        self.assertEqual((folder / ".rsrc/Brightness").read_bytes(), rsrc)
        finf = (folder / ".finf/Brightness").read_bytes()
        self.assertEqual(len(finf), 32)
        self.assertEqual(finf[:8], file_type + creator)
        self.assertEqual(struct.unpack(">H", finf[8:10])[0], self.package.HAS_BUNDLE)

    def test_module_resources(self):
        _, _, _, rsrc = self.package.read_macbinary(self.binary)
        data_offset, map_offset = struct.unpack(">II", rsrc[:8])
        resource_map = rsrc[map_offset:]
        type_list = struct.unpack(">H", resource_map[24:26])[0]
        count = struct.unpack(">H", resource_map[type_list:type_list + 2])[0] + 1
        types = {resource_map[type_list + 2 + 8 * i:type_list + 6 + 8 * i] for i in range(count)}
        for needed in (b"sdev", b"ics#", b"ics8", b"BNDL", b"FREF", b"M5Br", b"vers"):
            self.assertIn(needed, types)

    def test_crc_reference_value(self):
        # CRC-16/XMODEM check value for "123456789".
        self.assertEqual(self.package.crc16_xmodem(b"123456789"), 0x31C3)


if __name__ == "__main__":
    unittest.main()
