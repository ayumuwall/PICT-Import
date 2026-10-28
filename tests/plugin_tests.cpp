#include "PITypes.h"
#include "PIFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using Entry = void (*)(int16, FormatRecord*, intptr_t*, int16*);
bool abortRequested = false;
Boolean abortProc() { return abortRequested; }
int progressDone = 0;
void progressProc(int32 done, int32) { progressDone = done; }
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void word(std::vector<unsigned char>& b, int n) { b.push_back((unsigned(n) >> 8) & 255); b.push_back(unsigned(n) & 255); }
void rect(std::vector<unsigned char>& b, int top, int left, int bottom, int right) {
    word(b, top); word(b, left); word(b, bottom); word(b, right);
}
struct Host {
    Entry entry;
    FormatRecord record{};
    intptr_t state = 0;
    Str255 error{};
    XPlatFileSpec fileSpec{};
    int fd;
    std::string path;
    explicit Host(Entry entry, const std::string& filename) : entry(entry), path(filename) {
        fd = open(filename.c_str(), O_RDONLY);
        check(fd >= 0, "テストPICTを開けません。");
        record.posixFileDescriptor = fd;
        record.hostSupportsPOSIXIO = 1;
        record.HostSupports32BitCoordinates = 1;
        record.errorString = &error;
        record.abortProc = abortProc;
        record.progressProc = progressProc;
        for (int i = 0; i < 16; ++i) record.planeMap[i] = i;
    }
    ~Host() {
        if (state) call(formatSelectorReadFinish);
        if (fileSpec.mFileReference) CFRelease(fileSpec.mFileReference);
        close(fd);
    }
    void useFileURL() {
        const auto absolute = std::filesystem::absolute(path).string();
        fileSpec.mFileSpecVersion = kXPlatFileSpecVersion;
        fileSpec.mFileReference = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8*>(absolute.data()), absolute.size(), false);
        check(fileSpec.mFileReference != nullptr, "テスト用のファイルURLを作成できません。");
        record.fileSpecX = &fileSpec;
        record.hostSupportsPluginOpeningFile = true;
        record.hostSupportsPOSIXIO = 0;
        record.pluginUsingPOSIXIO = 0;
        record.posixFileDescriptor = -1;
    }
    int call(int selector) {
        int16 result = 0;
        entry(selector, &record, &state, &result);
        return result;
    }
};
}

