#include "PITypes.h"
#include "PIFormat.h"
#include "PIAbout.h"
#include "pict.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <memory>
#include <new>
#include <os/log.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct ReadState {
    pict::Image image;
    int nextRow = 0;
};

void clear(FormatRecord& record, intptr_t& data) noexcept {
    record.data = nullptr;
    delete reinterpret_cast<ReadState*>(data);
    data = 0;
}

class InputFile {
    int descriptor = -1;
    bool owned = false;
public:
    explicit InputFile(const FormatRecord& record) {
        // ファイル識別・準備段階ではホストのPOSIX記述子が未設定の場合がある。
        // SDKで渡されるCFURLから読み取り専用で開き、ホストの記述子を閉じない。
        if (record.fileSpecX && record.fileSpecX->mFileSpecVersion == kXPlatFileSpecVersion &&
            record.fileSpecX->mFileReference) {
            const auto url = record.fileSpecX->mFileReference;
            const auto scheme = CFURLCopyScheme(url);
            const bool isFile = scheme && CFStringCompare(scheme, CFSTR("file"), 0) == kCFCompareEqualTo;
            if (scheme) CFRelease(scheme);
            std::array<UInt8, PATH_MAX> path{};
            if (!isFile || !CFURLGetFileSystemRepresentation(url, true, path.data(), path.size()))
                throw pict::Error("Photoshopから渡されたファイルURLをローカルパスへ変換できません。");
            descriptor = open(reinterpret_cast<const char*>(path.data()), O_RDONLY | O_CLOEXEC);
            if (descriptor < 0) throw pict::Error("Photoshopが指定したPICTファイルを読み取り専用で開けません。");
            owned = true;
            return;
        }
        // ホストが開閉をプラグインへ任せる場合、記述子の各フィールドは未定義。
        if (!record.hostSupportsPluginOpeningFile && record.hostSupportsPOSIXIO &&
            record.pluginUsingPOSIXIO && record.posixFileDescriptor >= 0) {
            descriptor = record.posixFileDescriptor;
            return;
        }
        throw pict::Error("Photoshopから有効なファイルURLまたは交渉済みのファイル記述子が渡されませんでした。");
    }
    ~InputFile() { if (owned) close(descriptor); }
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;
    int get() const { return descriptor; }
};

os_log_t readLog() {
    static const auto log = os_log_create("jp.pictimport.photoshop", "read");
    return log;
}

struct CallTrace {
    int selector;
    const int16* result;
    ~CallTrace() {
        os_log_with_type(readLog(), OS_LOG_TYPE_DEFAULT, "PICT Import 0.1.3 selector=%{public}d result=%{public}d", selector, *result);
    }
};

std::vector<std::uint8_t> readFile(FormatRecord& record) {
    const InputFile source(record);
    const int fd = source.get();
    struct stat info{};
    if (fstat(fd, &info) != 0) throw pict::Error("PICTファイルのサイズを取得できません。");
    if (!S_ISREG(info.st_mode)) throw pict::Error("PICTの入力が通常のファイルではありません。");
    if (info.st_size < 0 || std::uint64_t(info.st_size) > pict::Limits{}.maxFileBytes)
        throw pict::Error("PICTファイルが256 MiBの上限を超えています。");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t read = 0;
    while (read < bytes.size()) {
        if (record.abortProc && record.abortProc()) throw pict::Error("読み込みをキャンセルしました。");
        const auto n = pread(fd, bytes.data() + read, std::min<std::size_t>(bytes.size() - read, 1024 * 1024), read);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw pict::Error("PICTファイルを最後まで読み込めません。");
        read += std::size_t(n);
    }
    return bytes;
}

void nextChunk(FormatRecord& record, ReadState& state) {
    if (record.abortProc && record.abortProc()) throw pict::Error("読み込みをキャンセルしました。");
    if (state.nextRow == state.image.height) {
        record.data = nullptr;
        return;
    }
    const int top = state.nextRow;
    const int bottom = std::min(top + 64, state.image.height);
    record.theRect32 = {top, 0, bottom, state.image.width};
    record.theRect = {static_cast<int16>(top), 0, static_cast<int16>(bottom), static_cast<int16>(state.image.width)};
    record.loPlane = 0; record.hiPlane = 2;
    record.colBytes = 4; record.planeBytes = 1;
    record.rowBytes = state.image.width * 4;
    record.data = state.image.rgba.data() + std::size_t(top) * record.rowBytes;
    state.nextRow = bottom;
}

