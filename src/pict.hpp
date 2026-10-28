#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

namespace pict {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Image {
    int width = 0;
    int height = 0;
    double horizontalResolution = 72;
    double verticalResolution = 72;
    std::vector<std::uint8_t> rgba;
};

struct Limits {
    std::size_t maxPixels = 32 * 1024 * 1024;
    std::size_t maxFileBytes = 256 * 1024 * 1024;
    std::size_t maxOpcodes = 1000000;
};

// ファイルの512バイトヘッダーあり／なしを識別する。全体の妥当性はdecodeで検査する。
bool probe(const std::uint8_t* data, std::size_t size) noexcept;
Image decode(const std::uint8_t* data, std::size_t size,
             const Limits& limits = {}, const std::function<bool()>& cancelled = {});

} // namespace pict
