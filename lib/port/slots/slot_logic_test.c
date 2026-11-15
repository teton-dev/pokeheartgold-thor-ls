// Host check of slot_logic.c against a JP overlay 77 dump: cc -I. slot_logic_test.c slot_logic.c && ./a.out ov77.bin
#include <stdio.h>
#include <stdlib.h>
#include "slot_logic.h"
int main(int argc, char **v) {
    if (argc < 2) return 1; FILE *f = fopen(v[1], "rb"); static uint8_t buf[30000]; uint32_t n = fread(buf, 1, sizeof buf, f);
    SlotTables t = { buf, n }; SlotGame g;
    for (int type = 0; type < 4; type++) {
        int cnt[7] = {0}, cls[3] = {0}; Slot_Init(&g, &t, 12345, type, 100);
        for (int i = 0; i < 200000; i++) { g.flags = 0; Slot_DrawSymbol(&g); cnt[g.symbol]++; cls[g.outcomeClass]++; }
        printf("type %d:", type); for (int s = 0; s < 7; s++) printf(" %.1f", cnt[s] / 2000.0); printf(" | class %d %d %d\n", cls[0], cls[1], cls[2]);
    }
    Slot_Init(&g, &t, 1, 1, 100); int lv[4] = {0}; for (int i = 0; i < 100000; i++) { g.machineId = 3; lv[Slot_BonusRoll(&g)]++; }
    printf("bonus roll: %d %d %d none %d; jackpot %d %d\n", lv[0], lv[1], lv[2], lv[3], g.jackpot[0], g.jackpot[1]);
    Slot_Init(&g, &t, 2, 2, 100); int oc[4] = {0}; for (int i = 0; i < 20000; i++) { g.reach = 1; g.betIndex = 2; Slot_PayoutDecision(&g); oc[g.outcome]++; }
    printf("payout outcomes: %d %d %d none %d balance %d\n", oc[0], oc[1], oc[2], oc[3], g.balance);
    return 0;
}
