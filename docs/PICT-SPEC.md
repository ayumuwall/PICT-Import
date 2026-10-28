# Macintosh PICTの仕様調査と実装方針

調査日：2026-10-28。対象はMacintoshのQuickDraw PICTです。Apple IIgsのQuickDraw IIやMacPaintのファイルとは区別します。

## 一次資料

1. Apple『Inside Macintosh: Imaging With QuickDraw』Appendix A「Picture Opcodes」。Appleが公開していた文書の[保存版の目次](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-458.html)、[Version and Header Opcodes](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-459.html)、[Picture Opcode Data Types](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-460.html)、[Opcodes in Pictures](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-461.html)。形式解析はこの仕様に基づきます。
2. Apple『Inside Macintosh: Imaging With QuickDraw』[CopyBits](https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-166.html)。転送モードと色の扱いの参照資料です。
3. Adobe公式[Photoshop C++ SDK](https://github.com/AdobeDocs/photoshop-cpp-sdk)。使用版は`7e9aca4353a3d4615e9522f47521bf840fb4388c`。`PIFormat.h`、`PITypes.h`、`simpleformat`の登録定義、`pipl-schema.json`でネイティブ形式プラグインの契約を確認しました。
4. Adobeの[Photoshop拡張方式の説明](https://developer.adobe.com/photoshop/)。追加の画像形式にはC++ SDKを利用できます。
5. Apple『Inside Macintosh: QuickTime』Image Compression ManagerのTable 3-1「Fields of the PICT opcode for compressed QuickTime images」（[保存版](https://vintageapple.org/inside_r/pdf/QuickTime_1993.pdf)）。`0x8200`の可変長レコードとImageDescriptionを参照しました。変換行列の固定小数点形式はAppleの[行列仕様](https://developer.apple.com/documentation/quicktime-file-format/matrices)も参照できます。

## PICTは描画命令の記録

PICTの中身は、図形・文字・ビットマップと描画状態を記録した命令列です。写真だけを含むファイルもあれば、ベクター図形や文字が混在するファイルもあります。圧縮データだけを取り出す変換では一般のPICTを復元できないため、命令を順に再生する構成にしました。

複数バイト整数はビッグエンディアンです。`Rect`は`top, left, bottom, right`の符号付き16ビット整数、`Point`は`v, h`の順です。`Fixed`は符号付き16.16固定小数点数です。

## ファイルとバージョンの識別

通常のファイルは、アプリケーション用の512バイトの後にPICT本体が続きます。リソース等から取り出した本体には、この前置きがありません。本実装は開始位置512と0を調べ、矩形とバージョン命令を確認します。

PICT本体先頭の構成は次のとおりです。

| 相対位置 | 内容 |
| --- | --- |
| 0 | `picSize`、2バイト。大きなPICTでは桁あふれするためファイル長には使わない |
| 2 | `picFrame`、8バイト |
| 10 | バージョン命令 |

PICT 1は`11 01`で始まり、以後の命令は1バイトです。PICT 2と拡張PICT 2は`00 11 02 FF`に続いて`0C 00`と24バイトのヘッダーを持ちます。以後の命令は2バイトで、各命令の開始をPICT本体内の偶数位置にそろえます。終端はそれぞれ`FF`、`00 FF`です。

24バイトのヘッダーは、バージョン値が`-1`なら通常のPICT 2、`-2`なら解像度情報を持つ拡張PICT 2です。拡張形式では`hRes`・`vRes`・`srcRect`を読み、ソース画素寸法と解像度をPhotoshopへ渡します。負の原点も、画素バッファの原点へ移して処理します。

## ラスター画像の構造

次の命令では、ビットマップの記述にソース矩形、転送先矩形、転送モード、必要に応じたマスク領域、走査線データが続きます。

| 命令 | 用途 |
| --- | --- |
| `0090`／`0091` | BitsRect／BitsRgn |
| `0098`／`0099` | PackBitsRect／PackBitsRgn |
| `009A`／`009B` | DirectBitsRect／DirectBitsRgn |

`rowBytes`の上位ビットはBitMapとPixMapの識別に使われます。PixMapは追加の形式情報を持ち、パレット画像にはカラーテーブルが続きます。DirectBitsの`baseAddr`は当時のアドレス値であり、ポインターとして参照しません。

本実装のピクセル形式は、白黒1ビット、パレット1／2／4／8ビット、16ビットRGB555、32ビットRGBです。カラーテーブルのインデックスを検査し、未定義の要素や不正な走査線長は拒否します。

## 圧縮と展開

PackBitsの制御バイトは符号付きです。`0..127`は後続の`n+1`単位をそのまま出力、`-127..-1`は次の1単位を`1-n`回繰り返し、`-128`は何もしません。展開後の長さが想定した走査線と一致することを確認します。

圧縮走査線にはバイト数が付き、`rowBytes > 250`なら2バイト、その他は1バイトです。短い走査線には非圧縮の特例があります。16ビット画素の圧縮は2バイトを1単位として展開します。

PixMapの`packType`については、非圧縮、32ビット画素のパッド除去、16ビット単位の圧縮、RGB成分を分けた圧縮を扱います。パッドを透明度とは解釈せず、RGB画像として出力します。

### QuickTime埋め込みJPEG（0.1.3以降）

`0x8200`（CompressedQuickTime）のバージョン0で、`ImageDescription.cType`が`jpeg`の単一フレーム画像をImageIOで展開します。RGBの24／32ビット記述と8ビットグレースケール記述（depth 40）を対象とし、色テーブル・外部データ参照は受け付けません。ソース矩形で切り抜き、正の拡大率と平行移動を適用して、既存のクリップ領域・転送モードで合成します。複数の命令による分割画像も順に配置します。

命令長・ImageDescription長・JPEG長・開始／終端マーカーを検査し、ImageIOでデコードする前にJPEG自身の寸法と記述の一致を確認します。`dataSize`が0の場合は命令内の残りをJPEGとみなし、末尾の1バイトの位置合わせを許容します。埋め込み画像にも既存の画素数・処理量・キャンセル制限を適用します。

JPEG命令の直後に、同じ転送先矩形の白黒`PackBitsRect`がある場合はQuickTime未搭載環境向けの代替表示として描画を抑制します。データ自体は通常どおり読み取り・検査します。別の矩形やカラー画像、他の命令を挟んだ後の画像は通常どおり描画します。

回転・せん断・射影・反転、マット・マスク、JPEG以外のコーデック、`0x8201`（UncompressedQuickTime）は未対応として拒否します。

## 描画と安全性

図形はCoreGraphicsで被覆マスクを生成し、色・8×8パターン・基本の論理転送を適用します。複雑なリージョンは走査線の境界差分を復元してクリップに使います。文字はMacRomanからCoreTextへ渡します。現行フォントによる描画なので、QuickDrawのビットマップフォントを厳密に再現するものではありません。

読み取り位置・命令の長さ・画素数・圧縮の展開量・処理量を検査します。未対応の描画機能は、成功したように見える欠落画像を避けるためエラーとして返します。予約命令はAppleが長さを定義した範囲をスキップします。

## 対応範囲の詳細

| 項目 | 対応範囲 |
| --- | --- |
| 形式 | PICT 1、PICT 2、拡張PICT 2 |
| コンテナー | 512バイトのファイルヘッダーあり、ヘッダーなしの生PICTデータ |
| ビットマップ | 1ビット白黒、1／2／4／8ビットのパレット、16ビットRGB555、32ビットRGB |
| 圧縮 | 非圧縮、PackBits、16ビット単位RLE、RGB成分ごとのRLE、32ビットのパッド除去 |
| QuickTime | 埋め込みJPEG、分割画像、平行移動、正の拡大縮小、ソース矩形による切り抜き（0.1.3以降） |
| 転送 | BitsRect／BitsRgn、PackBitsRect／PackBitsRgn、DirectBitsRect／DirectBitsRgn |
| 図形 | 線、矩形、角丸矩形、楕円、円弧、ポリゴン、領域の塗りつぶし、same系 |
| クリップ | 矩形・走査線形式のリージョン |
| その他 | 8×8の白黒パターン、RGB色指定、基本論理転送モード、反転、拡張ヘッダーの解像度 |
| 文字 | MacRoman、太字・斜体、フォント名指定とシステムフォントへの代替 |

JPEG以外のQuickTime埋め込み画像と上記の未対応変換、PostScript描画コメント、PixPat、算術転送モード、原点の移動、複雑なリージョンの輪郭描画、一部の文字装飾・文字間隔・文字変形は未対応で、エラーとして扱います。色付きソースに対する論理転送やペンの輪郭は、当時のOS・表示デバイスと完全に同じ結果になる保証はありません。

上限は256 MiB／ファイル、32×1024×1024画素、各辺32767画素です。描画命令数・展開量・リージョンの複雑さにも上限を設けています。

## Photoshopとの接続

macOSの`8BIF`形式バンドルに`PluginMain`を公開し、`PiPLs.json`でPICTの拡張子と読み込み機能を登録します。SDKヘッダーを利用してABIを合わせ、`FormatRecord`を独自に再定義しない方針です。

`RequiredHostVersion`を宣言する場合には、`RequiredHost`も必須です。本プラグインはPhotoshopを表す`8BIM`を指定します。この依存条件はAdobe SDKの[`PIGeneral.h`](https://github.com/AdobeDocs/photoshop-cpp-sdk/blob/7e9aca4353a3d4615e9522f47521bf840fb4388c/pluginsdk/photoshopapi/photoshop/PIGeneral.h#L658)に明記されています。0.1.1からビルド・配布時にも検査します。

0.1.2では`SupportsPluginOpeningFile`を宣言し、SDKの`XPlatFileSpec`にある`CFURLRef`からプラグイン自身が読み取り専用でファイルを開きます。準備段階ではI/O方式の交渉だけを行い、まだ有効とは限らないファイル記述子を検査しません。`pread`で読み、プラグインが開いたファイルだけを閉じます。ホストがPOSIX記述子を管理する場合は、交渉済みの記述子のみ利用します。

読み込み開始時に画像サイズ・解像度・8ビットRGBを設定し、64行ずつピクセルを渡します。32ビット座標が通知されない場合も、本形式の上限32767画素の範囲で従来の座標を使用できます。終了・キャンセル・失敗時には読み込み状態を解放します。PICTの書き出し機能は登録しません。

## 検証の範囲

合成データでPICT 1／2、ヘッダー、負の座標、解像度、主要なピクセル形式、圧縮、クリップ、図形、文字、破損・切断・変異入力を検証しました。実ファイル4点のラスターデコードは独立したFFmpegの結果と全画素を照合しました。

さらに生成したUniversalバンドルを`dlopen`し、SDKの構造体を使った模擬ホストで識別・画像受け渡し・チャンク・進捗・キャンセル・エラー処理を検証しました。0.1.2では準備段階に記述子がない場合、日本語パスのファイルURL、16ビット座標、実際のIMAGE029.pctの全行転送も検証しています。

0.1.3では、QuickTime JPEGの分割・移動・拡大縮小・切り抜き・クリップ・代替表示と、未対応形式・不正な長さ・寸法不一致・切断・画素数制限・キャンセルを合成データで検証しました。実ファイルIMAGE005.pctも、JPEGの3帯を独立に展開・連結した640×480の参照RGBAと、デコーダー出力・模擬ホストへの480行の転送結果が全画素一致しました。利用者によるPhotoshop本体での読み込みも確認済みです。

Photoshop 27.11.0上で0.1.1の形式登録を確認した後、同版でIMAGE029.pctの読み込み失敗を再現しました。0.1.2への修正後、利用者からPhotoshop本体でPICTを読み込めたことが確認されています。
