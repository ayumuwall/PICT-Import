# PICT Importの開発

インストール・利用方法は[README](../README.md)を参照してください。この文書はソースからのビルドや、不具合調査を行う方向けです。

## 必要なもの

- macOS上のXcode Command Line ToolsまたはXcode
- Python 3.9以降
- SDK取得時のインターネット接続

## ビルドと配布物の作成

```sh
git clone https://github.com/ayumuwall/PICT-Import.git
cd PICT-Import
python3 scripts/fetch_sdk.py
make test
make test-installer
python3 scripts/package.py
```

`make test`はプラグインをビルドしてテストし、`make test-installer`はインストーラーを作成・展開検証します。システムへのインストールは行いません。プラグインだけをビルドする場合は`make plugin`を実行してください。

| 出力 | 内容 |
| --- | --- |
| `dist/PICT Import.plugin` | Intel／Apple SiliconのUniversalバンドル |
| `dist/PICT-Import-<バージョン>-Installer.pkg` | macOS標準インストーラー |
| `dist/PICT-Import-<バージョン>-macOS.zip` | プラグイン、README、仕様説明、MITライセンス |

プラグインはアドホック署名、PKGは未署名です。Developer ID署名・公証は行っていません。

### Adobe SDK

`scripts/fetch_sdk.py`は、Adobe公式リポジトリの固定コミットからヘッダーを取得し、各ファイルのGit blobハッシュを確認して`.vendor/photoshop-sdk`に保存します。別のSDKを使う場合は`make SDK=/path/to/photoshop-sdk plugin`と指定できます。

SDKには[Adobe SDKライセンス](https://github.com/AdobeDocs/photoshop-cpp-sdk/blob/7e9aca4353a3d4615e9522f47521bf840fb4388c/LICENSE.md)が適用されます。本プロジェクトのMITライセンスには含まれません。SDK本体・ヘッダーは、公開リポジトリや配布物に同梱しません。

## テスト

`make test`では、次の内容を検証します。

- デコーダー：PICT 1／2、主要ピクセル形式、圧縮、図形、文字、クリップ、QuickTime JPEGの分割・配置・代替表示、破損・切断データ、2,000件の変異入力。AddressSanitizer／UndefinedBehaviorSanitizerを使用します。
- 登録情報：PiPLのホスト指定・バージョン・エントリーポイントの整合性。
- ホスト接続：実際のMach-Oバンドルをロードし、PICT識別・画像受け渡し・進捗・キャンセル・エラー処理、日本語パスのファイルURL、QuickTime JPEGの受け渡しを検証します。

`make test-installer`では、配置先・所有者・権限・更新設定・同梱ファイルのSHA-256照合・バンドル署名・Intel／Apple Silicon両アーキテクチャを検証します。

合成PICTは`build/fixtures`に生成します。外部のPICTを検証する場合は、次の環境変数を指定できます。

```sh
PICT_TEST_SAMPLE_DIR=/path/to/samples make test
```

参照デコーダーの結果と比較する場合は、ファイル名の拡張子を`.rgba`に変えた8ビットRGBAデータを用意し、`PICT_TEST_REFERENCE_DIR`も指定します。デコーダーと、ホストへ転送した全行の両方を照合します。検証に使った個人の画像は、公開リポジトリ・配布物に含めません。

### 実機での確認

0.1.2では、Photoshop本体でPICTを読み込めたことを利用者が確認しています。Intel／Apple Siliconの双方で実機検証を完了したという記録はありません。

0.1.3では、QuickTime JPEGを3分割で格納した実ファイル`IMAGE005.pct`について、640×480画素のデコードと模擬ホストへの480行の転送を検証しました。各JPEGを独立に展開して連結した参照RGBAと全画素が一致しています。利用者によるPhotoshop本体での読み込みも確認済みです。

0.1.1から0.1.2への修正内容やファイル構造の調査は、[PICTの仕様調査](PICT-SPEC.md)を参照してください。

## 診断ログ

0.1.2以降は、Photoshopからの呼び出し番号・返却値・エラー内容をmacOSの統合ログへ記録します。画像データはログに含めません。読み込みを試した直後に次のコマンドで確認できます。

```sh
/usr/bin/log show --last 10m --style compact --predicate 'subsystem == "jp.pictimport.photoshop"'
```

## ホームページ

ホームページのソースは`docs/index.html`と`docs/assets/`です。GitHub Pagesは`gh-pages`ブランチのルートから公開しています。更新時は、サイトのHTML・資産・`.nojekyll`・`LICENSE`を同ブランチへ同期してプッシュします。

## 構成

| パス | 内容 |
| --- | --- |
| `src/pict.cpp` | PICTの解析、PackBits展開、描画 |
| `src/plugin.cpp` | PhotoshopのFormatRecord／POSIX I/O接続 |
| `resources/PiPLs.json` | 形式登録、拡張子、読み込み専用の宣言 |
| `resources/Info.plist` | macOSバンドル情報 |
| `resources/installer/` | インストーラーの日本語画面・配置設定 |
| `tests/pict_tests.cpp` | デコーダーと変異入力のテスト |
| `tests/plugin_tests.cpp` | バンドルをロードするホスト接続テスト |
| `scripts/sdk.lock.json` | Adobe SDKの固定版とハッシュ |
| `scripts/build_installer.py` | macOSインストーラーの作成 |
| `scripts/verify_installer.py` | システムへ導入しないパッケージ検証 |
| `scripts/verify_plugin_metadata.py` | PiPLの必須項目と依存条件の検査 |
