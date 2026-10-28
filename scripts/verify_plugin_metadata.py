#!/usr/bin/env python3
"""Photoshopが登録情報を受理するためのPiPL依存条件を検査する。"""

import json
from pathlib import Path
import plistlib
import sys


def validate_metadata(info, metadata):
    records = metadata.get("PiPLs", [])
    if len(records) != 1 or records[0].get("Kind") != "Format":
        raise ValueError("PICTのFormat登録が1件必要です。")
    record = records[0]
    # Adobe SDK PIGeneral.h: RequiredHostVersionがある場合はRequiredHostが必須。
    if "RequiredHostVersion" in record and not record.get("RequiredHost"):
        raise ValueError("RequiredHostVersionを指定する場合はRequiredHostが必須です。")
    if record.get("RequiredHost") != "8BIM":
        raise ValueError("Photoshopを示すRequiredHost（8BIM）が必要です。")
    if info.get("CFBundlePackageType") != "8BIF":
        raise ValueError("macOSバンドルの種類がFormatプラグインではありません。")
    version = tuple(int(part) for part in info["CFBundleShortVersionString"].split("."))
    component_version = tuple(record.get(key, 0) for key in (
        "ComponentVersionShortNum", "ComponentVersionMinorRevNum", "ComponentVersionDotRevNum"))
    if version != component_version:
        raise ValueError("Info.plistとPiPLのバージョンが一致しません。")
    if not all(record.get(key) == "PluginMain" for key in ("CodeMacIntel64", "CodeMacARM64")):
        raise ValueError("Intel／Apple Siliconのエントリーポイントが不正です。")


def verify_bundle(bundle):
    with (bundle / "Contents/Info.plist").open("rb") as file:
        info = plistlib.load(file)
    metadata = json.loads((bundle / "Contents/Resources/PiPLs.json").read_text(encoding="utf-8"))
    validate_metadata(info, metadata)


if __name__ == "__main__":
    bundle = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "dist/PICT Import.plugin"
    try:
        verify_bundle(bundle)
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f"プラグイン登録情報の検証に失敗しました: {error}")
    print("成功: PiPLのホスト指定・バージョン・エントリーポイントの整合性")