int main() {
    void* library = dlopen("./dist/PICT Import.plugin/Contents/MacOS/PICTImport", RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::cerr << "プラグインをロードできません: " << dlerror() << '\n'; return 1; }
    const auto entry = reinterpret_cast<Entry>(dlsym(library, "PluginMain"));
    try {
        check(entry != nullptr, "PluginMainが公開されていません。");
        {
            Host host(entry, "build/fixtures/red-top-v2.pict");
            host.record.posixFileDescriptor = -1;
            check(host.call(formatSelectorReadPrepare) == 0 && host.state == 0,
                  "ファイルがまだ開かれていないPrepareが失敗しました。");
            check(host.record.pluginUsingPOSIXIO == 1, "POSIX I/Oの交渉が行われません。");
            std::cout << "成功: ファイル未設定の準備段階でI/Oを行わない\n";
        }
        {
            Host host(entry, "build/fixtures/red-top-v2.pict");
            host.record.pluginUsingPOSIXIO = 1;
            lseek(host.fd, 7, SEEK_SET);
            check(host.call(formatSelectorFilterFile) == 0, "FilterFileがPICTを識別できません。");
            check(lseek(host.fd, 0, SEEK_CUR) == 7, "FilterFileがファイル位置を書き換えています。");
            check(host.call(formatSelectorReadPrepare) == 0, "ReadPrepareが失敗しました。");
            check(host.call(formatSelectorReadStart) == 0, "ReadStartが失敗しました。");
            check(host.state && host.record.data, "最初の画像バッファがありません。");
            check(host.record.imageSize32.h == 32 && host.record.imageSize32.v == 16, "画像寸法が不正です。");
            check(host.record.depth == 8 && host.record.planes == 3 && host.record.imageMode == plugInModeRGBColor,
                  "画像モードが不正です。");
            check(host.record.colBytes == 4 && host.record.rowBytes == 128 && host.record.planeBytes == 1,
                  "ピクセル配置が不正です。");
            const auto* pixels = static_cast<const unsigned char*>(host.record.data);
            check(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0, "赤色がホストに渡されません。");
            check(pixels[8 * 128 + 1] == 255, "下半分の白色が不正です。");
            check(host.call(formatSelectorReadContinue) == 0 && host.record.data == nullptr, "読み込みが終了しません。");
            check(host.call(formatSelectorReadFinish) == 0 && host.state == 0, "状態が解放されません。");
            check(lseek(host.fd, 0, SEEK_CUR) == 7, "読み込みがファイル位置を書き換えています。");
            std::cout << "成功: 実際のMach-OバンドルをロードしてPICT識別・読み込み・終了\n";
        }
        {
            std::vector<unsigned char> tall(512, 0);
            word(tall, 0); rect(tall, 0, 0, 150, 4); tall.insert(tall.end(), {0x11, 1, 0x31});
            rect(tall, 0, 0, 150, 4); tall.push_back(0xff);
            std::ofstream out("build/fixtures/tall-v1.pict", std::ios::binary);
            out.write(reinterpret_cast<const char*>(tall.data()), tall.size()); out.close();
            Host host(entry, "build/fixtures/tall-v1.pict");
            host.record.pluginUsingPOSIXIO = 1;
            check(host.call(formatSelectorReadStart) == 0 && host.record.theRect32.bottom == 64, "最初の64行が不正です。");
            check(host.call(formatSelectorReadContinue) == 0 && host.record.theRect32.top == 64 && host.record.theRect32.bottom == 128,
                  "2つ目のチャンクが不正です。");
            check(host.call(formatSelectorReadContinue) == 0 && host.record.theRect32.top == 128 && host.record.theRect32.bottom == 150,
                  "最後のチャンクが不正です。");
            check(host.call(formatSelectorReadContinue) == 0 && !host.record.data && progressDone == 150,
                  "終了または進捗が不正です。");
            std::cout << "成功: 複数チャンク・進捗コールバック\n";
        }
        {
            Host host(entry, "build/fixtures/red-top-v2.pict");
            host.record.pluginUsingPOSIXIO = 1;
            check(host.call(formatSelectorReadStart) == 0, "キャンセル前の読み込みが失敗しました。");
            abortRequested = true;
            check(host.call(formatSelectorReadContinue) == userCanceledErr && !host.state && !host.record.data,
                  "キャンセル後の状態が不正です。");
            abortRequested = false;
            check(host.call(formatSelectorReadFinish) == 0, "キャンセル後の終了処理が失敗しました。");
            std::cout << "成功: キャンセル・二重終了・メモリ解放\n";
        }
        {
            std::ofstream invalid("build/fixtures/invalid.pict", std::ios::binary);
            invalid << "not a PICT"; invalid.close();
            Host host(entry, "build/fixtures/invalid.pict");
            host.useFileURL();
            check(host.call(formatSelectorFilterFile) == formatCannotRead, "別形式が受理されました。");
            check(host.call(formatSelectorReadStart) == errReportString && !host.state && host.error[0] > 0,
                  "エラーメッセージが返りません。");
            check(host.call(formatSelectorWriteStart) == formatCannotRead, "書き出しが有効になっています。");
            std::cout << "成功: 形式不一致・エラー文字列・読み込み専用\n";
        }
        {
            Host host(entry, "build/fixtures/quicktime-jpeg.pict");
            host.useFileURL();
            check(host.call(formatSelectorReadStart) == 0, "QuickTime JPEGの読み込みが失敗しました。");
            check(host.record.imageSize32.h == 16 && host.record.imageSize32.v == 40 && host.record.data,
                  "QuickTime JPEGの画像寸法・バッファが不正です。");
            const auto* pixels = static_cast<const unsigned char*>(host.record.data);
            const auto top = pixels + 4 * host.record.rowBytes + 4 * host.record.colBytes;
            const auto bottom = pixels + 20 * host.record.rowBytes + 4 * host.record.colBytes;
            check(top[0] > 230 && top[1] < 20 && top[2] < 20 && bottom[0] < 20 && bottom[2] > 230,
                  "QuickTime JPEGの分割画像がホストへ正しく渡されません。");
            check(host.call(formatSelectorReadContinue) == 0 && !host.record.data,
                  "QuickTime JPEGの読み込みが終了しません。");
            std::cout << "成功: QuickTime JPEGの分割画像をCFURLから読み込み、ホストへ転送\n";
        }
        {
            const std::string filename = "build/fixtures/日本語の画像.pct";
            std::filesystem::copy_file("build/fixtures/red-top-v2.pict", filename,
                                      std::filesystem::copy_options::overwrite_existing);
            Host host(entry, filename);
            host.useFileURL();
            host.record.HostSupports32BitCoordinates = 0;
            check(host.call(formatSelectorFilterFile) == 0, "CFURLからの形式識別が失敗しました。");
            check(host.call(formatSelectorReadPrepare) == 0, "CFURL使用時のPrepareが失敗しました。");
            check(host.call(formatSelectorReadStart) == 0, "CFURLからの読み込みが失敗しました。");
            check(host.record.PluginUsing32BitCoordinates == 0 && host.record.imageSize.h == 32,
                  "16ビット座標での互換動作が不正です。");
            const auto* pixels = static_cast<const unsigned char*>(host.record.data);
            check(pixels && pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0,
                  "CFURLから読み込んだ画素が不正です。");
            check(host.call(formatSelectorReadContinue) == 0 && !host.record.data, "CFURLの読み込みが終了しません。");
            check(host.call(formatSelectorReadFinish) == 0 && !host.state, "CFURLの状態が解放されません。");
            check(fcntl(host.fd, F_GETFD) >= 0, "ホストのファイルを閉じてしまいました。");
            std::cout << "成功: 日本語のファイルURL・ホスト記述子なし・16ビット座標\n";
        }
        if (const char* directory = std::getenv("PICT_TEST_SAMPLE_DIR")) {
            for (const auto& sample : std::filesystem::directory_iterator(directory)) {
                auto extension = sample.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (extension != ".pict" && extension != ".pct" && extension != ".pic") continue;
                const auto filename = sample.path().string();
                Host host(entry, filename);
                host.useFileURL();
                check(host.call(formatSelectorFilterFile) == 0, "実ファイルの形式識別が失敗しました。");
                check(host.call(formatSelectorReadPrepare) == 0, "実ファイルのPrepareが失敗しました。");
                check(host.call(formatSelectorReadStart) == 0, "実ファイルの読み込みが失敗しました。");
                const int width = host.record.imageSize32.h, height = host.record.imageSize32.v;
                check(width > 0 && height > 0, "実ファイルの寸法が不正です。");
                std::vector<unsigned char> rgba(std::size_t(width) * height * 4, 255);
                int rows = 0;
                while (host.record.data) {
                    const auto* bytes = static_cast<const unsigned char*>(host.record.data);
                    for (int y = host.record.theRect32.top; y < host.record.theRect32.bottom; ++y) {
                        for (int x = 0; x < width; ++x)
                            for (int c = 0; c < 3; ++c)
                                rgba[(std::size_t(y) * width + x) * 4 + c] = bytes[(y - host.record.theRect32.top) * host.record.rowBytes + x * host.record.colBytes + c];
                        ++rows;
                    }
                    check(host.call(formatSelectorReadContinue) == 0, "実ファイルのチャンク転送が失敗しました。");
                }
                check(rows == height, "実ファイルの全行が転送されません。");
                if (const char* referenceDirectory = std::getenv("PICT_TEST_REFERENCE_DIR")) {
                    std::ifstream reference(std::filesystem::path(referenceDirectory) / (sample.path().stem().string() + ".rgba"), std::ios::binary);
                    check(reference.good(), "参照画像がありません。");
                    const std::vector<unsigned char> expected{std::istreambuf_iterator<char>(reference), std::istreambuf_iterator<char>()};
                    check(rgba == expected, "ホストへ渡すRGBAが参照デコーダーと一致しません。");
                }
                std::cout << "成功: 実ファイル" << sample.path().filename().string()
                          << "をCFURLで読み込み、" << rows << "行をホストへ転送\n";
            }
        }
        dlclose(library);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "失敗: " << error.what() << '\n';
        dlclose(library);
        return 1;
    }
}
