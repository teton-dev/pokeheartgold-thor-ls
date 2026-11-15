// Host check: c++ -std=c++17 -I. slots_rom_core.cpp slots_rom_test.cpp && ./a.out HEARTGOLD_JP.nds outdir
#include <cstdio>
#include "slots_rom_core.h"
int main(int argc, char **argv) {
    if (argc < 3) return 1;
    std::string game, error;
    bool ok = SlotsRom::Extract(argv[1], argv[2], game, error, [](const std::string &s) { puts(s.c_str()); });
    printf("%s %s %s\n", ok ? "ok" : "FAILED", game.c_str(), error.c_str());
    return ok ? 0 : 1;
}
