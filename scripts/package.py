#!/usr/bin/env python3
"""SDKや個人の画像を含めず、プラグインと説明書をZIPへまとめる。"""

from pathlib import Path
import plistlib
import subprocess
import zipfile

from verify_plugin_metadata import verify_bundle


def main():
    root = Path(__file__).resolve().parent.parent
    bundle = root / "dist/PICT Import.plugin"
    if not (bundle / "Contents/MacOS/PICTImport").is_file():
        raise SystemExit("プラグインがありません。先にmake pluginを実行してください。")
    verify_bundle(bundle)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
    with (bundle / "Contents/Info.plist").open("rb") as file:
        version = plistlib.load(file)["CFBundleShortVersionString"]
    output = root / f"dist/PICT-Import-{version}-macOS.zip"
    files = [(p, Path("PICT-Import") / p.relative_to(root / "dist"))
             for p in sorted(bundle.rglob("*")) if p.is_file()]
    files += [(root / "README.md", Path("PICT-Import/README.md")),
              (root / "LICENSE", Path("PICT-Import/LICENSE")),
              (root / "docs/PICT-SPEC.md", Path("PICT-Import/docs/PICT-SPEC.md"))]
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for source, destination in files:
            archive.write(source, destination)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise SystemExit("ZIPの検証に失敗しました。")
    print(f"配布用ZIPを作成・検証しました: {output}")


if __name__ == "__main__":
    main()
