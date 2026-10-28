"""Photoshopが形式登録を無視する定義の再発を検出する。"""

import copy
import importlib.util
import json
from pathlib import Path
import plistlib
import unittest


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("verify_plugin_metadata", ROOT / "scripts/verify_plugin_metadata.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PluginMetadataTests(unittest.TestCase):
    def setUp(self):
        self.info = plistlib.loads((ROOT / "resources/Info.plist").read_bytes())
        self.metadata = json.loads((ROOT / "resources/PiPLs.json").read_text(encoding="utf-8"))

    def test_current_registration_is_valid(self):
        MODULE.validate_metadata(self.info, self.metadata)

    def test_missing_required_host_is_rejected(self):
        # 0.1.0でPhotoshopの形式一覧に登録されなかった組み合わせ。
        broken = copy.deepcopy(self.metadata)
        del broken["PiPLs"][0]["RequiredHost"]
        with self.assertRaisesRegex(ValueError, "RequiredHostが必須"):
            MODULE.validate_metadata(self.info, broken)

    def test_stale_component_version_is_rejected(self):
        stale = copy.deepcopy(self.metadata)
        stale["PiPLs"][0]["ComponentVersionDotRevNum"] -= 1
        with self.assertRaisesRegex(ValueError, "バージョンが一致"):
            MODULE.validate_metadata(self.info, stale)


if __name__ == "__main__":
    unittest.main(verbosity=2)
