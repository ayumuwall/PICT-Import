#include "pict.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

namespace {
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void word(Bytes& b, int n) { b.push_back((unsigned(n) >> 8) & 255); b.push_back(unsigned(n) & 255); }
void longword(Bytes& b, std::uint32_t n) { word(b, n >> 16); word(b, n); }
void rect(Bytes& b, int t, int l, int bottom, int right) {
    word(b, t); word(b, l); word(b, bottom); word(b, right);
}
void color(Bytes& b, int red, int green, int blue) {
    word(b, red * 257); word(b, green * 257); word(b, blue * 257);
}
struct Picture {
    Bytes bytes;
    bool v2;
    Picture(int width, int height, bool version2 = true, int left = 0, int top = 0, int dpi = 72)
        : v2(version2) {
        word(bytes, 0); rect(bytes, 0, 0, height, width);
        if (v2) {
            word(bytes, 0x11); word(bytes, 0x2ff); word(bytes, 0xc00);
            word(bytes, -2); word(bytes, 0);
            longword(bytes, dpi << 16); longword(bytes, dpi << 16);
            rect(bytes, top, left, top + height, left + width); longword(bytes, 0);
        } else { bytes.push_back(0x11); bytes.push_back(1); }
    }
    void op(int n) {
        if (v2) { if (bytes.size() & 1) bytes.push_back(0); word(bytes, n); }
        else bytes.push_back(n);
    }
    void rgb(int red, int green, int blue) { op(0x1a); color(bytes, red, green, blue); }
    Bytes finish(bool fileHeader = false) {
        op(0xff);
        if (fileHeader) {
            Bytes result(512, 0x5a);
            result.insert(result.end(), bytes.begin(), bytes.end());
            return result;
        }
        return bytes;
    }
};

pict::Image decode(const Bytes& b) { return pict::decode(b.data(), b.size()); }
void pixel(const pict::Image& image, int x, int y, std::array<int, 3> expected) {
    const auto i = (std::size_t(y) * image.width + x) * 4;
    for (int c = 0; c < 3; ++c)
        check(image.rgba[i + c] == expected[c], "画素（" + std::to_string(x) + "," + std::to_string(y) +
            "）の成分" + std::to_string(c) + ": " + std::to_string(image.rgba[i + c]) +
            " != " + std::to_string(expected[c]));
    check(image.rgba[i + 3] == 255, "不透明度が不正です。");
}
void rejects(const Bytes& b, const std::string& fragment = "") {
    try { decode(b); }
    catch (const pict::Error& e) {
        check(std::string(e.what()).find(fragment) != std::string::npos, std::string("想定外のエラー: ") + e.what());
        return;
    }
    throw std::runtime_error("不正なPICTが受理されました。");
}
void pixmap(Bytes& b, int width, int height, int rowBytes, int bits, int packing, int components = 3) {
    word(b, 0x8000 | rowBytes); rect(b, 0, 0, height, width);
    word(b, 0); word(b, packing); longword(b, 0);
    longword(b, 72 << 16); longword(b, 72 << 16);
    word(b, bits <= 8 ? 0 : 16); word(b, bits);
    word(b, bits <= 8 ? 1 : components); word(b, bits <= 8 ? bits : bits == 16 ? 5 : 8);
    longword(b, 0); longword(b, 0); longword(b, 0);
}
void copyRects(Bytes& b, int width, int height) {
    rect(b, 0, 0, height, width); rect(b, 0, 0, height, width); word(b, 0);
}
void save(const Bytes& b, const std::string& name) {
    std::filesystem::create_directories("build/fixtures");
    std::ofstream f("build/fixtures/" + name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), b.size());
}