void reportError(FormatRecord& record, short& result, const std::string& message) noexcept {
    if (!record.errorString) { result = formatCannotRead; return; }
    auto n = std::min<std::size_t>(message.size(), 255);
    // UTF-8の途中でエラーメッセージを切らない。
    while (n < message.size() && n > 0 && (static_cast<unsigned char>(message[n]) & 0xc0) == 0x80) --n;
    (*record.errorString)[0] = static_cast<unsigned char>(n);
    std::memcpy(&(*record.errorString)[1], message.data(), n);
    result = errReportString;
}

} // namespace

extern "C" __attribute__((visibility("default"))) void PluginMain(
    const int16 selector, FormatRecord* record, intptr_t* data, int16* result) noexcept {
    if (!result) return;
    *result = noErr;
    if (selector == formatSelectorAbout) {
        CFUserNotificationDisplayAlert(0, kCFUserNotificationNoteAlertLevel, nullptr, nullptr, nullptr,
            CFSTR("PICT Import 0.1.3"),
            CFSTR("Macintosh PICT 1／2をRGB画像として読み込みます。\n対応範囲・制限事項は付属のREADMEをご覧ください。"),
            CFSTR("OK"), nullptr, nullptr, nullptr);
        return;
    }
    if (!record || !data) { *result = paramErr; return; }
    const CallTrace trace{selector, result};
    try {
        switch (selector) {
            case formatSelectorFilterFile: {
                const InputFile source(*record);
                std::array<std::uint8_t, 552> header{};
                ssize_t n;
                do { n = pread(source.get(), header.data(), header.size(), 0); } while (n < 0 && errno == EINTR);
                *result = n >= 0 && pict::probe(header.data(), std::size_t(n)) ? noErr : formatCannotRead;
                return;
            }
            case formatSelectorReadPrepare:
                record->maxData = 0;
                // PrepareではメモリとI/O方式だけを交渉し、ファイルは参照しない。
                record->pluginUsingPOSIXIO = record->hostSupportsPOSIXIO ? 1 : 0;
                return;
            case formatSelectorReadStart: {
                clear(*record, *data);
                auto state = std::make_unique<ReadState>();
                const auto bytes = readFile(*record);
                state->image = pict::decode(bytes.data(), bytes.size(), {}, [&]() {
                    return record->abortProc && record->abortProc();
                });
                record->PluginUsing32BitCoordinates = record->HostSupports32BitCoordinates ? 1 : 0;
                record->imageMode = plugInModeRGBColor;
                record->imageSize32 = {state->image.height, state->image.width};
                record->imageSize = {static_cast<int16>(state->image.height), static_cast<int16>(state->image.width)};
                record->depth = 8; record->planes = 3;
                record->imageHRes = static_cast<Fixed>(std::round(state->image.horizontalResolution * 65536));
                record->imageVRes = static_cast<Fixed>(std::round(state->image.verticalResolution * 65536));
                record->transparencyPlane = 0;
                record->transparencyMatting = 0;
                record->imageRsrcSize = 0; record->imageRsrcData = nullptr;
                nextChunk(*record, *state);
                *data = reinterpret_cast<intptr_t>(state.release());
                return;
            }
            case formatSelectorReadContinue: {
                auto* state = reinterpret_cast<ReadState*>(*data);
                if (!state) throw pict::Error("PICTの読み込み状態がありません。");
                if (record->progressProc) record->progressProc(state->nextRow, state->image.height);
                nextChunk(*record, *state);
                return;
            }
            case formatSelectorReadFinish:
                clear(*record, *data);
                return;
            default:
                *result = formatCannotRead;
                return;
        }
    } catch (const std::bad_alloc&) {
        if (selector != formatSelectorFilterFile) clear(*record, *data);
        *result = memFullErr;
    } catch (const std::exception& error) {
        os_log_error(readLog(), "読み込み失敗 selector=%{public}d: %{public}s", selector, error.what());
        if (selector == formatSelectorFilterFile) { *result = formatCannotRead; return; }
        clear(*record, *data);
        if (record->abortProc && record->abortProc()) *result = userCanceledErr;
        else reportError(*record, *result, error.what());
    } catch (...) {
        if (selector != formatSelectorFilterFile) clear(*record, *data);
        *result = formatCannotRead;
    }
}
