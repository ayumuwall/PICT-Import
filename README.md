# PICT Import

古いMacで作成したPICT画像を、Photoshopで開くためのmacOS用プラグインです。`.pict`・`.pct`・`.pic`ファイルを読み込み、Photoshopで編集したり、PNG・JPEG・PSDなどで保存したりできます。

[ホームページ](https://ayumuwall.github.io/PICT-Import/)

**[PICT Import 0.1.2のインストーラーをダウンロード（macOS）](https://github.com/ayumuwall/PICT-Import/releases/download/v0.1.2/PICT-Import-0.1.2-Installer.pkg)**

[手動インストール用ZIP](https://github.com/ayumuwall/PICT-Import/releases/download/v0.1.2/PICT-Import-0.1.2-macOS.zip) · [リリース一覧](https://github.com/ayumuwall/PICT-Import/releases)

**現在の公開版は0.1.2（開発版）です。Photoshop本体でのPICT読み込みを確認済みです。**

## 必要な環境

| 項目 | 必要なもの |
| --- | --- |
| OS | macOS 12以降（使用するPhotoshopの動作要件も満たすこと） |
| Photoshop | Photoshop 2025（バージョン26.0）以降 |
| Mac | Intel Mac／Apple Silicon Mac |
| インストール時 | Macの管理者認証 |

Photoshopは別途必要です。プラグインはインストーラーだけで導入できます。

## インストール

1. Photoshopで作業中のファイルを保存し、Photoshopを終了します。
2. 上のリンクからダウンロードした`PICT-Import-0.1.2-Installer.pkg`を開きます。
3. インストーラーの案内に従います。管理者認証を求められたら、Macの管理者アカウントで許可してください。
4. Photoshopを起動します。Mac全体の再起動は不要です。

インストーラーは未署名・未公証のため、macOSの警告で止まる場合があります。その場合は[「インストーラーが開けない」](#インストーラーが開けない)を参照してください。

<details>
<summary>ZIPを使って手動でインストールする場合</summary>

1. Photoshopを終了し、[手動インストール用ZIP](https://github.com/ayumuwall/PICT-Import/releases/download/v0.1.2/PICT-Import-0.1.2-macOS.zip)をダウンロード・展開します。
2. Finderの「移動 → フォルダへ移動」で、`/Library/Application Support/`を開きます。
3. その中の`Adobe` → `Plug-Ins` → `CC`フォルダーを開きます。フォルダーがない場合は、この順に作成してください。
4. 展開した`PICT-Import`フォルダー内の`PICT Import.plugin`を、`CC`フォルダーへコピーします。管理者認証を求められる場合があります。
5. Photoshopを起動します。

インストーラーも、手動インストールも、最終的な配置先は同じです。

```text
/Library/Application Support/Adobe/Plug-Ins/CC/PICT Import.plugin
```

この[共有プラグインフォルダー](https://helpx.adobe.com/photoshop/desktop/troubleshoot/tool-feature-issues/photoshop-plug-in-folder-locations.html)に置くと、Mac上の各ユーザーと、対応するPhotoshopの各バージョンで利用できます。

</details>

## PICTを開く・別の形式で保存する

1. Photoshopの「ファイル → 開く」を選びます。
2. PICTファイル（`.pict`・`.pct`・`.pic`）を選んで開きます。形式の指定が必要な場合は「Macintosh PICT」を選んでください。
3. 画像が開いたら、通常のPhotoshop画像と同じように編集できます。
4. PNGなどで保存するには、「ファイル → コピーを保存」から保存形式を選びます。書き出し機能も利用できます。

## Photoshopで開けるPICTと開けないPICTがある理由

Adobeの公式ヘルプでは、Photoshopは**ラスター（ピクセル画像）のPICTを開ける一方、QuickDraw PICTの読み込みとPICT形式への保存には対応しない**と案内されています。「PhotoshopはPICT対応」と書かれていても、すべてのPICTを開けるという意味ではありません。[Adobe：サポートされている画像ファイル形式](https://helpx.adobe.com/jp/photoshop/desktop/save-and-export/export-files-to-different-formats/image-file-formats-supported-in-photoshop.html)

PICTには、画像データだけでなく「線を引く」「文字を描く」「画像を貼り付ける」といったQuickDrawの描画命令も記録できます。同じ`.pict`や`.pct`でも、作成したアプリや保存方法によって中身が異なるのが、互換性をややこしくする理由です。[Apple『Inside Macintosh』の描画命令仕様（保存版）](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-461.html)

| よくある疑問 | 読み込めるかを判断する手がかり |
| --- | --- |
| あるPICTは開くのに、別のPICTは開かない | 写真のような画像データが中心か、図形・文字の描画命令を含むかなど、ファイルの中身に違いがあります。 |
| PICT 1なら図形、PICT 2なら写真？ | 1／2は形式のバージョンです。どちらにも描画命令があるため、この番号だけで読み込めるとは判断できません。 |
| 「Macintosh PICT」を指定すれば必ず開く？ | 形式の指定は自動判別を補う操作です。Adobeも、実際の形式が違う場合や破損したファイルは開けないと案内しています。 |

バージョンと描画命令の関係は[AppleのPICTヘッダー仕様（保存版）](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-459.html)、形式を指定する操作は[Adobeのファイル読み込み手順](https://helpx.adobe.com/photoshop/desktop/create-open-import-images/import-files/open-and-import-files-in-photoshop.html)を参照できます。

PICT Importは、画像データに加えて基本的な図形や文字の描画命令も処理します。標準の読み込みで開けないPICTを試すためのプラグインですが、対応範囲は次のとおりです。

## 読み込める画像と制限

PICT 1・PICT 2に対応し、白黒・パレットカラー・RGBの画像、基本的な図形や文字を読み込めます。ただし、PICTの内容によっては読み込めないものや、当時と見た目が異なるものがあります。

- **図形や文字も画像になります。** ベクターや文字としての再編集、元のレイヤー・透明度・ICCプロファイルの復元はできません。背景は白になります。
- **文字の再現には制限があります。** MacRomanの欧文文字が対象です。日本語などは正しく再現できず、欧文も現在のフォントで描くため字形や字幅が変わる場合があります。
- **QuickTime形式でJPEGなどを埋め込んだPICTや、一部の特殊な描画には未対応です。** 未対応機能を含むファイルは、読み込みエラーになります。
- **MacBinary・AppleDouble・StuffItや、リソースフォーク全体は直接読み込めません。** これらに格納された画像は、先にPICTデータを取り出す必要があります。

圧縮方式・描画機能・画像サイズの上限など、詳しい対応範囲は[PICTの仕様と対応範囲](docs/PICT-SPEC.md)を参照してください。

## 困ったとき

### インストーラーが開けない

「開発元を確認できない」「Appleで悪質なソフトウェアかどうかを確認できない」と表示される場合があります。本リポジトリのリリースから入手したファイルであることを確認し、インストールする場合は次の手順で、このインストーラーの実行を許可します。

1. 一度インストーラーを開き、警告が出たら閉じます。
2. Macの「システム設定 → プライバシーとセキュリティ」を開きます。macOS 12では「システム環境設定 → セキュリティとプライバシー → 一般」です。
3. 対象のインストーラーに対して「このまま開く」を選び、確認画面の案内に従います。

詳しくは[Appleの案内](https://support.apple.com/ja-jp/102445)を参照してください。会社・学校などで管理されているMacで許可できない場合は、管理者へ相談してください。

### PhotoshopでPICTを選べない・プラグインが見つからない

次の順に確認してください。

1. インストール後、Photoshopを終了して起動し直したか。
2. Photoshopが2025（26.0）以降か。
3. 「Photoshop → プラグインについて」に「Macintosh PICT」が表示されるか。
4. `/Library/Application Support/Adobe/Plug-Ins/CC/`に`PICT Import.plugin`があるか。

以前、Photoshopのバージョン別`Plug-ins`フォルダーに手動で入れた場合は、同じプラグインが二重に配置されていないかも確認してください。

### 特定のPICTだけ開けない・表示が元と違う

[読み込める画像と制限](#読み込める画像と制限)を確認してください。未対応の埋め込み画像や描画機能、文字・フォントの違いなどが原因になることがあります。

解決しない場合は、[GitHub Issues](https://github.com/ayumuwall/PICT-Import/issues)へ、macOS・Photoshop・PICT Importのバージョン、Macの種類（Intel／Apple Silicon）、表示されたエラーを添えて報告してください。元のPICTを作成したアプリ名や、共有可能なサンプルがあると調査に役立ちます。

## 更新・アンインストール

**更新：** Photoshopを終了し、新しいバージョンのインストーラーを実行してください。同じ場所にある旧版を更新します。

**アンインストール：** Photoshopを終了し、Finderの「移動 → フォルダへ移動」で次のフォルダーを開き、`PICT Import.plugin`をゴミ箱へ移動します。管理者認証を求められる場合があります。

```text
/Library/Application Support/Adobe/Plug-Ins/CC/
```

その後、Photoshopを起動し直してください。

## 開発・技術資料

- [ソースからのビルド・テスト・診断ログ](https://github.com/ayumuwall/PICT-Import/blob/main/docs/DEVELOPMENT.md)
- [PICTの仕様と対応範囲](docs/PICT-SPEC.md)

## ライセンス

[MITライセンス](LICENSE)で公開しています。

AdobeおよびPhotoshopはAdobeの商標です。本プロジェクトはAdobe公式製品ではありません。