Bytes jpeg(int red, int green, int blue, bool split = false) {
    Bytes rgba(16 * 16 * 4, 255);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const auto i = (y * 16 + x) * 4;
            rgba[i] = split && y >= 8 ? 0 : red;
            rgba[i + 1] = split && y >= 8 ? 0 : green;
            rgba[i + 2] = split && y >= 8 ? 255 : blue;
        }
    auto space = CGColorSpaceCreateDeviceRGB();
    auto context = CGBitmapContextCreate(rgba.data(), 16, 16, 8, 64, space,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    check(context, "テストJPEGの描画メモリを確保できません。");
    auto image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    check(image, "テストJPEGの元画像を作成できません。");
    auto data = CFDataCreateMutable(nullptr, 0);
    check(data, "テストJPEGの出力メモリを確保できません。");
    auto destination = CGImageDestinationCreateWithData(data, CFSTR("public.jpeg"), 1, nullptr);
    check(destination, "テストJPEGのエンコーダーを作成できません。");
    CGImageDestinationAddImage(destination, image, nullptr);
    check(CGImageDestinationFinalize(destination), "テストJPEGを作成できません。");
    Bytes result(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
    CFRelease(destination); CFRelease(data); CGImageRelease(image);
    return result;
}

// 戻り値はQuickTime命令のペイロード先頭。破損データ検査にも使う。
std::size_t quickTime(Picture& p, const Bytes& image, int x = 0, int y = 0,
                      int scale = 65536, bool unknownSize = false) {
    Bytes q;
    word(q, 0);
    for (int value : {scale, 0, 0, 0, scale, 0, x * 65536, y * 65536, 0x40000000}) longword(q, value);
    longword(q, 0); rect(q, 0, 0, 0, 0); word(q, 64);
    rect(q, 0, 0, 16, 16); longword(q, 768); longword(q, 0);
    longword(q, 86); longword(q, 0x6a706567);
    longword(q, 0); word(q, 0); word(q, 0);
    word(q, 1); word(q, 1); longword(q, 0x6170706c);
    longword(q, 0); longword(q, 1023); word(q, 16); word(q, 16);
    longword(q, 72 << 16); longword(q, 72 << 16);
    longword(q, unknownSize ? 0 : image.size()); word(q, 1);
    q.resize(q.size() + 32, 0); word(q, 24); word(q, -1);
    q.insert(q.end(), image.begin(), image.end());
    if (q.size() & 1) q.push_back(0);
    p.op(0x8200); longword(p.bytes, q.size());
    const auto start = p.bytes.size();
    p.bytes.insert(p.bytes.end(), q.begin(), q.end());
    return start;
}

void placeholder(Picture& p, int x, int y, int size = 16) {
    p.op(0x98); word(p.bytes, 2); rect(p.bytes, 0, 0, 16, 16);
    rect(p.bytes, 0, 0, 16, 16); rect(p.bytes, y, x, y + size, x + size); word(p.bytes, 0);
    p.bytes.insert(p.bytes.end(), 32, 0xff);
}

void nearPixel(const pict::Image& image, int x, int y, std::array<int, 3> color) {
    const auto i = (std::size_t(y) * image.width + x) * 4;
    for (int c = 0; c < 3; ++c)
        check(std::abs(int(image.rgba[i + c]) - color[c]) <= 20,
            "JPEGの画素（" + std::to_string(x) + "," + std::to_string(y) + "）の成分" +
            std::to_string(c) + ": " + std::to_string(image.rgba[i + c]) + " != " + std::to_string(color[c]));
}

void testQuickTime() {
    const auto red = jpeg(255, 0, 0), blue = jpeg(0, 0, 255);
    Picture p(16, 40);
    quickTime(p, red); placeholder(p, 0, 0);
    quickTime(p, blue, 0, 16, 65536, true); placeholder(p, 0, 16);
    p.rgb(0, 255, 0); p.op(0x31); rect(p.bytes, 32, 0, 40, 16);
    const auto bytes = p.finish(true); save(bytes, "quicktime-jpeg.pict");
    const auto image = decode(bytes);
    nearPixel(image, 4, 4, {255, 0, 0}); nearPixel(image, 4, 20, {0, 0, 255});
    pixel(image, 4, 36, {0, 255, 0});
    Picture scaled(40, 40);
    scaled.op(1); word(scaled.bytes, 10); rect(scaled.bytes, 4, 4, 30, 30);
    quickTime(scaled, jpeg(255, 0, 0, true), 2, 2, 2 * 65536);
    const auto resized = decode(scaled.finish());
    nearPixel(resized, 6, 6, {255, 0, 0}); nearPixel(resized, 6, 27, {0, 0, 255});
    pixel(resized, 2, 2, {255, 255, 255}); pixel(resized, 31, 20, {255, 255, 255});
    Picture cropped(16, 16);
    const auto crop = quickTime(cropped, jpeg(255, 0, 0, true), -4, -4);
    cropped.bytes[crop + 57] = 8; // ソース矩形の下端を8へ変更する。
    const auto partial = decode(cropped.finish());
    nearPixel(partial, 2, 2, {255, 0, 0}); pixel(partial, 2, 5, {255, 255, 255});
    Picture reduced(16, 16);
    quickTime(reduced, red, 2, 2, 32768);
    const auto shrunk = decode(reduced.finish());
    nearPixel(shrunk, 2, 2, {255, 0, 0}); nearPixel(shrunk, 9, 9, {255, 0, 0});
    pixel(shrunk, 10, 9, {255, 255, 255});
    Picture normal(32, 16);
    quickTime(normal, red); placeholder(normal, 16, 0);
    const auto following = decode(normal.finish());
    nearPixel(following, 4, 4, {255, 0, 0}); pixel(following, 20, 4, {0, 0, 0});
    Picture intervening(16, 16);
    quickTime(intervening, red); intervening.rgb(0, 0, 0); placeholder(intervening, 0, 0);
    pixel(decode(intervening.finish()), 4, 4, {0, 0, 0});
}

void testQuickTimeFailures() {
    Picture p(16, 16);
    const auto start = quickTime(p, jpeg(255, 0, 0));
    const auto original = p.finish();
    const auto corruptWord = [&](std::size_t offset, int value, const char* message) {
        auto bytes = original;
        bytes[start + offset] = (unsigned(value) >> 8) & 255;
        bytes[start + offset + 1] = unsigned(value) & 255;
        rejects(bytes, message);
    };
    corruptWord(0, 1, "バージョン");
    corruptWord(6, 1, "回転");
    corruptWord(38, 1, "マット");
    corruptWord(64, 1, "マスク");
    corruptWord(50, 32, "転送モード");
    corruptWord(58, 17, "ソース矩形");
    corruptWord(72, 0, "JPEGのみ");
    corruptWord(82, 1, "外部データ");
    corruptWord(100, 17, "寸法");
    corruptWord(116, 2, "複数フレーム");
    corruptWord(150, 8, "ピクセル形式");
    auto tooShort = original; tooShort[start + 71] = 85; rejects(tooShort, "ImageDescription");
    auto tooLong = original; tooLong[start + 68] = 0x7f; rejects(tooLong, "途中");
    auto badDataSize = original; badDataSize[start + 112] = 0xff; rejects(badDataSize, "途中");
    auto badJpeg = original; badJpeg[start + 154] = 0; rejects(badJpeg, "終端");
    auto missingEnd = original;
    for (std::size_t i = start + 154; i + 1 < missingEnd.size(); ++i)
        if (missingEnd[i] == 0xff && missingEnd[i + 1] == 0xd9) missingEnd[i + 1] = 0;
    rejects(missingEnd, "終端");
    Picture corruptJpeg(16, 16);
    quickTime(corruptJpeg, {0xff, 0xd8, 0, 0, 0, 0, 0xff, 0xd9});
    rejects(corruptJpeg.finish());
    for (std::size_t i = start; i < original.size(); ++i)
        rejects(Bytes(original.begin(), original.begin() + i));
    int cancellationChecks = 0;
    try {
        pict::decode(original.data(), original.size(), {}, [&] { return ++cancellationChecks >= 5; });
        throw std::runtime_error("QuickTimeのキャンセルが効いていません。");
    } catch (const pict::Error& e) {
        check(std::string(e.what()).find("キャンセル") != std::string::npos,
              "QuickTimeのキャンセル以外のエラーが発生しました。");
    }
    Picture small(1, 1); quickTime(small, jpeg(255, 0, 0)); const auto smallBytes = small.finish();
    try {
        pict::decode(smallBytes.data(), smallBytes.size(), {16, 1024 * 1024, 1024});
        throw std::runtime_error("QuickTime画像の画素数制限が効いていません。");
    } catch (const pict::Error&) {}
}

void testVector() {
    Picture p(32, 16);
    p.rgb(255, 0, 0); p.op(0x31); rect(p.bytes, 0, 0, 8, 32);
    auto bytes = p.finish(true); save(bytes, "red-top-v2.pict");
    const auto image = decode(bytes);
    check(image.width == 32 && image.height == 16, "画像寸法が不正です。");
    pixel(image, 0, 0, {255, 0, 0}); pixel(image, 31, 7, {255, 0, 0});
    pixel(image, 0, 8, {255, 255, 255}); pixel(image, 31, 15, {255, 255, 255});
    check(pict::probe(bytes.data(), bytes.size()), "通常のファイルを識別できません。");
    check(!pict::probe(nullptr, 1000), "NULLが受理されました。");
    for (std::size_t i = 0; i < bytes.size(); ++i) rejects(Bytes(bytes.begin(), bytes.begin() + i));
}
void testVersion1() {
    Picture p(8, 4, false);
    p.op(0x31); rect(p.bytes, 0, 0, 2, 8);
    const auto bytes = p.finish(true); save(bytes, "black-top-v1.pct");
    const auto image = decode(bytes);
    pixel(image, 0, 0, {0, 0, 0}); pixel(image, 7, 1, {0, 0, 0});
    pixel(image, 0, 2, {255, 255, 255});
}
void testExtendedHeader() {
    Picture p(4, 2, true, -10, 12, 144);
    p.rgb(0, 0, 255); p.op(0x31); rect(p.bytes, 12, -10, 14, -6);
    const auto image = decode(p.finish());
    pixel(image, 0, 0, {0, 0, 255}); pixel(image, 3, 1, {0, 0, 255});
    check(image.horizontalResolution == 144 && image.verticalResolution == 144, "解像度が保存されません。");
    Picture original(4, 2);
    original.bytes[16] = original.bytes[17] = original.bytes[18] = original.bytes[19] = 255;
    const auto standard = decode(original.finish());
    check(standard.width == 4 && standard.horizontalResolution == 72, "通常のPICT 2ヘッダーが不正です。");
}
void testMonochrome() {
    Picture p(8, 2);
    p.op(0x90); word(p.bytes, 2); rect(p.bytes, 0, 0, 2, 8); copyRects(p.bytes, 8, 2);
    p.bytes.insert(p.bytes.end(), {0xaa, 0, 0x55, 0});
    const auto image = decode(p.finish());
    pixel(image, 0, 0, {0, 0, 0}); pixel(image, 1, 0, {255, 255, 255});
    pixel(image, 0, 1, {255, 255, 255}); pixel(image, 1, 1, {0, 0, 0});
    Picture packed(64, 2, false);
    packed.op(0x98); word(packed.bytes, 8); rect(packed.bytes, 0, 0, 2, 64); copyRects(packed.bytes, 64, 2);
    packed.bytes.insert(packed.bytes.end(), {2, 0xf9, 0xaa, 2, 0xf9, 0x55});
    const auto bytes = packed.finish(true); save(bytes, "packbits-v1.pict");
    const auto result = decode(bytes);
    pixel(result, 62, 0, {0, 0, 0}); pixel(result, 63, 0, {255, 255, 255});
    pixel(result, 62, 1, {255, 255, 255});
}
void testPalette(int bits, bool packed) {
    Picture p(8, 2);
    p.op(packed ? 0x98 : 0x90);
    const int rowBytes = std::max(2, bits);
    pixmap(p.bytes, 8, 2, rowBytes, bits, 0);
    longword(p.bytes, 0); word(p.bytes, 0); word(p.bytes, 1);
    word(p.bytes, 0); color(p.bytes, 255, 0, 0);
    word(p.bytes, 1); color(p.bytes, 0, 0, 255);
    copyRects(p.bytes, 8, 2);
    Bytes row(rowBytes, 0);
    for (int x = 0; x < 8; ++x) row[(x * bits) / 8] |= (x & 1) << (8 - bits - (x * bits) % 8);
    for (int y = 0; y < 2; ++y) {
        if (packed && rowBytes >= 8) { p.bytes.push_back(rowBytes + 1); p.bytes.push_back(rowBytes - 1); }
        p.bytes.insert(p.bytes.end(), row.begin(), row.end());
    }
    const auto bytes = p.finish(true);
    if (bits == 8 && packed) save(bytes, "indexed-8bit.pict");
    const auto image = decode(bytes);
    pixel(image, 0, 0, {255, 0, 0}); pixel(image, 7, 1, {0, 0, 255});
}
void testDirect16(bool packed, bool wide = false) {
    const int width = wide ? 128 : 4;
    Picture p(width, 2);
    p.op(0x9a); longword(p.bytes, 0xff);
    pixmap(p.bytes, width, 2, width * 2, 16, packed ? 3 : 1);
    copyRects(p.bytes, width, 2);
    for (int y = 0; y < 2; ++y) {
        if (packed) {
            if (wide) word(p.bytes, 3); else p.bytes.push_back(3);
            p.bytes.push_back(1 - width);
            word(p.bytes, y == 0 ? 0x7c00 : 0x03e0);
        } else for (int x = 0; x < width; ++x) word(p.bytes, y == 0 ? 0x7c00 : 0x03e0);
    }
    const auto bytes = p.finish(true);
    if (packed && !wide) save(bytes, "direct-16bit.pict");
    const auto image = decode(bytes);
    pixel(image, 0, 0, {255, 0, 0}); pixel(image, width - 1, 1, {0, 255, 0});
}
void testDirect32(int packing, int components = 3, bool narrow = false) {
    const int width = narrow ? 1 : 2;
    Picture p(width, 2);
    p.op(0x9a); longword(p.bytes, 0xff);
    pixmap(p.bytes, width, 2, width * 4, 32, packing, components);
    copyRects(p.bytes, width, 2);
    for (int y = 0; y < 2; ++y) {
        Bytes row;
        if (packing == 4 && !narrow) {
            if (components == 4) row.insert(row.end(), {0, 0});
            row.insert(row.end(), {255, 0, 0, 255, 0, 0});
            p.bytes.push_back(row.size() + 1); p.bytes.push_back(row.size() - 1);
        } else {
            for (int x = 0; x < width; ++x) {
                if (packing != 2 || narrow) row.push_back(0);
                row.push_back(x == 0 ? 255 : 0); row.push_back(x == 0 ? 0 : 255); row.push_back(0);
            }
        }
        p.bytes.insert(p.bytes.end(), row.begin(), row.end());
    }
    const auto bytes = p.finish(true);
    if (packing == 4 && components == 3 && !narrow) save(bytes, "direct-32bit.pict");
    const auto image = decode(bytes);
    pixel(image, 0, 0, {255, 0, 0});
    if (!narrow) pixel(image, 1, 1, {0, 255, 0});
}
void testRegion() {
    Picture p(4, 2);
    p.op(1); word(p.bytes, 36); rect(p.bytes, 0, 0, 2, 4);
    for (int n : {0, 0, 2, 32767, 1, 0, 4, 32767, 2, 2, 4, 32767, 32767}) word(p.bytes, n);
    p.op(0x31); rect(p.bytes, 0, 0, 2, 4);
    const auto image = decode(p.finish());
    pixel(image, 0, 0, {0, 0, 0}); pixel(image, 2, 0, {255, 255, 255});
    pixel(image, 0, 1, {255, 255, 255}); pixel(image, 3, 1, {0, 0, 0});
    Picture b(8, 2);
    b.op(0x91); word(b.bytes, 2); rect(b.bytes, 0, 0, 2, 8); copyRects(b.bytes, 8, 2);
    word(b.bytes, 10); rect(b.bytes, 0, 2, 2, 4);
    b.bytes.insert(b.bytes.end(), {255, 255, 255, 255});
    const auto clipped = decode(b.finish());
    pixel(clipped, 1, 0, {255, 255, 255}); pixel(clipped, 2, 0, {0, 0, 0});
    pixel(clipped, 4, 1, {255, 255, 255});
}
void testPatternsAndModes() {
    Picture p(8, 8);
    p.op(0xa); p.bytes.insert(p.bytes.end(), {0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55});
    p.op(0x34); rect(p.bytes, 0, 0, 8, 8);
    p.op(0x33); rect(p.bytes, 0, 0, 4, 4);
    const auto image = decode(p.finish());
    pixel(image, 0, 0, {255, 255, 255}); pixel(image, 1, 0, {0, 0, 0});
    pixel(image, 4, 0, {0, 0, 0}); pixel(image, 4, 1, {255, 255, 255});
    Picture xorP(4, 2);
    xorP.op(8); word(xorP.bytes, 10);
    xorP.op(0x31); rect(xorP.bytes, 0, 0, 2, 4);
    xorP.op(0x39);
    const auto result = decode(xorP.finish());
    pixel(result, 0, 0, {255, 255, 255});
}
void testShapesAndText() {
    Picture p(32, 32);
    p.op(0x51); rect(p.bytes, 2, 2, 18, 18);
    auto image = decode(p.finish());
    pixel(image, 10, 10, {0, 0, 0}); pixel(image, 2, 2, {255, 255, 255});
    Picture poly(16, 16);
    poly.op(0x71); word(poly.bytes, 22); rect(poly.bytes, 0, 0, 16, 16);
    rect(poly.bytes, 0, 0, 0, 16); word(poly.bytes, 16); word(poly.bytes, 8);
    image = decode(poly.finish()); pixel(image, 8, 4, {0, 0, 0}); pixel(image, 0, 15, {255, 255, 255});
    Picture text(80, 24);
    text.op(0x28); word(text.bytes, 16); word(text.bytes, 2);
    text.bytes.insert(text.bytes.end(), {4, 'P', 'I', 'C', 'T'});
    image = decode(text.finish());
    check(std::any_of(image.rgba.begin(), image.rgba.end(), [](auto b) { return b == 0; }), "文字が描画されません。");
    pixel(image, 0, 23, {255, 255, 255});
}
void testReservedAndAlignment() {
    Picture p(4, 2);
    p.op(0x24); word(p.bytes, 3); p.bytes.insert(p.bytes.end(), {1, 2, 3});
    p.op(0x100); word(p.bytes, 0);
    p.op(0x8100); longword(p.bytes, 3); p.bytes.insert(p.bytes.end(), {1, 2, 3});
    p.op(0x8000); p.op(0x31); rect(p.bytes, 0, 0, 2, 4);
    pixel(decode(p.finish()), 3, 1, {0, 0, 0});
}
void testFailures() {
    Picture qt(4, 2); qt.op(0x8201); longword(qt.bytes, 0); rejects(qt.finish(), "QuickTime");
    Picture unsupported(4, 2); unsupported.op(0x12); rejects(unsupported.finish(), "未対応");
    Picture text(4, 2); text.op(6); longword(text.bytes, 1); rejects(text.finish(), "未対応");
    Picture size(4, 2); auto bytes = size.finish();
    try { pict::decode(bytes.data(), bytes.size(), {4, 1000, 100}); throw std::runtime_error("画素数制限が効いていません。"); }
    catch (const pict::Error&) {}
    try { pict::decode(bytes.data(), bytes.size(), {}, [] { return true; }); throw std::runtime_error("キャンセルが効いていません。"); }
    catch (const pict::Error&) {}
    Picture broken(64, 1, false);
    broken.op(0x98); word(broken.bytes, 8); rect(broken.bytes, 0, 0, 1, 64); copyRects(broken.bytes, 64, 1);
    broken.bytes.insert(broken.bytes.end(), {2, 0x81, 0xff}); rejects(broken.finish(), "PackBits");
    Picture shortRow(64, 1, false);
    shortRow.op(0x98); word(shortRow.bytes, 8); rect(shortRow.bytes, 0, 0, 1, 64); copyRects(shortRow.bytes, 64, 1);
    shortRow.bytes.insert(shortRow.bytes.end(), {2, 0xff, 0xff}); rejects(shortRow.finish(), "PackBits");
    Picture noEnd(4, 2); rejects(noEnd.bytes, "途中");
}

void testExternalSamples() {
    const char* directory = std::getenv("PICT_TEST_SAMPLE_DIR");
    if (!directory) return;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension != ".pict" && extension != ".pct" && extension != ".pic") continue;
        std::ifstream file(entry.path(), std::ios::binary);
        Bytes data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto image = decode(data);
        check(image.rgba.size() == std::size_t(image.width) * image.height * 4, "外部PICTの画素数が不正です。");
        if (const char* referenceDirectory = std::getenv("PICT_TEST_REFERENCE_DIR")) {
            const auto path = std::filesystem::path(referenceDirectory) / (entry.path().stem().string() + ".rgba");
            std::ifstream referenceFile(path, std::ios::binary);
            check(referenceFile.good(), "参照画像を開けません。");
            const Bytes reference{std::istreambuf_iterator<char>(referenceFile), std::istreambuf_iterator<char>()};
            check(reference == image.rgba, "参照デコーダーのRGBAと一致しません: " + entry.path().filename().string());
        }
        std::cout << "外部サンプル: " << entry.path().filename().string() << ": " << image.width << " × " << image.height << '\n';
        ++count;
    }
    check(count > 0, "外部PICTサンプルがありません。");
}

