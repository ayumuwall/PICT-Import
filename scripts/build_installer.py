#!/usr/bin/env python3
"""作成済みプラグインからmacOS標準の配布用PKGを生成する。"""

from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from verify_plugin_metadata import verify_bundle


INSTALL_DIRECTORY = Path("/Library/Application Support/Adobe/Plug-Ins/CC")
INSTALL_PATH = Path("PICT Import.plugin")
PACKAGE_ID = "jp.pictimport.photoshop.pkg"


def main():
    root = Path(__file__).resolve().parent.parent
    bundle = root / "dist/PICT Import.plugin"
    if not (bundle / "Contents/MacOS/PICTImport").is_file():
        raise SystemExit("プラグインがありません。先にmake pluginを実行してください。")
    verify_bundle(bundle)
    with (bundle / "Contents/Info.plist").open("rb") as file:
        info = plistlib.load(file)
    if info.get("CFBundleIdentifier") != "jp.pictimport.photoshop":
        raise SystemExit("想定と異なるプラグイン識別子です。")
    version = info["CFBundleShortVersionString"]
    if not re.fullmatch(r"\d+(?:\.\d+){0,2}", version):
        raise SystemExit("プラグインのバージョン形式が不正です。")
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)

    build = root / "build"
    build.mkdir(exist_ok=True)
    output = root / f"dist/PICT-Import-{version}-Installer.pkg"
    with tempfile.TemporaryDirectory(prefix="installer-", dir=build) as temporary:
        work = Path(temporary)
        payload = work / "payload"
        staged = payload / INSTALL_PATH
        staged.parent.mkdir(parents=True)
        shutil.copytree(bundle, staged, symlinks=True, ignore=shutil.ignore_patterns(".DS_Store"))
        for path in payload.rglob("*"):
            if path.is_symlink():
                raise SystemExit("パッケージにシンボリックリンクは含められません。")
            executable = path == staged / "Contents/MacOS/PICTImport"
            path.chmod(0o755 if path.is_dir() or executable else 0o644)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(staged)], check=True)

        components = work / "components.plist"
        with components.open("wb") as file:
            plistlib.dump([{
                "RootRelativeBundlePath": INSTALL_PATH.as_posix(),
                "BundleIsRelocatable": False,
                "BundleIsVersionChecked": True,
                "BundleHasStrictIdentifier": True,
                "BundleOverwriteAction": "upgrade",
            }], file)
        subprocess.run([
            "pkgbuild", "--root", str(payload),
            "--component-plist", str(components),
            "--identifier", PACKAGE_ID, "--version", version,
            "--install-location", str(INSTALL_DIRECTORY), "--ownership", "recommended",
            str(work / "PICTImport-component.pkg"),
        ], check=True)

        resources = root / "resources/installer"
        distribution = work / "Distribution.xml"
        text = (resources / "Distribution.xml").read_text(encoding="utf-8").replace("@VERSION@", version)
        ET.fromstring(text)
        distribution.write_text(text, encoding="utf-8")
        # 完成前に既存の配布物を失わないよう、一時ファイルで作成してから置き換える。
        result = work / output.name
        subprocess.run([
            "productbuild", "--distribution", str(distribution),
            "--package-path", str(work), "--resources", str(resources),
            str(result),
        ], check=True)
        result.replace(output)
    print(f"インストーラーを作成しました: {output}")
    print(f"インストール先: {INSTALL_DIRECTORY / INSTALL_PATH}")
    print("このPKGは未署名の開発版です。このコマンドではインストールは行いません。")


if __name__ == "__main__":
    main()
