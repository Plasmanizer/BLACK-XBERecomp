#include "common/save_signature.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include "app/app.h"
#include "core/log.h"
#include "kernel/crypto.h"

namespace save {

namespace {

namespace fs = std::filesystem;

constexpr size_t kProfileSize = 6144;
constexpr size_t kSigSize = 20;
constexpr size_t kSignedSize = kProfileSize - kSigSize;  // the signature covers everything before it
constexpr size_t kBlocks = 3;
constexpr size_t kBlockSize = 0x778;  // the size word at +4 of each of the three profile blocks

// the Xbox kernel's XboxHDKey and XboxSignatureKey (runtime/kernel/xboxkrnl.cpp): never set, so all zero
constexpr uint8_t kHdKey[16] = {};
constexpr uint8_t kSigKey[16] = {};

uint32_t Word(const std::vector<uint8_t>& d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

// the size of a Black Profile and the block size word in all three blocks
bool LooksLikeProfile(const std::vector<uint8_t>& d) {
    if (d.size() != kProfileSize) return false;
    for (size_t b = 0; b < kBlocks; b++)
        if (Word(d, b * kBlockSize + 4) != kBlockSize) return false;
    return true;
}

// what the game's XCalculateSignature gives: the HD-key HMAC of the signature-key HMAC of the data
void Sign(const std::vector<uint8_t>& d, uint8_t out[kSigSize]) {
    uint8_t inner[kSigSize];
    crypto::Hmac(kSigKey, sizeof kSigKey, d.data(), kSignedSize, nullptr, 0, inner);
    crypto::Hmac(kHdKey, sizeof kHdKey, inner, sizeof inner, nullptr, 0, out);
}

bool ReadAll(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return !f.bad();
}

bool WriteAll(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    f.flush();
    return bool(f);
}

bool IsProfileName(const fs::path& name) {
    std::string s = name.string();
    if (s.size() != 7) return false;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s == "profile";
}

Outcome ResignOne(const fs::path& file) {
    std::vector<uint8_t> data;
    if (!ReadAll(file, data) || !LooksLikeProfile(data)) return Outcome::NotProfile;

    uint8_t want[kSigSize];
    Sign(data, want);
    if (std::memcmp(want, data.data() + kSignedSize, kSigSize) == 0) return Outcome::AlreadyValid;

    // the original, kept once: the first copy is the one that came from the Xbox
    fs::path backup = file;
    backup += ".xbox.bak";
    std::error_code ec;
    if (!fs::exists(backup, ec)) {
        fs::copy_file(file, backup, fs::copy_options::none, ec);
        if (ec) return Outcome::Failed;
    }

    // written beside the file and moved over it, so a failed write cannot leave half a profile
    std::memcpy(data.data() + kSignedSize, want, kSigSize);
    fs::path temp = file;
    temp += ".new";
    if (!WriteAll(temp, data)) {
        fs::remove(temp, ec);
        return Outcome::Failed;
    }
    fs::rename(temp, file, ec);  // replaces the file, on Windows too
    if (ec) {
        fs::remove(temp, ec);
        return Outcome::Failed;
    }
    return Outcome::Signed;
}

}  // namespace

std::vector<Result> ResignProfiles(const fs::path& title_dir) {
    std::vector<Result> results;
    std::error_code ec;
    if (!fs::is_directory(title_dir, ec)) return results;

    std::vector<fs::path> folders;
    for (fs::directory_iterator it(title_dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) folders.push_back(it->path());
    std::sort(folders.begin(), folders.end());

    for (const fs::path& folder : folders) {
        std::vector<fs::path> files;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && IsProfileName(it->path().filename())) files.push_back(it->path());
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) results.push_back({file, ResignOne(file)});
    }
    return results;
}

void ResignSaves() {
    std::string hdd = app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd";
    fs::path title_dir = app::ExeDir() / hdd / "E" / "UDATA" / "45410083";
    for (const Result& r : ResignProfiles(title_dir)) {
        std::string name = (r.file.parent_path().filename() / r.file.filename()).string();
        switch (r.outcome) {
            case Outcome::Signed:
                LOGR("Save %s: signature from another console, signed for this one (original kept as Profile.xbox.bak)",
                     name.c_str());
                break;
            case Outcome::Failed: LOGE("Save %s: signature is wrong and could not be replaced", name.c_str()); break;
            case Outcome::AlreadyValid:
            case Outcome::NotProfile: break;
        }
    }
}

}  // namespace save
