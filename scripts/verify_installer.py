#!/usr/bin/env python3
"""PKGを一時フォルダーへ展開し、インストールせずに配布内容を検証する。"""

import hashlib
from pathlib import Path
import plistlib
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from verify_plugin_metadata import verify_bundle


def require(condition, message):
    if not condition:
        raise SystemExit(f"検証に失敗しました: {message}")


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def main():
    root = Path(__file__).resolve().parent.parent
    source = root / "dist/PICT Import.plugin"
    with (source / "Contents/Info.plist").open("rb") as file:
        info = plistlib.load(file)
    version = info["CFBundleShortVersionString"]
    package = root / f"dist/PICT-Import-{version}-Installer.pkg"
    require(package.is_file(), "インストーラーがありません。")
    with tempfile.TemporaryDirectory(prefix="installer-check-", dir=root / "build") as temporary:
        expanded = Path(temporary) / "expanded"
        subprocess.run(["pkgutil", "--expand-full", str(package), str(expanded)], check=True)
        component = expanded / "PICTImport-component.pkg"
        metadata = ET.parse(component / "PackageInfo").getroot()
        require(metadata.get("identifier") == "jp.pictimport.photoshop.pkg", "パッケージ識別子が異なります。")
        require(metadata.get("version") == version, "バージョンが異なります。")
        require(metadata.get("install-location") == "/Library/Application Support/Adobe/Plug-Ins/CC",
                "共有プラグインフォルダー以外への配置です。")
        require(metadata.get("relocatable") == "false", "配置先が固定されていません。")
        require(metadata.get("auth") == "root", "管理者権限での配置になっていません。")
        require(metadata.find("upgrade-bundle/bundle").get("id") == info["CFBundleIdentifier"],
                "既存プラグインの更新設定がありません。")
        require(metadata.find("strict-identifier/bundle").get("id") == info["CFBundleIdentifier"],
                "更新対象の識別子が固定されていません。")
        require(metadata.find("scripts") is None and not (component / "Scripts").exists(),
                "追加のインストールスクリプトが含まれています。")

        payload = component / "Payload"
        target = payload / "PICT Import.plugin"
        verify_bundle(target)
        require(set(path.name for path in payload.iterdir()) == {"PICT Import.plugin"},
                "プラグイン以外のファイルが含まれています。")
        expected = {path.relative_to(source) for path in source.rglob("*") if path.is_file() and path.name != ".DS_Store"}
        actual = {path.relative_to(target) for path in target.rglob("*") if path.is_file()}
        require(actual == expected, "プラグインのファイル構成が異なります。")
        for relative in expected:
            path = target / relative
            require(not path.is_symlink(), f"リンクが含まれています: {relative}")
            require(digest(path) == digest(source / relative), f"内容が異なります: {relative}")
        binary = target / "Contents/MacOS/PICTImport"
        require(stat.S_IMODE(binary.stat().st_mode) == 0o755, "実行ファイルの権限が不正です。")
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(target)], check=True)
        architectures = subprocess.check_output(["lipo", "-archs", str(binary)], text=True).split()
        require(set(architectures) == {"x86_64", "arm64"}, "Universalバンドルではありません。")
        bom = subprocess.check_output(["lsbom", "-f", str(component / "Bom")], text=True)
        require(all(line.split("\t")[2] == "0/0" for line in bom.splitlines()),
                "インストールファイルの所有者がroot:wheelではありません。")
        print(f"成功: 配置先・所有者・権限・更新設定・{len(expected)}ファイルのSHA-256照合")
        print("成功: 展開したプラグインの署名検証・Intel／Apple Silicon両アーキテクチャ")

        distribution = ET.parse(expanded / "Distribution").getroot()
        options = distribution.find("options")
        require(options.get("require-scripts") == "false" and options.get("allow-external-scripts") == "false",
                "インストーラーの外部スクリプトが有効です。")
        require(distribution.find("script") is None, "予期しないJavaScriptが含まれています。")
        domains = distribution.find("domains")
        require(domains.get("enable_localSystem") == "true" and domains.get("enable_currentUserHome") == "false"
                and domains.get("enable_anywhere") == "false", "インストール対象ボリュームが固定されていません。")
        require(distribution.find("volume-check/allowed-os-versions/os-version").get("min") == "12.0",
                "必要なmacOSバージョンが異なります。")
        require(distribution.find("pkg-ref/must-close/app").get("id") == "com.adobe.Photoshop",
                "Photoshopの終了確認がありません。")
        require(set(options.get("hostArchitectures").split(",")) == {"x86_64", "arm64"},
                "インストーラーの対象アーキテクチャが異なります。")
        for element in ("welcome", "readme", "conclusion"):
            path = expanded / "Resources" / distribution.find(element).get("file")
            require(path.is_file() and "lang=\"ja\"" in path.read_text(encoding="utf-8"),
                    f"日本語の案内がありません: {element}")
        print("成功: macOS条件・Photoshop終了確認・日本語の案内・インストールスクリプトなし")
    print("インストーラーの展開検証が完了しました。システムへのインストールは行っていません。")


if __name__ == "__main__":
    main()
