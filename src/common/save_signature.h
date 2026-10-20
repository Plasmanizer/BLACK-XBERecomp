#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Saves copied from a real Xbox are signed with that console's keys, which the port does not have. The game finds the
// signature wrong and calls the profile damaged. Here every Profile is signed again with the port's keys when the
// port starts, so a save folder can simply be copied into hdd/E/UDATA/45410083/.
namespace save {

enum class Outcome {
    Signed,        // the signature was wrong and has been replaced
    AlreadyValid,  // nothing to do
    NotProfile,    // not 6144 bytes, or not shaped like a Black profile: left alone
    Failed,        // could not back up, write or replace the file: left as it was
};

struct Result {
    std::filesystem::path file;
    Outcome outcome;
};

// the Profile (Europe: Profilo) files below title_dir (UDATA/45410083), one folder level down; each wrong one is saved
// beside itself as <name>.xbox.bak first, unless that copy already exists
std::vector<Result> ResignProfiles(const std::filesystem::path& title_dir);

// the port's save folder, logged; call after the settings load and before the game boots
void ResignSaves();

}  // namespace save