void testMutations() {
    Picture p(16, 16);
    p.op(0x31); rect(p.bytes, 0, 0, 16, 16);
    const auto bytes = p.finish();
    std::mt19937 random(0x50494354);
    for (int n = 0; n < 2000; ++n) {
        Bytes mutation = bytes;
        for (int j = 0; j < 1 + n % 4; ++j) mutation[random() % mutation.size()] = random() & 255;
        try {
            const auto image = pict::decode(mutation.data(), mutation.size(), {4096, 1024 * 1024, 1024});
            check(image.rgba.size() == std::size_t(image.width) * image.height * 4, "変異入力で画素数が不正です。");
        } catch (const pict::Error&) {}
    }
}
}

int main() {
    std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"PICT 2・上下の向き・ヘッダー・全位置での切断", testVector},
        {"PICT 1", testVersion1},
        {"拡張ヘッダー・負の座標・解像度", testExtendedHeader},
        {"モノクロ・PICT 1 PackBits", testMonochrome},
        {"インデックス1/2/4/8ビット", [] { for (int bits : {1, 2, 4, 8}) { testPalette(bits, false); testPalette(bits, true); } }},
        {"16ビットRGB555・ワードPackBits・2バイト長", [] { testDirect16(false); testDirect16(true); testDirect16(true, true); }},
        {"32ビットRGB・圧縮形式1/2/4・4成分・短い走査線", [] {
            testDirect32(1); testDirect32(2); testDirect32(4); testDirect32(4, 4);
            testDirect32(1, 3, true); testDirect32(2, 3, true); testDirect32(4, 3, true); }},
        {"複雑なクリップ・BitsRgn", testRegion},
        {"パターン・反転・XOR", testPatternsAndModes},
        {"楕円・ポリゴン・文字", testShapesAndText},
        {"予約命令の長さ・ワード境界", testReservedAndAlignment},
        {"QuickTime JPEG・分割・移動・拡大・切り抜き・代替表示", testQuickTime},
        {"QuickTime JPEGの破損・未対応形式・上限・キャンセル", testQuickTimeFailures},
        {"未対応命令・破損・上限・キャンセル", testFailures},
        {"2000件の変異入力・境界検査", testMutations}
    };
    if (std::getenv("PICT_TEST_SAMPLE_DIR")) tests.push_back({"外部PICTサンプル", testExternalSamples});
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "成功: " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "失敗: " << name << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size() - failed << " / " << tests.size() << " 項目成功\n";
    return failed ? 1 : 0;
}
