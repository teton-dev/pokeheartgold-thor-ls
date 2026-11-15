#ifndef POKEHEARTGOLD_PORT_SLOTS_ROM_CORE_H
#define POKEHEARTGOLD_PORT_SLOTS_ROM_CORE_H

// The Japanese HeartGold/SoulSilver ROM's Game Corner files, pulled out of a ROM (standard C++ only, so it can be
// tested on the host): the slot machine's graphics (a/1/2/7 and a/1/5/5) and its overlay 77 (the code the odds and
// payout tables are read from, decompressed). Nothing of the ROM is kept in the repository.

#include <functional>
#include <string>

namespace SlotsRom {

// Extracts into outDir (a/1/2/7, a/1/5/5, ov77.bin, rom.txt). game: "HeartGold" or "SoulSilver" on success.
// progress is called with a short status line.
bool Extract(const std::string &romPath, const std::string &outDir, std::string &game, std::string &error,
    const std::function<void(const std::string &)> &progress);

// The overlay's tables sum to what the rules need (each odds row to 1000): the self-check of an extracted ov77.bin
bool CheckOverlay(const std::string &path, std::string &error);

} // namespace SlotsRom

#endif
