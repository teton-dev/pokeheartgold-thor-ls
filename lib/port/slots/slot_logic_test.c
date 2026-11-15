// Host check of slot_logic.c against a JP overlay 77 dump: cc -I. slot_logic_test.c slot_logic.c && ./a.out ov77.bin
// Plays spins with random stop presses and counts what the draw asked for against what the reels showed.
#include <stdio.h>
#include <stdlib.h>
#include "slot_logic.h"
int main(int argc, char **v) {
    if (argc < 2) return 1;
    FILE *f = fopen(v[1], "rb"); static uint8_t buf[30000]; uint32_t n = fread(buf, 1, sizeof buf, f);
    SlotTables t = { buf, n }; SlotGame g;
    for (int type = 0; type < 4; type++) {
        int cnt[7] = {0}, cls[3] = {0}; Slot_Init(&g, &t, 12345, type, 100);
        for (int i = 0; i < 200000; i++) { g.flags = 0; Slot_DrawSymbol(&g); cnt[g.symbol]++; cls[g.outcomeClass]++; }
        printf("type %d:", type); for (int s = 0; s < 7; s++) printf(" %.1f", cnt[s] / 2000.0); printf(" | class %d %d %d\n", cls[0], cls[1], cls[2]);
    }
    // Spins: the draw, three presses at random places, the result
    int asked[7] = {0}, got[7][7] = {{0}};
    long paid = 0, bet = 0;
    Slot_Init(&g, &t, 99, 0, 50000);
    srand(1);
    for (int i = 0; i < 100000; i++) {
        g.flags = 0;
        Slot_DrawSymbol(&g);
        g.reelStopped[0] = g.reelStopped[1] = g.reelStopped[2] = 0;
        for (int r = 0; r < 3; r++) Slot_ChooseStop(&g, r, rand() % SLOT_REEL_LENGTH);
        int line, won = Slot_Judge(&g, &line);
        asked[g.symbol]++; got[g.symbol][won]++;
        bet += 3; if (won > 1) paid += Slot_WinPayout(&g, won);
    }
    for (int s = 0; s < 7; s++) {
        printf("drawn %d (%6d):", s, asked[s]);
        for (int w = 0; w < 7; w++) printf(" %6d", got[s][w]);
        printf("\n");
    }
    printf("return: %.1f%% (paid %ld for %ld bet; replays not counted as free)\n", 100.0 * paid / bet, paid, bet);
    return 0;
}
