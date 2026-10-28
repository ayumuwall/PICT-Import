#!/usr/bin/env python3
"""Adobe公式リポジトリから固定版のビルド用ヘッダーを取得する。"""

import hashlib
import json
from pathlib import Path
import sys
from urllib.error import URLError
from urllib.request import Request, urlopen


def blob_hash(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def main():
    root = Path(__file__).resolve().parent.parent
    lock = json.loads((root / "scripts/sdk.lock.json").read_text(encoding="utf-8"))
    destination = root / ".vendor/photoshop-sdk"
    print("Adobe公式Photoshop C++ SDKのヘッダーを取得します。")
    print(f"利用条件: https://github.com/{lock['repository']}/blob/{lock['commit']}/LICENSE.md")
    print("SDKはローカルのビルド用途に保存し、プラグイン配布物には同梱しません。")
    for item in lock["files"]:
        output = destination / item["path"]
        if output.is_file() and blob_hash(output.read_bytes()) == item["sha"]:
            continue
        url = f"https://raw.githubusercontent.com/{lock['repository']}/{lock['commit']}/{item['path']}"
        request = Request(url, headers={"User-Agent": "PICT-Import-SDK-bootstrap/0.1"})
        with urlopen(request, timeout=30) as response:
            data = response.read(2 * 1024 * 1024)
        if blob_hash(data) != item["sha"]:
            raise ValueError(f"SDKファイルのハッシュが一致しません: {item['path']}")
        output.parent.mkdir(parents=True, exist_ok=True)
        temporary = output.with_suffix(output.suffix + ".download")
        temporary.write_bytes(data)
        temporary.replace(output)
        print(f"取得: {item['path']}")
    print(f"SDKの{len(lock['files'])}ファイルを検証しました: {destination}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, URLError, ValueError) as error:
        print(f"SDKの準備に失敗しました: {error}", file=sys.stderr)
        sys.exit(1)
