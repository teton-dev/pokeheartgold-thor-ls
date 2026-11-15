// Host check: cc slot_assets_test.c slot_assets.c && ./a.out slots-jp-dir
#include <stdio.h>
#include "slot_assets.h"
int main(int argc, char **argv) {
    SlotNarc n;
    if (argc < 2 || !SlotNarc_Open(&n, argv[1], "a/1/2/7")) { puts("open failed"); return 1; }
    printf("%u members\n", n.count);
    unsigned pal[] = {8, 20}, til[] = {9, 21}, scr[] = {10, 22};
    for (int i = 0; i < 2; i++) {
        printf("pal %u: %u B, tiles %u: %u B, screen %u: %u B\n", pal[i], SlotGfx_Palette(SlotNarc_Member(&n, pal[i])).size,
            til[i], SlotGfx_Tiles(SlotNarc_Member(&n, til[i])).size, scr[i], SlotGfx_Screen(SlotNarc_Member(&n, scr[i])).size);
    }
    SlotNarc_Close(&n);
    return 0;
}
