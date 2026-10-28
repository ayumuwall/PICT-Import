#include "pict.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace pict {
namespace {

struct Rect {
    int top = 0, left = 0, bottom = 0, right = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool contains(int x, int y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

struct Point { int x = 0, y = 0; };
using Color = std::array<std::uint8_t, 3>;
using Pattern = std::array<std::uint8_t, 8>;
constexpr Color black{0, 0, 0}, white{255, 255, 255};
constexpr Pattern solid{255, 255, 255, 255, 255, 255, 255, 255};

class Reader {
public:
    const std::uint8_t* data;
    std::size_t size, pos;
    Reader(const std::uint8_t* bytes, std::size_t length, std::size_t offset = 0)
        : data(bytes), size(length), pos(offset) {}
    void need(std::size_t n) const {
        if (pos > size || n > size - pos) throw Error("PICTデータが途中で切れています。");
    }
    std::uint8_t u8() { need(1); return data[pos++]; }
    int s8() { return static_cast<std::int8_t>(u8()); }
    std::uint16_t u16() { const auto a = u8(); return (a << 8) | u8(); }
    int s16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() { const auto a = u16(); return (std::uint32_t(a) << 16) | u16(); }
    double fixed() { return static_cast<std::int32_t>(u32()) / 65536.0; }
    void skip(std::size_t n) { need(n); pos += n; }
    Rect rect() {
        Rect result;
        result.top = s16(); result.left = s16();
        result.bottom = s16(); result.right = s16();
        return result;
    }
    Point point() { const int y = s16(), x = s16(); return {x, y}; }
    Color color() {
        Color c{};
        for (auto& v : c) v = static_cast<std::uint8_t>((u16() + 128u) / 257u);
        return c;
    }
    Pattern pattern() { Pattern p{}; for (auto& b : p) b = u8(); return p; }
};

bool headerAt(const std::uint8_t* data, std::size_t size, std::size_t offset) {
    if (!data || offset > size || size - offset < 12) return false;
    Reader r(data, size, offset);
    r.skip(2);
    const auto bounds = r.rect();
    if (bounds.width() <= 0 || bounds.height() <= 0) return false;
    if (r.u8() == 0x11 && r.u8() == 1) return true;
    r.pos = offset + 10;
    return size - r.pos >= 4 && r.u16() == 0x0011 && r.u16() == 0x02ff;
}

std::size_t locate(const std::uint8_t* data, std::size_t size) {
    if (headerAt(data, size, 512)) return 512;
    if (headerAt(data, size, 0)) return 0;
    throw Error("PICT 1／2のヘッダーを識別できません。リソースフォークやMacBinaryは展開してください。");
}

std::size_t pixelCount(int width, int height, const Limits& limits) {
    if (width <= 0 || height <= 0 || width > 32767 || height > 32767 ||
        std::size_t(width) > limits.maxPixels / std::size_t(height)) {
        throw Error("画像サイズが不正、または画素数の上限（標準32メガピクセル）を超えています。");
    }
    return std::size_t(width) * std::size_t(height);
}

struct Band {
    int top, bottom;
    std::vector<int> edges;
};

struct Region {
    Rect bounds;
    bool rectangular = true;
    std::vector<Band> bands;
    bool contains(int x, int y) const {
        if (!bounds.contains(x, y)) return false;
        if (rectangular) return true;
        const auto band = std::lower_bound(bands.begin(), bands.end(), y,
            [](const Band& b, int row) { return b.bottom <= row; });
        if (band == bands.end() || y < band->top) return false;
        const auto n = std::upper_bound(band->edges.begin(), band->edges.end(), x) - band->edges.begin();
        return (n & 1) != 0;
    }
    void translate(Point p) {
        bounds.left += p.x; bounds.right += p.x;
        bounds.top += p.y; bounds.bottom += p.y;
        for (auto& b : bands) {
            b.top += p.y; b.bottom += p.y;
            for (auto& x : b.edges) x += p.x;
        }
    }
};

Region readRegion(Reader& outer) {
    const auto start = outer.pos;
    const auto length = outer.u16();
    if (length < 10 || (length & 1)) throw Error("リージョンのサイズが不正です。");
    outer.need(length - 2);
    Reader r(outer.data, start + length, outer.pos);
    outer.pos = start + length;
    Region region;
    region.bounds = r.rect();
    if (region.bounds.width() < 0 || region.bounds.height() < 0)
        throw Error("リージョンの矩形が不正です。");
    if (length == 10) return region;
    region.rectangular = false;
    std::set<int> active;
    std::size_t edgeCount = 0;
    int previousY = region.bounds.top;
    while (true) {
        const int y = r.s16();
        if (y == 32767) break;
        if (y < previousY || y > region.bounds.bottom || y < region.bounds.top)
            throw Error("リージョンの走査線が不正です。");
        if (y > previousY && !active.empty()) {
            edgeCount += active.size();
            if (edgeCount > 1024 * 1024) throw Error("リージョンの複雑さが上限を超えています。");
            region.bands.push_back({previousY, y, {active.begin(), active.end()}});
        }
        int previousX = std::numeric_limits<int>::min();
        while (true) {
            const int x = r.s16();
            if (x == 32767) break;
            if (x <= previousX || x < region.bounds.left || x > region.bounds.right)
                throw Error("リージョンの境界が不正です。");
            previousX = x;
            const auto it = active.find(x);
            if (it == active.end()) active.insert(x); else active.erase(it);
        }
        if (active.size() & 1) throw Error("リージョンの境界の組が不正です。");
        previousY = y;
    }
    if (!active.empty() || r.pos != r.size) throw Error("リージョンの終端が不正です。");
    return region;
}

std::vector<std::uint8_t> unpack(Reader& r, std::size_t packedSize,
                               std::size_t outputSize, std::size_t unit) {
    r.need(packedSize);
    Reader row(r.data, r.pos + packedSize, r.pos);
    r.pos += packedSize;
    std::vector<std::uint8_t> result;
    result.reserve(outputSize);
    while (row.pos < row.size) {
        const int control = row.s8();
        if (control == -128) continue;
        const auto count = std::size_t(control >= 0 ? control + 1 : 1 - control) * unit;
        if (count > outputSize - result.size()) throw Error("PackBitsの展開量が走査線の長さを超えています。");
        if (control >= 0) {
            row.need(count);
            result.insert(result.end(), row.data + row.pos, row.data + row.pos + count);
            row.skip(count);
        } else {
            row.need(unit);
            for (std::size_t i = 0; i < count; i += unit)
                result.insert(result.end(), row.data + row.pos, row.data + row.pos + unit);
            row.skip(unit);
        }
    }
    if (result.size() != outputSize) throw Error("PackBitsの展開後の走査線が短すぎます。");
    return result;
}

struct Bitmap {
    Rect bounds;
    std::vector<std::uint8_t> rgb;
};

Color oldColor(std::uint32_t index) {
    switch (index) {
        case 30: return white;
        case 33: return black;
        case 69: return {255, 255, 0};
        case 137: return {255, 0, 255};
        case 205: return {255, 0, 0};
        case 273: return {0, 255, 255};
        case 341: return {0, 255, 0};
        case 409: return {0, 0, 255};
        default: throw Error("未対応のQuickDrawカラー番号です。");
    }
}

void validateMode(unsigned mode) {
    mode &= ~64u; // ditherCopyは8ビットRGBでは色数の削減を行わない。
    if (mode > 15 && mode != 36) throw Error("未対応のQuickDraw転送モードです。");
}

struct PathDeleter { void operator()(CGPath* p) const { if (p) CGPathRelease(p); } };
using Path = std::unique_ptr<CGPath, PathDeleter>;
struct ContextDeleter { void operator()(CGContext* p) const { if (p) CGContextRelease(p); } };
using Context = std::unique_ptr<CGContext, ContextDeleter>;

template<class T> class CFObject {
    T value;
public:
    explicit CFObject(T value) : value(value) {}
    ~CFObject() { if (value) CFRelease(value); }
    CFObject(const CFObject&) = delete;
    CFObject& operator=(const CFObject&) = delete;
    T get() const { return value; }
    void reset(T replacement) { if (value) CFRelease(value); value = replacement; }
};

class Decoder {
    Reader r;
    const Limits& limits;
    const std::function<bool()>& cancelled;
    Image image;
    Rect frame;
    bool version2 = false;
    std::size_t start = 0, work = 0;
    Region clip;
    Color fg = black, bg = white;
    Pattern pen = solid, fill = solid, background{};
    Point origin, penPosition, ovalSize{16, 16}, penSize{1, 1}, textPosition;
    Rect lastRect;
    std::vector<Point> lastPoly;
    Region lastRegion;
    bool haveRect = false, havePoly = false, haveRegion = false;
    unsigned penMode = 8, textMode = 1;
    int fontID = 0, fontSize = 12;
    unsigned fontFace = 0;
    std::string fontName;
    bool pendingQuickTimeFallback = false;
    Rect quickTimeDestination;

    void checkCancelled() const {
        if (cancelled && cancelled()) throw Error("読み込みをキャンセルしました。");
    }
    void charge(std::size_t n) {
        constexpr std::size_t maxWork = 512ull * 1024 * 1024;
        if (n > maxWork - work) throw Error("描画処理量の上限を超えています。");
        work += n;
        checkCancelled();
    }
    Rect intersect(Rect a, Rect b) const {
        return {std::max(a.top, b.top), std::max(a.left, b.left),
                std::min(a.bottom, b.bottom), std::min(a.right, b.right)};
    }
    Point device(Point p) const { return {p.x + origin.x, p.y + origin.y}; }
    Rect device(Rect b) const {
        return {b.top + origin.y, b.left + origin.x, b.bottom + origin.y, b.right + origin.x};
    }
    void pixel(int x, int y, Color source, unsigned mode, bool invert = false) {
        if (!clip.contains(x, y)) return;
        auto* d = &image.rgba[(std::size_t(y - frame.top) * image.width + x - frame.left) * 4];
        mode &= ~64u;
        if (mode == 36) { if (source == bg) return; mode = 0; }
        mode &= 7;
        for (int c = 0; c < 3; ++c) {
            const auto s = source[c];
            if (invert) { d[c] ^= 255; continue; }
            switch (mode) {
                case 0: d[c] = s; break;
                case 1: d[c] &= s; break;
                case 2: d[c] = ~(d[c] ^ s); break;
                case 3: d[c] |= ~s; break;
                case 4: d[c] = ~s; break;
                case 5: d[c] &= ~s; break;
                case 6: d[c] ^= s; break;
                case 7: d[c] |= s; break;
            }
        }
    }
    Color patternColor(const Pattern& pat, int x, int y) const {
        return pat[unsigned(y) & 7] & (0x80 >> (unsigned(x) & 7)) ? fg : bg;
    }
    // CoreGraphicsでは形状の被覆だけを作り、QuickDrawのビット演算はRGB上で実行する。
    template<class Draw> void mask(Rect bounds, unsigned verb, Draw draw) {
        bounds = intersect(intersect(bounds, frame), clip.bounds);
        if (bounds.width() <= 0 || bounds.height() <= 0) return;
        const auto count = std::size_t(bounds.width()) * bounds.height();
        charge(count);
        std::vector<std::uint8_t> coverage(count, 0);
        auto gray = CGColorSpaceCreateDeviceGray();
        Context context(CGBitmapContextCreate(coverage.data(), bounds.width(), bounds.height(),
                                             8, bounds.width(), gray, kCGImageAlphaNone));
        CGColorSpaceRelease(gray);
        if (!context) throw Error("図形描画用メモリを確保できません。");
        CGContextSetShouldAntialias(context.get(), false);
        CGContextSetGrayFillColor(context.get(), 1, 1);
        CGContextSetGrayStrokeColor(context.get(), 1, 1);
        CGContextTranslateCTM(context.get(), -bounds.left, bounds.bottom);
        CGContextScaleCTM(context.get(), 1, -1);
        draw(context.get());
        const Pattern& p = verb == 2 ? background : verb == 4 ? fill : pen;
        const unsigned mode = verb == 2 || verb == 4 ? 0 : penMode;
        for (int y = bounds.top; y < bounds.bottom; ++y)
            for (int x = bounds.left; x < bounds.right; ++x)
                if (coverage[std::size_t(y - bounds.top) * bounds.width() + x - bounds.left])
                    pixel(x, y, patternColor(p, x, y), mode, verb == 3);
    }
    void path(CGPathRef p, unsigned verb) {
        if (verb == 0 && (penSize.x == 0 || penSize.y == 0)) return;
        const auto box = CGPathGetBoundingBox(p);
        const double expansion = verb == 0 ? std::max(penSize.x, penSize.y) + 1 : 1;
        Rect b{int(std::floor(box.origin.y - expansion)), int(std::floor(box.origin.x - expansion)),
               int(std::ceil(box.origin.y + box.size.height + expansion)),
               int(std::ceil(box.origin.x + box.size.width + expansion))};
        mask(b, verb, [&](CGContextRef ctx) {
            CGContextAddPath(ctx, p);
            if (verb == 0) {
                // QuickDrawのペンは長方形。等幅以外は変換した座標系でストロークする。
                CGContextScaleCTM(ctx, penSize.x, penSize.y);
                CGContextSetLineWidth(ctx, 1);
                CGContextSetLineCap(ctx, kCGLineCapSquare);
                CGContextStrokePath(ctx);
            } else CGContextEOFillPath(ctx);
        });
    }
    void rectangle(unsigned group, unsigned verb, Rect b, int angle = 0, int sweep = 0) {
        if (b.width() <= 0 || b.height() <= 0) return;
        b = device(b);
        const auto cg = CGRectMake(b.left, b.top, b.width(), b.height());
        Path p(CGPathCreateMutable());
        if (group == 3) {
            if (verb == 0) {
                const auto inset = CGRectMake(b.left + penSize.x / 2.0, b.top + penSize.y / 2.0,
                    std::max(0, b.width() - penSize.x), std::max(0, b.height() - penSize.y));
                CGPathAddRect(p.get(), nullptr, inset);
            } else CGPathAddRect(p.get(), nullptr, cg);
        } else if (group == 4) {
            CGPathAddRoundedRect(p.get(), nullptr, cg,
                std::min(ovalSize.x / 2.0, b.width() / 2.0), std::min(ovalSize.y / 2.0, b.height() / 2.0));
        } else if (group == 5) CGPathAddEllipseInRect(p.get(), nullptr, cg);
        else {
            const double pi = std::acos(-1.0);
            auto transform = CGAffineTransformMake(b.width() / 2.0, 0, 0, b.height() / 2.0,
                b.left + b.width() / 2.0, b.top + b.height() / 2.0);
            if (verb != 0) CGPathMoveToPoint(p.get(), &transform, 0, 0);
            CGPathAddArc(p.get(), &transform, 0, 0, 1, (angle - 90) * pi / 180,
                (angle + sweep - 90) * pi / 180, sweep < 0);
            if (verb != 0) CGPathCloseSubpath(p.get());
        }
        path(p.get(), verb);
    }
    void line(Point from, Point to) {
        from = device(from); to = device(to);
        Path p(CGPathCreateMutable());
        CGPathMoveToPoint(p.get(), nullptr, from.x + penSize.x / 2.0, from.y + penSize.y / 2.0);
        CGPathAddLineToPoint(p.get(), nullptr, to.x + penSize.x / 2.0, to.y + penSize.y / 2.0);
        path(p.get(), 0);
    }
    void polygon(unsigned verb) {
        if (lastPoly.empty()) return;
        Path p(CGPathCreateMutable());
        auto v = device(lastPoly.front());
        CGPathMoveToPoint(p.get(), nullptr, v.x, v.y);
        for (std::size_t i = 1; i < lastPoly.size(); ++i) {
            v = device(lastPoly[i]); CGPathAddLineToPoint(p.get(), nullptr, v.x, v.y);
        }
        if (verb != 0) CGPathCloseSubpath(p.get());
        path(p.get(), verb);
    }
    void region(unsigned verb) {
        Region region = lastRegion;
        region.translate(origin);
        Path p(CGPathCreateMutable());
        if (region.rectangular) {
            auto b = region.bounds;
            CGPathAddRect(p.get(), nullptr, CGRectMake(b.left, b.top, b.width(), b.height()));
        } else {
            for (const auto& band : region.bands)
                for (std::size_t i = 0; i < band.edges.size(); i += 2)
                    CGPathAddRect(p.get(), nullptr, CGRectMake(band.edges[i], band.top,
                        band.edges[i + 1] - band.edges[i], band.bottom - band.top));
        }
        if (verb == 0 && !region.rectangular)
            throw Error("複雑なリージョンの輪郭描画は未対応です。");
        path(p.get(), verb);
    }
    void readBitmap(bool direct, bool packed, bool quickTimeFallback = false) {
        if (direct) r.skip(4); // 記録されたbaseAddrは使用しない。
        const auto rowField = r.u16();
        const bool pixmap = (rowField & 0x8000) != 0;
        const auto rowBytes = rowField & 0x3fff;
        Bitmap bitmap;
        bitmap.bounds = r.rect();
        const auto width = bitmap.bounds.width(), height = bitmap.bounds.height();
        const auto count = pixelCount(width, height, limits);
        charge(count);
        if (rowBytes == 0 || (rowBytes & 1)) throw Error("ビットマップのrowBytesが不正です。");
        unsigned packType = 0, pixelType = 0, bits = 1, components = 1, componentBits = 1;
        if (pixmap) {
            if (r.u16() != 0) throw Error("未対応のPixMapバージョンです。");
            packType = r.u16(); r.skip(12);
            pixelType = r.u16(); bits = r.u16();
            components = r.u16(); componentBits = r.u16(); r.skip(12);
        } else if (direct) throw Error("DirectBitsにPixMapがありません。");
        const bool indexed = bits == 1 || bits == 2 || bits == 4 || bits == 8;
        if ((indexed && (pixelType != 0 || components != 1 || componentBits != bits)) ||
            (!indexed && !(pixelType == 16 && ((bits == 16 && components == 3 && componentBits == 5) ||
                (bits == 32 && (components == 3 || components == 4) && componentBits == 8)))))
            throw Error("未対応のPixMapピクセル形式です。");
        if (unsigned(rowBytes) < (unsigned(width) * bits + 7) / 8)
            throw Error("ビットマップの走査線が画像幅より短すぎます。");
        if (packType > 4 || (packType == 2 && bits != 32) ||
            (packType == 3 && bits != 16) || (packType == 4 && bits != 32))
            throw Error("未対応のPixMap圧縮形式です。");
        std::array<Color, 256> palette{};
        std::array<bool, 256> present{};
        if (pixmap && indexed) {
            r.skip(4);
            const auto flags = r.u16();
            const auto entries = std::size_t(r.u16()) + 1;
            if (entries > (1u << bits)) throw Error("カラーテーブルの要素数が不正です。");
            for (std::size_t i = 0; i < entries; ++i) {
                const auto value = r.u16();
                const auto index = flags & 0x8000 ? i : value;
                if (index >= (1u << bits) || present[index]) throw Error("カラーテーブルの番号が不正です。");
                palette[index] = r.color(); present[index] = true;
            }
        } else if (indexed) {
            palette[0] = bg; palette[1] = fg; present[0] = present[1] = true;
        }
        // 描画パラメーターはピクセルデータに先行するため、呼び出し側に戻してから展開する。
        bitmap.rgb.resize(count * 3);
        const auto source = r.rect(), destination = r.rect();
        const auto mode = r.u16();
        validateMode(mode);
        Region bitmapClip{frame, true, {}};
        if (pendingBitmapRegion) { bitmapClip = readRegion(r); bitmapClip.translate(origin); }
        if (source.width() <= 0 || source.height() <= 0 || destination.width() <= 0 || destination.height() <= 0 ||
            source.left < bitmap.bounds.left || source.top < bitmap.bounds.top ||
            source.right > bitmap.bounds.right || source.bottom > bitmap.bounds.bottom)
            throw Error("CopyBitsの転送矩形が不正です。");
        if (packType == 0 && !indexed) packType = bits == 16 ? 3 : 4;
        const bool compressed = rowBytes >= 8 && packType != 1 && packType != 2 &&
                                (direct || packed);
        const bool planar = bits == 32 && packType == 4 && compressed;
        if (bits == 32 && (rowBytes % 4)) throw Error("32ビット画像のrowBytesが不正です。");
        const std::size_t planeWidth = rowBytes / 4;
        std::size_t decodedBytes = rowBytes;
        if (planar) decodedBytes = planeWidth * components;
        else if (packType == 2 && rowBytes >= 8) decodedBytes = planeWidth * 3;
        for (int y = 0; y < height; ++y) {
            checkCancelled();
            std::vector<std::uint8_t> row;
            if (compressed) {
                const auto byteCount = rowBytes > 250 ? r.u16() : r.u8();
                row = unpack(r, byteCount, decodedBytes, packType == 3 ? 2 : 1);
            } else {
                r.need(decodedBytes);
                row.assign(r.data + r.pos, r.data + r.pos + decodedBytes);
                r.skip(decodedBytes);
            }
            for (int x = 0; x < width; ++x) {
                Color color{};
                if (indexed) {
                    const unsigned bit = unsigned(x) * bits;
                    const unsigned index = (row[bit / 8] >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
                    if (!present[index]) throw Error("画像が存在しないパレット要素を参照しています。");
                    color = palette[index];
                } else if (bits == 16) {
                    const auto value = (unsigned(row[x * 2]) << 8) | row[x * 2 + 1];
                    for (int c = 0; c < 3; ++c)
                        color[c] = static_cast<std::uint8_t>((((value >> (10 - c * 5)) & 31) * 255 + 15) / 31);
                } else if (planar) {
                    const auto first = components == 4 ? planeWidth : 0;
                    for (int c = 0; c < 3; ++c) color[c] = row[first + planeWidth * c + x];
                } else {
                    const std::size_t stride = packType == 2 && rowBytes >= 8 ? 3 : 4;
                    const std::size_t offset = stride == 4 ? 1 : 0;
                    for (int c = 0; c < 3; ++c) color[c] = row[x * stride + offset + c];
                }
                std::copy(color.begin(), color.end(), bitmap.rgb.begin() + (std::size_t(y) * width + x) * 3);
            }
        }
        const auto dest = device(destination);
        // QuickTimeが直後に付ける、同じ転送先の白黒PackBitsRectは代替表示用。
        if (quickTimeFallback && !pixmap && mode == 0 &&
            dest.top == quickTimeDestination.top && dest.left == quickTimeDestination.left &&
            dest.bottom == quickTimeDestination.bottom && dest.right == quickTimeDestination.right) return;
        const auto area = intersect(intersect(intersect(dest, frame), clip.bounds), bitmapClip.bounds);
        if (area.width() > 0 && area.height() > 0) {
            charge(std::size_t(area.width()) * area.height());
            for (int y = area.top; y < area.bottom; ++y) {
                const int sy = source.top + std::int64_t(y - dest.top) * source.height() / dest.height() - bitmap.bounds.top;
                for (int x = area.left; x < area.right; ++x) {
                    if (!bitmapClip.contains(x, y)) continue;
                    const int sx = source.left + std::int64_t(x - dest.left) * source.width() / dest.width() - bitmap.bounds.left;
                    const auto i = (std::size_t(sy) * width + sx) * 3;
                    pixel(x, y, {bitmap.rgb[i], bitmap.rgb[i + 1], bitmap.rgb[i + 2]}, mode);
                }
            }
        }
    }
    bool pendingBitmapRegion = false;

    void quickTime() {
        const auto length = r.u32();
        r.need(length);
        Reader q(r.data, r.pos + length, r.pos);
        r.skip(length);
        if (q.u16() != 0) throw Error("未対応のQuickTime画像命令のバージョンです。");
        std::array<std::int32_t, 9> matrix{};
        for (auto& value : matrix) value = static_cast<std::int32_t>(q.u32());
        // 16.16の拡大率・移動量と2.30の同次座標。回転・せん断・射影は拒否する。
        if (matrix[0] <= 0 || matrix[4] <= 0 || matrix[1] || matrix[2] ||
            matrix[3] || matrix[5] || matrix[8] != 0x40000000)
            throw Error("QuickTime画像の回転・せん断・射影・反転は未対応です。");
        const double scaleX = matrix[0] / 65536.0, scaleY = matrix[4] / 65536.0;
        const double offsetX = matrix[6] / 65536.0 + origin.x;
        const double offsetY = matrix[7] / 65536.0 + origin.y;
        const auto matteSize = q.u32(); q.skip(8);
        const auto mode = q.u16(); validateMode(mode);
        const auto source = q.rect(); q.skip(4);
        const auto maskSize = q.u32();
        if (matteSize || maskSize) throw Error("QuickTime画像のマット・マスクは未対応です。");
        const auto descriptionStart = q.pos;
        const auto descriptionSize = q.u32();
        if (descriptionSize < 86) throw Error("QuickTime画像のImageDescriptionが短すぎます。");
        q.need(descriptionSize - 4);
        if (q.u32() != 0x6a706567) throw Error("QuickTime埋め込み画像はJPEGのみ対応しています。");
        q.skip(6);
        if (q.u16()) throw Error("外部データを参照するQuickTime画像は未対応です。");
        q.skip(16);
        const int width = q.u16(), height = q.u16();
        const auto count = pixelCount(width, height, limits);
        q.skip(8);
        std::size_t dataSize = q.u32();
        if (q.u16() != 1) throw Error("複数フレームのQuickTime画像は未対応です。");
        q.skip(32);
        const auto depth = q.u16();
        if ((depth != 24 && depth != 32 && depth != 40) || q.s16() != -1)
            throw Error("未対応のQuickTime JPEGピクセル形式です。");
        q.pos = descriptionStart + descriptionSize;
        if (source.width() <= 0 || source.height() <= 0 || source.left < 0 || source.top < 0 ||
            source.right > width || source.bottom > height)
            throw Error("QuickTime画像のソース矩形が不正です。");
        if (!dataSize) {
            dataSize = q.size - q.pos;
            // dataSizeが不明な場合も、命令末尾の偶数位置合わせだけを取り除く。
            if (dataSize >= 3 && q.data[q.size - 1] == 0 &&
                q.data[q.size - 3] == 0xff && q.data[q.size - 2] == 0xd9) --dataSize;
        }
        q.need(dataSize);
        const auto* jpeg = q.data + q.pos;
        q.skip(dataSize);
        if (q.size - q.pos > 1 || (q.pos < q.size && q.u8() != 0) || dataSize < 4 ||
            jpeg[0] != 0xff || jpeg[1] != 0xd8 || jpeg[dataSize - 2] != 0xff || jpeg[dataSize - 1] != 0xd9)
            throw Error("QuickTime JPEGの長さ・終端が不正です。");
        charge(count);
        const CFObject<CFDataRef> data(CFDataCreateWithBytesNoCopy(nullptr, jpeg, dataSize, kCFAllocatorNull));
        if (!data.get()) throw std::bad_alloc();
        const CFObject<CGImageSourceRef> decoder(CGImageSourceCreateWithData(data.get(), nullptr));
        if (!decoder.get() || !CGImageSourceGetType(decoder.get()) ||
            !CFEqual(CGImageSourceGetType(decoder.get()), CFSTR("public.jpeg")) ||
            CGImageSourceGetCount(decoder.get()) != 1)
            throw Error("QuickTime JPEGを識別できません。");
        // デコード前にJPEG自身の寸法を確認し、記述との不一致や過大な展開を拒否する。
        const CFObject<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(decoder.get(), 0, nullptr));
        int jpegWidth = 0, jpegHeight = 0;
        const auto dimension = [&](CFStringRef key, int& value) {
            if (!properties.get()) return false;
            const auto number = CFDictionaryGetValue(properties.get(), key);
            return number && CFGetTypeID(number) == CFNumberGetTypeID() &&
                CFNumberGetValue(static_cast<CFNumberRef>(number), kCFNumberIntType, &value);
        };
        if (!dimension(kCGImagePropertyPixelWidth, jpegWidth) ||
            !dimension(kCGImagePropertyPixelHeight, jpegHeight) || jpegWidth != width || jpegHeight != height)
            throw Error("QuickTime画像とJPEGの寸法が一致しません。");
        const void* keys[]{kCGImageSourceShouldCacheImmediately};
        const void* values[]{kCFBooleanTrue};
        const CFObject<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        if (!options.get()) throw std::bad_alloc();
        const CFObject<CGImageRef> decoded(CGImageSourceCreateImageAtIndex(decoder.get(), 0, options.get()));
        if (!decoded.get() || CGImageSourceGetStatusAtIndex(decoder.get(), 0) != kCGImageStatusComplete ||
            CGImageGetWidth(decoded.get()) != std::size_t(width) || CGImageGetHeight(decoded.get()) != std::size_t(height))
            throw Error("QuickTime JPEGをデコードできません。");
        std::vector<std::uint8_t> rgba(count * 4);
        const CFObject<CGColorSpaceRef> space(CGColorSpaceCreateDeviceRGB());
        if (!space.get()) throw std::bad_alloc();
        Context context(CGBitmapContextCreate(rgba.data(), width, height, 8, std::size_t(width) * 4,
            space.get(), kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big));
        if (!context) throw Error("QuickTime JPEGの描画用メモリを確保できません。");
        CGContextDrawImage(context.get(), CGRectMake(0, 0, width, height), decoded.get());
        checkCancelled();
        const double left = offsetX + source.left * scaleX, top = offsetY + source.top * scaleY;
        const double right = offsetX + source.right * scaleX, bottom = offsetY + source.bottom * scaleY;
        quickTimeDestination = {int(std::floor(top)), int(std::floor(left)),
                                int(std::ceil(bottom)), int(std::ceil(right))};
        pendingQuickTimeFallback = true;
        const auto area = intersect(intersect(quickTimeDestination, frame), clip.bounds);
        if (area.width() <= 0 || area.height() <= 0) return;
        charge(std::size_t(area.width()) * area.height());
        for (int y = area.top; y < area.bottom; ++y) {
            checkCancelled();
            const double sy = (y + 0.5 - offsetY) / scaleY;
            if (sy < source.top || sy >= source.bottom) continue;
            for (int x = area.left; x < area.right; ++x) {
                const double sx = (x + 0.5 - offsetX) / scaleX;
                if (sx < source.left || sx >= source.right) continue;
                const auto i = (std::size_t(sy) * width + std::size_t(sx)) * 4;
                pixel(x, y, {rgba[i], rgba[i + 1], rgba[i + 2]}, mode);
            }
        }
    }

    void text(unsigned opcode) {
        if (opcode == 0x28) textPosition = r.point();
        else {
            if (opcode == 0x29 || opcode == 0x2b) textPosition.x += r.u8();
            if (opcode == 0x2a || opcode == 0x2b) textPosition.y += r.u8();
        }
        const auto length = r.u8();
        r.need(length);
        if (!length) return;
        if (std::abs(std::int64_t(textPosition.x)) > 1000000 ||
            std::abs(std::int64_t(textPosition.y)) > 1000000)
            throw Error("文字の描画位置が大きすぎます。");
        if (fontID >= 16384 || fontID == 23)
            throw Error("MacRoman以外の文字コードを使用するフォントは未対応です。");
        if (fontFace & ~3u) throw Error("斜体・太字以外のQuickDraw文字装飾は未対応です。");
        const CFObject<CFStringRef> string(CFStringCreateWithBytes(nullptr, r.data + r.pos, length, kCFStringEncodingMacRoman, false));
        r.skip(length);
        if (!string.get()) throw Error("MacRomanの文字列を解釈できません。");
        std::string name = fontName.empty() ? (fontID == 4 ? "Monaco" : fontID == 20 ? "Times" : "Helvetica") : fontName;
        const CFObject<CFStringRef> cfName(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(name.data()), name.size(),
                                                   kCFStringEncodingMacRoman, false));
        if (!cfName.get()) throw Error("フォント名を解釈できません。");
        CFObject<CTFontRef> font(CTFontCreateWithName(cfName.get(), fontSize, nullptr));
        if (!font.get()) throw std::bad_alloc();
        if (fontFace) {
            const auto traits = (fontFace & 1 ? kCTFontBoldTrait : 0) | (fontFace & 2 ? kCTFontItalicTrait : 0);
            auto replacement = CTFontCreateCopyWithSymbolicTraits(font.get(), 0, nullptr, traits, traits);
            if (replacement) font.reset(replacement);
        }
        const void* keys[]{kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
        const void* values[]{font.get(), kCFBooleanTrue};
        const CFObject<CFDictionaryRef> attrs(CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks));
        if (!attrs.get()) throw std::bad_alloc();
        const CFObject<CFAttributedStringRef> attributed(CFAttributedStringCreate(nullptr, string.get(), attrs.get()));
        if (!attributed.get()) throw std::bad_alloc();
        const CFObject<CTLineRef> line(CTLineCreateWithAttributedString(attributed.get()));
        if (!line.get()) throw std::bad_alloc();
        const auto position = device(textPosition);
        const auto bounds = CTLineGetBoundsWithOptions(line.get(), kCTLineBoundsUseGlyphPathBounds);
        const auto advance = CTLineGetTypographicBounds(line.get(), nullptr, nullptr, nullptr);
        Rect area{int(std::floor(position.y - CGRectGetMaxY(bounds))) - 2,
                  int(std::floor(position.x + CGRectGetMinX(bounds))) - 2,
                  int(std::ceil(position.y - CGRectGetMinY(bounds))) + 2,
                  int(std::ceil(position.x + CGRectGetMaxX(bounds))) + 2};
        const auto savedPen = pen;
        const auto savedMode = penMode;
        pen = solid; penMode = textMode;
        mask(area, 1, [&](CGContextRef ctx) {
            CGContextTranslateCTM(ctx, position.x, position.y);
            CGContextScaleCTM(ctx, 1, -1);
            CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
            CGContextSetTextPosition(ctx, 0, 0);
            CTLineDraw(line.get(), ctx);
        });
        pen = savedPen; penMode = savedMode;
        textPosition.x += int(std::round(advance));
    }
    void unsupported(unsigned opcode) {
        std::ostringstream message;
        message << "未対応の描画命令 0x" << std::hex << opcode << "（位置 0x" << r.pos << "）です。";
        throw Error(message.str());
    }
    void opcode(unsigned op) {
        const bool quickTimeFallback = std::exchange(pendingQuickTimeFallback, false);
        switch (op) {
            case 0x00: return;
            case 0x01: clip = readRegion(r); clip.translate(origin); return;
            case 0x02: background = r.pattern(); return;
            case 0x03: fontID = r.s16(); fontName.clear(); return;
            case 0x04: fontFace = r.u8(); return;
            case 0x05: textMode = r.u16(); validateMode(textMode); return;
            case 0x06: if (r.u32()) unsupported(op); return;
            case 0x07: penSize = r.point();
                if (penSize.x < 0 || penSize.y < 0) throw Error("ペンサイズが不正です。");
                return;
            case 0x08: penMode = r.u16(); validateMode(penMode); return;
            case 0x09: pen = r.pattern(); return;
            case 0x0a: fill = r.pattern(); return;
            case 0x0b: ovalSize = r.point();
                if (ovalSize.x < 0 || ovalSize.y < 0) throw Error("角丸のサイズが不正です。");
                return;
            case 0x0c: { const int dx = r.s16(), dy = r.s16();
                if (dx || dy) unsupported(op);
                return; }
            case 0x0d: fontSize = r.u16();
                if (fontSize == 0 || fontSize > 4096) throw Error("文字サイズが不正です。");
                return;
            case 0x0e: fg = oldColor(r.u32()); return;
            case 0x0f: bg = oldColor(r.u32()); return;
            case 0x10: { const auto numerator = r.point(), denominator = r.point();
                if (numerator.x != denominator.x || numerator.y != denominator.y ||
                    !denominator.x || !denominator.y) unsupported(op); return; }
            case 0x15: case 0x16: if (r.u16()) unsupported(op); return;
            case 0x1a: fg = r.color(); return;
            case 0x1b: bg = r.color(); return;
            case 0x1c: unsupported(op); return;
            case 0x1e: return;
            case 0x1d: case 0x1f: r.skip(6); return;
            case 0x20: { const auto from = r.point(); penPosition = r.point(); line(from, penPosition); return; }
            case 0x21: { const auto from = penPosition; penPosition = r.point(); line(from, penPosition); return; }
            case 0x22: case 0x23: {
                if (op == 0x22) penPosition = r.point();
                const auto from = penPosition;
                const auto dx = r.s8(), dy = r.s8();
                penPosition.x += dx; penPosition.y += dy; line(from, penPosition); return; }
            case 0x28: case 0x29: case 0x2a: case 0x2b: text(op); return;
            case 0x2c: { const auto length = r.u16();
                if (length < 3) throw Error("フォント名レコードが不正です。");
                r.need(length); const auto end = r.pos + length;
                fontID = r.s16(); const auto n = r.u8();
                if (n > end - r.pos) throw Error("フォント名の長さが不正です。");
                fontName.assign(reinterpret_cast<const char*>(r.data + r.pos), n); r.pos = end; return; }
            case 0x90: case 0x91: case 0x98: case 0x99: case 0x9a: case 0x9b:
                pendingBitmapRegion = (op & 1) != 0;
                readBitmap(op >= 0x9a, op >= 0x98, quickTimeFallback && op == 0x98); return;
            case 0xa0: { const auto kind = r.u16();
                if ((kind >= 190 && kind <= 199) || (kind >= 150 && kind <= 155))
                    throw Error("文字レイアウト／PostScript描画コメントを含むPICTは未対応です。");
                return; }
            case 0xa1: { const auto kind = r.u16(); const auto n = r.u16();
                if ((kind >= 190 && kind <= 199) || (kind >= 150 && kind <= 155))
                    throw Error("文字レイアウト／PostScript描画コメントを含むPICTは未対応です。");
                r.skip(n); return; }
            case 0x8200: quickTime(); return;
            case 0x8201:
                throw Error("非圧縮QuickTime画像を含むPICTは未対応です。");
        }
        if (op >= 0x30 && op <= 0x6c && (op & 7) <= 4) {
            const auto group = op >> 4, verb = op & 7;
            if (!(op & 8)) { lastRect = r.rect(); haveRect = true; }
            if (!haveRect) throw Error("再利用する図形の矩形がありません。");
            int angle = 0, sweep = 0;
            if (group == 6) { angle = r.s16(); sweep = r.s16(); }
            rectangle(group, verb, lastRect, angle, sweep); return;
        }
        if (op >= 0x70 && op <= 0x7c && (op & 7) <= 4) {
            if (!(op & 8)) {
                const auto length = r.u16();
                if (length < 10 || (length - 10) % 4) throw Error("ポリゴンのサイズが不正です。");
                r.need(length - 2); r.skip(8); lastPoly.clear();
                for (unsigned i = 0; i < (length - 10) / 4; ++i) lastPoly.push_back(r.point());
                havePoly = true;
            }
            if (!havePoly) throw Error("再利用するポリゴンがありません。");
            polygon(op & 7); return;
        }
        if (op >= 0x80 && op <= 0x8c && (op & 7) <= 4) {
            if (!(op & 8)) { lastRegion = readRegion(r); haveRegion = true; }
            if (!haveRegion) throw Error("再利用するリージョンがありません。");
            region(op & 7); return;
        }
        // Appleが長さを定義した予約命令だけをスキップする。
        if ((op >= 0x24 && op <= 0x27) || op == 0x2f ||
            (op >= 0x92 && op <= 0x97) || (op >= 0x9c && op <= 0x9f) ||
            (op >= 0xa2 && op <= 0xaf)) { const auto n = r.u16(); r.skip(n); return; }
        if ((op >= 0x17 && op <= 0x19) || (op >= 0xb0 && op <= 0xcf) ||
            (op >= 0x8000 && op <= 0x80ff)) return;
        if ((op >= 0xd0 && op <= 0xfe) || op >= 0x8100) { const auto n = r.u32(); r.skip(n); return; }
        if (op >= 0x100 && op <= 0x7fff && op != 0xc00) { r.skip((op >> 8) * 2); return; }
        unsupported(op);
    }
public:
    Decoder(const std::uint8_t* data, std::size_t size, const Limits& limits,
            const std::function<bool()>& cancelled)
        : r(data, size), limits(limits), cancelled(cancelled) {}
    Image run() {
        if (r.size > limits.maxFileBytes) throw Error("PICTファイルがサイズの上限を超えています。");
        start = locate(r.data, r.size);
        r.pos = start + 2; frame = r.rect();
        version2 = r.data[r.pos] == 0;
        r.skip(version2 ? 4 : 2);
        if (version2) {
            if (r.u16() != 0xc00) throw Error("PICT 2のHeaderOpがありません。");
            const auto header = r.pos;
            r.need(24);
            const auto version = r.s16(); r.skip(2);
            if (version == -2) {
                image.horizontalResolution = r.fixed(); image.verticalResolution = r.fixed();
                frame = r.rect();
                if (!std::isfinite(image.horizontalResolution) || !std::isfinite(image.verticalResolution) ||
                    image.horizontalResolution <= 0 || image.verticalResolution <= 0)
                    throw Error("PICTの解像度が不正です。");
            } else if (version != -1) throw Error("未対応のPICT 2ヘッダーバージョンです。");
            r.pos = header + 24;
        }
        const auto count = pixelCount(frame.width(), frame.height(), limits);
        image.width = frame.width(); image.height = frame.height();
        image.rgba.assign(count * 4, 255);
        clip.bounds = frame;
        for (std::size_t n = 0; n < limits.maxOpcodes; ++n) {
            checkCancelled();
            if (version2 && ((r.pos - start) & 1)) r.skip(1);
            const unsigned op = version2 ? r.u16() : r.u8();
            if (op == 0xff) return std::move(image);
            opcode(op);
        }
        throw Error("PICTの描画命令数が上限を超えています。");
    }
};

} // namespace

bool probe(const std::uint8_t* data, std::size_t size) noexcept {
    try { return headerAt(data, size, 512) || headerAt(data, size, 0); }
    catch (...) { return false; }
}

Image decode(const std::uint8_t* data, std::size_t size, const Limits& limits,
             const std::function<bool()>& cancelled) {
    return Decoder(data, size, limits, cancelled).run();
}

} // namespace pict
