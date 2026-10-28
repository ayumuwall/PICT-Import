# PICT Import — Photoshop用ネイティブプラグイン

Macintosh PICTをPhotoshopの「ファイル → 開く」から読み込む、macOS用の読み込み専用プラグインです。画像を8ビットRGBに描画し、Photoshopで編集・PNG保存できるようにします。

公開リポジトリ：[ayumuwall/PICT-Import](https://github.com/ayumuwall/PICT-Import) · ライセンス：[MIT](LICENSE)

## ダウンロード

配布済みのバイナリーは[GitHub Releases](https://github.com/ayumuwall/PICT-Import/releases)で確認できます。配布されていない版は、以下の開発手順でインストーラー（`.pkg`）と手動導入用ZIPを作成してください。現在の0.1.2は開発版です。対応環境・制限・検証状況を確認してから利用してください。

## 成果物と検証状況

- `dist/PICT Import.plugin`：Intel／Apple SiliconのUniversalバンドル。
- `dist/PICT-Import-0.1.2-Installer.pkg`：ダブルクリックで実行するmacOS標準インストーラー。
- `dist/PICT-Import-0.1.2-macOS.zip`：プラグインと導入・仕様説明をまとめた配布用ファイル。
- [PICTの仕様調査](docs/PICT-SPEC.md)：構造、圧縮方式、実装範囲、出典。
- 対象：macOS 12以降、Photoshop 2025／26.0以降。Adobe公式SDKのPiPLs.json方式を利用。
- デコーダーの単体テスト、AddressSanitizer／UndefinedBehaviorSanitizer、実際のMach-Oバンドルをロードするホスト接続テストが通過。
- 手元の4つのPICTファイル（640×480）は、FFmpegのQuickDrawデコーダーが出力したRGBAと全画素一致。元ファイルは変更せず、配布物にも含めていません。
- インストーラーは展開して、配置先・権限・更新設定・同梱プラグインとの一致・署名を検証します。
- 0.1.1ではホスト指定を修正し、Photoshopへの登録を確認しました。0.1.2では、準備段階の未設定記述子を参照せず、SDKのファイルURLからプラグイン自身が読み取り専用で開くよう変更しています。
- 指定されたIMAGE029.pctを新しいファイルURL方式で読み、模擬ホストへ渡した480行のRGBAがFFmpegの結果と一致することを検証しました。
- **0.1.2のPhotoshop本体での読み込みは未確認です。** Apple Silicon上の実行も未確認です。

## インストールと利用

1. Photoshopで作業中のファイルを保存し、Photoshopを終了します。
2. `PICT-Import-0.1.2-Installer.pkg`をダブルクリックし、案内に従ってインストールします。管理者の認証が必要です。旧版が入っている場合も同じ手順で更新します。
3. Photoshopを起動します。Mac全体の再起動は不要です。
4. 「Photoshop → プラグインについて」に「Macintosh PICT」が表示されることを確認します。
5. 「ファイル → 開く」で`.pict`・`.pct`・`.pic`を選びます。必要なら形式を「Macintosh PICT」に指定します。
6. PNGが必要な場合は、開いた画像をPhotoshopの「コピーを保存」または書き出し機能でPNGに保存します。

配置先は[Adobeが案内する共有フォルダー](https://helpx.adobe.com/photoshop/desktop/troubleshoot/tool-feature-issues/photoshop-plug-in-folder-locations.html)です。対応するPhotoshopのバージョンとMac上のすべてのユーザーで利用できます。この場所の既存プラグインは、識別子を確認して更新します。配置先の自動移動は無効にし、既に新しいバージョンがある場合はダウングレードしない設定です。

```text
/Library/Application Support/Adobe/Plug-Ins/CC/PICT Import.plugin
```

インストーラーは、標準機能でPhotoshopの終了を確認してからファイルを配置します。追加のインストールスクリプトは含まず、Photoshopの環境設定も変更しません。Photoshop未導入のMacにも配置できるため、利用時には対応するPhotoshopを用意してください。

ZIPで手動導入する場合も同じ共有フォルダーへ配置できます。バージョン別の`/Applications/Adobe Photoshop 2026/Plug-ins/`に手動配置したコピーがある場合は、二重配置にならないよう先に取り除いてください。

削除する場合は、Photoshop終了後に共有フォルダーの`PICT Import.plugin`を取り除いてください。

プラグインにはローカル開発用のアドホック署名を付けています。PKGは未署名で、Developer ID署名・公証済みの一般配布版ではありません。macOSが実行を拒否する場合は、セキュリティ設定を一括で無効化せず、必要な署名・公証を行ってください。

## 対応するPICT

| 項目 | 対応範囲 |
| --- | --- |
| 形式 | PICT 1、PICT 2、拡張PICT 2 |
| コンテナー | 512バイトのファイルヘッダーあり、ヘッダーなしの生PICTデータ |
| ビットマップ | 1ビット白黒、1／2／4／8ビットのパレット、16ビットRGB555、32ビットRGB |
| 圧縮 | 非圧縮、PackBits、16ビット単位RLE、RGB成分ごとのRLE、32ビットのパッド除去 |
| 転送 | BitsRect／BitsRgn、PackBitsRect／PackBitsRgn、DirectBitsRect／DirectBitsRgn |
| 図形 | 線、矩形、角丸矩形、楕円、円弧、ポリゴン、領域の塗りつぶし、same系 |
| クリップ | 矩形・走査線形式のリージョン |
| その他 | 8×8の白黒パターン、RGB色指定、基本論理転送モード、反転、拡張ヘッダーの解像度 |
| 文字 | MacRoman、太字・斜体、フォント名指定とシステムフォントへの代替 |

## 制限

- QuickTime埋め込み画像（JPEGを含む）、PostScript描画コメント、PixPat、算術転送モード、原点の移動、複雑なリージョンの輪郭描画、一部の文字装飾・文字間隔・文字変形は未対応で、エラーとして扱います。
- リソースフォーク全体、AppleDouble、MacBinary、StuffItは展開しません。**リソースから取り出したPICTそのもの**は読み込めます。
- ベクターと文字は1枚のRGB画像になります。文字は現行のCoreTextフォントで描くため、当時のQuickDrawと字形・字幅が異なる場合があります。日本語などMacRoman以外の文字コードは対象外です。
- 色付きソースに対する論理転送やペンの輪郭は、当時のOS・表示デバイスと完全に同じ結果になる保証はありません。ICCプロファイルの復元、レイヤー復元、透明度の復元、PICT書き出しは行いません。背景は白です。
- 上限は256 MiB／ファイル、32×1024×1024画素、各辺32767画素です。描画命令数・展開量・リージョンの複雑さにも上限を設けています。

## 開発

必要なものはXcode Command Line ToolsまたはXcodeと、Python 3.9以降です。HomebrewライブラリやJavaは不要です。

```sh
git clone https://github.com/ayumuwall/PICT-Import.git
cd PICT-Import
python3 scripts/fetch_sdk.py
make test
make plugin
make test-installer
python3 scripts/package.py
```

SDK取得時だけネット接続が必要です。Adobe公式リポジトリのコミットを固定し、各ファイルのGit blobハッシュを確認して`.vendor/photoshop-sdk`に保存します。別のSDKを使う場合は`make SDK=/path/to/photoshop-sdk plugin`と指定できます。SDK本体は配布物に含めません。利用条件は[Adobe SDKライセンス](https://github.com/AdobeDocs/photoshop-cpp-sdk/blob/7e9aca4353a3d4615e9522f47521bf840fb4388c/LICENSE.md)を参照してください。

テストは合成したPICTを`build/fixtures`に生成します。外部のPICTを検証する場合は次の環境変数を指定できます。

```sh
PICT_TEST_SAMPLE_DIR=/path/to/samples make test
```

参照デコーダーの結果を比較するときは、ファイル名の拡張子を`.rgba`に変えた8ビットRGBAデータを用意し、`PICT_TEST_REFERENCE_DIR`も指定します。

0.1.2以降は、Photoshopからの呼び出し番号・返却値・エラー内容をmacOSの統合ログへ記録します。ファイルの画像データはログに含めません。読み込み後に次のコマンドで該当プラグインの診断を確認できます。

```sh
/usr/bin/log show --last 10m --style compact --predicate 'subsystem == "jp.pictimport.photoshop"'
```

## 構成

```text
src/pict.cpp             PICTの解析、PackBits展開、描画
src/plugin.cpp           PhotoshopのFormatRecord／POSIX I/O接続
resources/PiPLs.json     形式登録、拡張子、読み込み専用の宣言
resources/Info.plist     macOSバンドル情報
tests/pict_tests.cpp     デコーダーと変異入力のテスト
tests/plugin_tests.cpp   生成したバンドルをロードするホスト接続テスト
scripts/sdk.lock.json    Adobe SDKの固定版とハッシュ
scripts/build_installer.py   macOSインストーラーの作成
scripts/verify_installer.py  システムへ導入しないパッケージ検証
scripts/verify_plugin_metadata.py  PiPLの必須項目と依存条件の検査
resources/installer/     インストーラーの日本語画面・配置設定
docs/PICT-SPEC.md        仕様調査と実装方針
LICENSE                 MITライセンス
```

## ライセンス

本プロジェクトのソースコードと文書は[MITライセンス](LICENSE)で公開しています。ライセンス文はプラグインバンドル・インストーラー・配布用ZIPにも同梱します。

ビルド時に利用するAdobe Photoshop C++ SDKはAdobe独自のライセンスが適用され、本プロジェクトのMITライセンスには含まれません。SDK本体・ヘッダーや検証に使用した個人の画像は、公開リポジトリと配布物に含めません。

AdobeおよびPhotoshopはAdobeの商標です。本プロジェクトはAdobe公式製品ではありません。
