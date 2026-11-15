// The Japanese ROM's import for the Japanese Game Corner: state, thread and folder around slots_rom_core.cpp
#include "port_slots_rom.h"

#include <simulator/sim_carts.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "slots_rom_core.h"

namespace fs = std::filesystem;

namespace {

std::mutex sMutex;
std::string sStatus;
std::string sGameName;
std::atomic<int> sState { SLOTS_ROM_NONE };
std::atomic<bool> sThreadDone { false };
bool sLoaded = false;

void SetStatus(const std::string &text) {
    std::lock_guard<std::mutex> lock(sMutex);
    sStatus = text;
}

std::string Dir() {
    return SIM_SettingsPath("slots-jp");
}

void Load() {
    if (sLoaded) {
        return;
    }
    sLoaded = true;
    std::ifstream info(Dir() + "/rom.txt");
    std::string line;
    while (std::getline(info, line)) {
        if (line.rfind("game=", 0) == 0) {
            sGameName = line.substr(5);
        }
    }
    std::string error;
    if (!sGameName.empty() && SlotsRom::CheckOverlay(Dir() + "/ov77.bin", error)) {
        sState = SLOTS_ROM_READY;
        sStatus = "Ready";
    } else {
        sGameName.clear();
    }
}

void ImportThread(std::string path, bool deleteAfter) {
    std::string game, error;
    SlotsRom::Extract(path, Dir(), game, error, SetStatus);
    if (deleteAfter) {
        std::error_code ec;
        fs::remove(path, ec);
    }
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (error.empty()) {
            sGameName = game;
            sStatus = "Imported " + game + " (Japan)";
        } else {
            sStatus = error;
        }
    }
    sThreadDone = true;
}

} // namespace

extern "C" void SlotsRom_Import(const char *path, BOOL deleteAfter) {
    Load();
    if (sState == SLOTS_ROM_IMPORTING) {
        return;
    }
    sState = SLOTS_ROM_IMPORTING;
    sThreadDone = false;
    SetStatus("Opening the ROM...");
    std::thread(ImportThread, std::string(path), deleteAfter != FALSE).detach();
}

extern "C" void SlotsRom_Update(void) {
    Load();
    if (sState != SLOTS_ROM_IMPORTING || !sThreadDone) {
        return;
    }
    sThreadDone = false;
    std::error_code ec;
    sState = fs::exists(fs::path(Dir()) / "rom.txt", ec) && !sGameName.empty() ? SLOTS_ROM_READY : SLOTS_ROM_FAILED;
}

extern "C" enum SlotsRomState SlotsRom_State(void) {
    Load();
    return (enum SlotsRomState)sState.load();
}

extern "C" const char *SlotsRom_Status(void) {
    static std::string copy;
    std::lock_guard<std::mutex> lock(sMutex);
    copy = sStatus;
    return copy.c_str();
}

extern "C" const char *SlotsRom_GameName(void) {
    Load();
    return sState == SLOTS_ROM_IMPORTING || sGameName.empty() ? nullptr : sGameName.c_str();
}

extern "C" const char *SlotsRom_Dir(void) {
    static std::string dir;
    dir = Dir();
    return dir.c_str();
}

extern "C" BOOL SlotsRom_Remove(void) {
    if (sState == SLOTS_ROM_IMPORTING) {
        return FALSE;
    }
    std::error_code ec;
    fs::remove_all(Dir(), ec);
    sGameName.clear();
    sState = SLOTS_ROM_NONE;
    SetStatus("");
    return !ec;
}
