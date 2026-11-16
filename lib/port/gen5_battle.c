// Gen 5 battles in HGSS; see include/port_sdk/port_gen5_battle.h. The Platinum fork's src/port/gen5_battle.c on
// HGSS's BattleSetup: the battle as the Gen 5 code takes it (gen5/api/gen5.h), and its results written back as HGSS's
// own battle leaves them in the BattleSetup.
#include "port_gen5_battle.h"
#include "port_gen5_battle_ui.h"

#ifdef GEN5_BATTLES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simulator/sim.h>

#include "constants/balls.h"
#include "constants/battle.h"
#include "constants/items.h"
#include "constants/sndseq.h"
#include "constants/species.h"
#include "constants/trainer_class.h"
#include "constants/trainers.h"

#include "msgdata/msg.naix"

#include "bag.h"
#include "blackout.h"
#include "encounter.h"
#include "battle/battle_system.h"
#include "evolution_scene.h"
#include "field_system.h"
#include "sys_flags.h"
#include "save_local_field_data.h"
#include "player_avatar.h"
#include "field/encounter_check.h"
#include "filesystem.h"
#include "game_stats.h"
#include "gen5.h"
#include "gf_rtc.h"
#include "item.h"
#include "map_header.h"
#include "msgdata.h"
#include "naming_screen.h"
#include "options.h"
#include "party.h"
#include "pm_string.h"
#include "player_data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "port_game_settings.h"
#include "port_gen5_rom.h"
#include "port_gen5_rocket.h"
#include "port_nuzlocke_mode.h"
#include "port_test_script.h"
#include "sound.h"
#include "system.h"
#include "task.h"
#include "trainer_data.h"

static BOOL sRequested = FALSE; // The next battle runs in BW2's engine
static BOOL sShown = FALSE; // BW2's battle is on screen
static BOOL sFailed = FALSE; // The battle couldn't start: back as if the player ran
static Gen5Battle sBattle; // The battle as handed to the Gen 5 code, and as it comes back
static Gen5Item sItemCountsBefore[GEN5_ITEMS_MAX];
static int sItemsBefore;

BOOL Gen5Battle_IsShown(void) {
    return sShown;
}

BOOL Gen5Battle_MoveBgShown(void) {
    return sShown && Gen5_MoveBgShown();
}

void Gen5Battle_RequestNext(void) {
    sRequested = TRUE;
}

BOOL Gen5Battle_Wants(const BattleSetup *setup) {
    if (sRequested) {
        sRequested = FALSE;
        return TRUE;
    }
    if (!GameSettings_Get()->gen5Battles || Gen5Rom_GameName() == NULL) {
        return FALSE;
    }
    // Wild battles alone (roaming Pokémon too), and the Safari Zone's (none of the player's Pokémon on the field: the
    // view plays them, gen5/src/safari.c, by the same rules as HGSS's). The Bug-Catching Contest, Pal Park, the
    // tutorial, link and Frontier battles stay HGSS's
    const u32 type = setup->battleType;
    if ((type & ~BATTLE_TYPE_ROAMER) == 0 || type == BATTLE_TYPE_SAFARI) {
        return TRUE;
    }
    // Trainers: single, double (one trainer's two Pokémon), against two trainers (TAG), and beside a partner whom
    // BW2's AI plays (MULTI with AI). The story's trainers (Gym Leaders, Silver, Team Rocket...) are started by their
    // scripts as any trainer is; Silver's first battle has BATTLE_SPECIAL_FIRST_RIVAL (losing it goes on with the story,
    // which the script decides from the result)
    // BATTLE_TYPE_11 is a battle the player may lose and go on (the party healed after: encounter.c)
    const u32 trainerTypes = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLES | BATTLE_TYPE_TAG | BATTLE_TYPE_MULTI | BATTLE_TYPE_AI
        | BATTLE_TYPE_11;
    return (type & BATTLE_TYPE_TRAINER) && (type & ~trainerTypes) == 0
        && (setup->battleSpecial & ~(BATTLE_SPECIAL_FIRST_RIVAL | BATTLE_SPECIAL_NO_RUNNING)) == 0;
}

// --- Text -------------------------------------------------------------------------------------------------------

// HGSS's text (the Gen 4 character table, charmap.txt) to Unicode, for BW2: letters, digits, the usual punctuation and
// the line and page breaks as BW2 writes them. Other characters are left out for now
static unsigned short CharToUnicode(u16 c) {
    if (c >= 0x121 && c <= 0x12A) {
        return '0' + (c - 0x121);
    }
    if (c >= 0x12B && c <= 0x144) {
        return 'A' + (c - 0x12B);
    }
    if (c >= 0x145 && c <= 0x15E) {
        return 'a' + (c - 0x145);
    }
    switch (c) {
    case 0x1DE:
        return ' ';
    case 0x1AB:
        return '!';
    case 0x1AC:
        return '?';
    case 0x1AD:
        return ',';
    case 0x1AE:
        return '.';
    case 0x1AF:
        return 0x2026; // …
    case 0x1B1:
        return '/';
    case 0x1B3:
        return '\'';
    case 0x1B9:
        return '(';
    case 0x1BA:
        return ')';
    case 0x1BB:
        return 0x2642; // ♂
    case 0x1BC:
        return 0x2640; // ♀
    case 0x1BE:
        return '-';
    case 0x1C4:
        return ':';
    case 0x1C5:
        return ';';
    case 0x188:
        return 0xE9; // é
    }
    return 0;
}

static void TextToGen5(const u16 *src, int srcLength, unsigned short *dest, int destLength) {
    int n = 0;
    for (int i = 0; i < srcLength && src[i] != EOS && n < destLength - 1; i++) {
        if (src[i] == 0xE000) { // A new line
            dest[n++] = 0xFFFE;
            continue;
        }
        if (src[i] == 0x25BC || src[i] == 0x25BD) { // A scroll or a new page: BW2's stream commands
            if (n + 3 > destLength - 1) {
                break;
            }
            dest[n++] = 0xF000;
            dest[n++] = 0xBE00 | (src[i] == 0x25BD);
            dest[n++] = 0;
            continue;
        }
        const unsigned short c = CharToUnicode(src[i]);
        if (c != 0) {
            dest[n++] = c;
        }
    }
    dest[n] = 0xFFFF;
}

// --- Pokémon ----------------------------------------------------------------------------------------------------

static unsigned StatusToGen5(u32 status) {
    if (status & MON_STATUS_SLP_MASK) {
        return GEN5_STATUS_SLEEP;
    }
    if (status & (MON_STATUS_PSN_MASK | MON_STATUS_TOX_MASK)) {
        return GEN5_STATUS_POISON;
    }
    if (status & MON_STATUS_BRN_MASK) {
        return GEN5_STATUS_BURN;
    }
    if (status & MON_STATUS_FRZ_MASK) {
        return GEN5_STATUS_FREEZE;
    }
    if (status & MON_STATUS_PRZ_MASK) {
        return GEN5_STATUS_PARALYSIS;
    }
    return GEN5_STATUS_NONE;
}

// BW2 keeps no sleep counter or toxic flag outside a battle: a Pokémon asleep sleeps a few turns more, and one that
// was badly poisoned stays so if it is still poisoned
static u32 StatusFromGen5(unsigned status, u32 before) {
    switch (status) {
    case GEN5_STATUS_SLEEP:
        return (before & MON_STATUS_SLP_MASK) ? (before & MON_STATUS_SLP_MASK) : 3;
    case GEN5_STATUS_POISON:
        return (before & MON_STATUS_TOX_MASK) ? MON_STATUS_TOX_MASK : MON_STATUS_PSN_MASK;
    case GEN5_STATUS_BURN:
        return MON_STATUS_BRN_MASK;
    case GEN5_STATUS_FREEZE:
        return MON_STATUS_FRZ_MASK;
    case GEN5_STATUS_PARALYSIS:
        return MON_STATUS_PRZ_MASK;
    default:
        return 0;
    }
}

// Species, forms, moves, items, abilities, natures and balls have the same numbers in Gen 4 and Gen 5
static void PokemonToGen5(Pokemon *mon, Gen5Pokemon *dest) {
    u16 name[POKEMON_NAME_LENGTH + 1];
    u16 otName[PLAYER_NAME_LENGTH + 1];

    memset(dest, 0, sizeof(*dest));
    dest->species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    dest->form = GetMonData(mon, MON_DATA_FORM, NULL);
    dest->level = GetMonData(mon, MON_DATA_LEVEL, NULL);
    dest->exp = GetMonData(mon, MON_DATA_EXPERIENCE, NULL);
    dest->personality = GetMonData(mon, MON_DATA_PERSONALITY, NULL);
    dest->otId = GetMonData(mon, MON_DATA_OT_ID, NULL);
    dest->heldItem = GetMonData(mon, MON_DATA_HELD_ITEM, NULL);
    dest->ability = GetMonData(mon, MON_DATA_ABILITY, NULL);
    dest->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP, NULL);
    dest->gender = GetMonGender(mon);
    for (int i = 0; i < 4; i++) {
        dest->moves[i] = GetMonData(mon, MON_DATA_MOVE1 + i, NULL);
        dest->pp[i] = GetMonData(mon, MON_DATA_MOVE1_PP + i, NULL);
        dest->ppUps[i] = GetMonData(mon, MON_DATA_MOVE1_PP_UPS + i, NULL);
    }
    dest->hp = GetMonData(mon, MON_DATA_HP, NULL);
    dest->status = StatusToGen5(GetMonData(mon, MON_DATA_STATUS, NULL));
    for (int i = 0; i < 6; i++) {
        dest->ivs[i] = GetMonData(mon, MON_DATA_HP_IV + i, NULL);
        dest->evs[i] = GetMonData(mon, MON_DATA_HP_EV + i, NULL);
    }
    dest->ball = GetMonData(mon, MON_DATA_POKEBALL, NULL);
    dest->pokerus = GetMonData(mon, MON_DATA_POKERUS, NULL);
    dest->metLevel = GetMonData(mon, MON_DATA_MET_LEVEL, NULL);
    dest->otGender = GetMonData(mon, MON_DATA_OT_GENDER, NULL);
    dest->isEgg = GetMonData(mon, MON_DATA_IS_EGG, NULL);
    dest->hasNickname = GetMonData(mon, MON_DATA_HAS_NICKNAME, NULL);
#ifdef SDK_PORT
    dest->dead = NuzlockeMode_IsDead(mon);
#endif
    GetMonData(mon, MON_DATA_NICKNAME, name);
    TextToGen5(name, POKEMON_NAME_LENGTH + 1, dest->nickname, GEN5_NAME_LENGTH);
    GetMonData(mon, MON_DATA_OT_NAME, otName);
    TextToGen5(otName, PLAYER_NAME_LENGTH + 1, dest->otName, 8);
}

// What the battle changed. TRUE if the Pokémon gained a level
static BOOL PokemonFromGen5(Pokemon *mon, const Gen5Pokemon *src) {
    const u32 level = GetMonData(mon, MON_DATA_LEVEL, NULL);
    const u32 status = GetMonData(mon, MON_DATA_STATUS, NULL);
    u32 value;

    SetMonData(mon, MON_DATA_EXPERIENCE, &src->exp);
    for (int i = 0; i < 4; i++) {
        SetMonData(mon, MON_DATA_MOVE1 + i, &src->moves[i]);
        SetMonData(mon, MON_DATA_MOVE1_PP_UPS + i, &src->ppUps[i]);
        SetMonData(mon, MON_DATA_MOVE1_PP + i, &src->pp[i]);
    }
    for (int i = 0; i < 6; i++) {
        value = src->evs[i];
        SetMonData(mon, MON_DATA_HP_EV + i, &value);
    }
    SetMonData(mon, MON_DATA_FRIENDSHIP, &src->friendship);
    SetMonData(mon, MON_DATA_HELD_ITEM, &src->heldItem);
    SetMonData(mon, MON_DATA_POKERUS, &src->pokerus);
    CalcMonLevelAndStats(mon);
    SetMonData(mon, MON_DATA_HP, &src->hp);
    value = StatusFromGen5(src->status, status);
    SetMonData(mon, MON_DATA_STATUS, &value);
    return GetMonData(mon, MON_DATA_LEVEL, NULL) > level;
}

// --- Trainers ---------------------------------------------------------------------------------------------------

// The classes BW2 draws itself (gen5/src/trainers.c, HGSS's table): the same class or the same person (Johto's and
// Kanto's Gym Leaders, Giovanni, Blue, Lance, Red from BW2's Pokémon World Tournament). Every other class (Team Rocket,
// Silver, the Elite Four, Kimono Girls, Sages, Ethan and Lyra...) has HGSS's own sprite drawn in its place
static BOOL TrainerClassHasBw2Sprite(int trainerClass) {
    switch (trainerClass) {
    case TRAINERCLASS_YOUNGSTER:
    case TRAINERCLASS_LASS:
    case TRAINERCLASS_TWINS:
    case TRAINERCLASS_HIKER:
    case TRAINERCLASS_BATTLE_GIRL:
    case TRAINERCLASS_FISHERMAN:
    case TRAINERCLASS_CYCLIST_M:
    case TRAINERCLASS_CYCLIST_F:
    case TRAINERCLASS_BLACK_BELT:
    case TRAINERCLASS_ARTIST:
    case TRAINERCLASS_PKMN_BREEDER_M:
    case TRAINERCLASS_PKMN_BREEDER_F:
    case TRAINERCLASS_POKEFAN_M:
    case TRAINERCLASS_POKEFAN:
    case TRAINERCLASS_ACE_TRAINER_M:
    case TRAINERCLASS_ACE_TRAINER_F:
    case TRAINERCLASS_ACE_TRAINER_M_GS:
    case TRAINERCLASS_ACE_TRAINER_F_GS:
    case TRAINERCLASS_WAITRESS:
    case TRAINERCLASS_VETERAN:
    case TRAINERCLASS_RICH_BOY:
    case TRAINERCLASS_LADY:
    case TRAINERCLASS_GENTLEMAN:
    case TRAINERCLASS_SOCIALITE:
    case TRAINERCLASS_BEAUTY:
    case TRAINERCLASS_POLICEMAN:
    case TRAINERCLASS_PKMN_RANGER_M:
    case TRAINERCLASS_PKMN_RANGER_F:
    case TRAINERCLASS_SCIENTIST:
    case TRAINERCLASS_SWIMMER_M:
    case TRAINERCLASS_SWIMMER_F:
    case TRAINERCLASS_PSYCHIC_M:
    case TRAINERCLASS_PSYCHIC_F:
    case TRAINERCLASS_GUITARIST:
    case TRAINERCLASS_ROUGHNECK:
    case TRAINERCLASS_WORKER:
    case TRAINERCLASS_SCHOOL_KID_M:
    case TRAINERCLASS_SCHOOL_KID_F:
    case TRAINERCLASS_BIKER:
    case TRAINERCLASS_PARASOL_LADY:
    case TRAINERCLASS_WAITER:
    case TRAINERCLASS_LEADER_FALKNER:
    case TRAINERCLASS_LEADER_BUGSY:
    case TRAINERCLASS_LEADER_WHITNEY:
    case TRAINERCLASS_LEADER_MORTY:
    case TRAINERCLASS_LEADER_PRYCE:
    case TRAINERCLASS_LEADER_JASMINE:
    case TRAINERCLASS_LEADER_CHUCK:
    case TRAINERCLASS_LEADER_CLAIR:
    case TRAINERCLASS_LEADER_BROCK:
    case TRAINERCLASS_LEADER_MISTY:
    case TRAINERCLASS_LEADER_LT_SURGE:
    case TRAINERCLASS_LEADER_ERIKA:
    case TRAINERCLASS_LEADER_JANINE:
    case TRAINERCLASS_LEADER_SABRINA:
    case TRAINERCLASS_LEADER_BLAINE:
    case TRAINERCLASS_LEADER_BLUE:
    case TRAINERCLASS_PKMN_TRAINER_RED:
    case TRAINERCLASS_CHAMPION:
    case TRAINERCLASS_PKMN_TRAINER_LANCE:
    case TRAINERCLASS_ROCKET_BOSS:
        return TRUE;
    default:
        return FALSE;
    }
}

// HGSS's trainer sprite (a/0/5/8), for the Gen 5 battle to draw in place of its class's: the Platinum fork's
// TrainerArtToGen5 (src/port/gen5_battle.c), which reads Platinum's, the same format. Each class's picture is an 80x80
// cell bank (NCER) of poses of OBJ rectangles over a block of its character file (NCGR), with its palette (NCLR) and
// an animation bank (NANR) whose sequence 1 plays through the poses once
#define ART_MAX_RECTS 32
#define ART_POOL_SIZE (48 * 1024)

typedef struct ArtRect {
    u8 width;
    u8 height;
    u8 *pixels; // one pixel (0 to 15) a byte, in the pool
    int x;      // in the texture
    int y;
} ArtRect;

static const u8 sArtObjWidth[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 sArtObjHeight[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

static void TrainerArtToGen5(int trainerClass, Gen5Trainer *dest)
{
    const int base = trainerClass * 5; // a/0/5/8: tiles, palette, cells, animations, scan (GetTrainerPicFileIds)
    const u32 tileSize = GetNarcMemberSizeByIdPair(NARC_a_0_5_8, base);
    const u8 *tiles = malloc(tileSize);
    const u8 *palette = malloc(GetNarcMemberSizeByIdPair(NARC_a_0_5_8, base + 1));
    const u8 *cells = malloc(GetNarcMemberSizeByIdPair(NARC_a_0_5_8, base + 2));
    const u8 *anims = malloc(GetNarcMemberSizeByIdPair(NARC_a_0_5_8, base + 3));
    ArtRect *rects = malloc(sizeof(ArtRect) * ART_MAX_RECTS);
    u8 *pool = malloc(ART_POOL_SIZE);
    int poolUsed = 0;
    int rectCount = 0;
    BOOL ok = TRUE;
    int left = 0, top = 0, right = 0, bottom = 0;
    BOOL first = TRUE;

    ReadWholeNarcMemberByIdPair((void *)tiles, NARC_a_0_5_8, base);
    ReadWholeNarcMemberByIdPair((void *)palette, NARC_a_0_5_8, base + 1);
    ReadWholeNarcMemberByIdPair((void *)cells, NARC_a_0_5_8, base + 2);
    ReadWholeNarcMemberByIdPair((void *)anims, NARC_a_0_5_8, base + 3);

    // NCGR: "RGCN" header 16 bytes, "RAHC" section: tiles from 0x30. NCLR: colors from 0x28. NCER: "KBEC" body at 0x18:
    // cell count (u16), attributes (u16), the cells' offset (u32), mapping, the VRAM transfer table's offset (u32) from
    // the body; cell entries (16 bytes each) from 0x30, the OBJs (6 bytes each) after them
    const u8 *tileData = tiles + 0x30;
    const u32 dataSize = tileSize - 0x30;
    const int cellCount = *(const u16 *)(cells + 0x18);
    const u32 transferOffset = *(const u32 *)(cells + 0x18 + 12);
    const u8 *transfer = cells + 0x18 + transferOffset + 8; // after the maximum size and the count: (source, size) per cell
    const u8 *objsBase = cells + 0x30 + cellCount * 16;
    dest->artPoseCount = 0;
    dest->artQuadCount = 0;
    dest->artSeqCount = 0;
    dest->artLastPose = 0;
    if (cellCount <= 0 || cellCount > GEN5_ART_MAX_POSES) {
        ok = FALSE;
    }

    memset(dest->artTexture, 0, sizeof(dest->artTexture));
    for (int i = 0; i < 16; i++) {
        dest->artPalette[i] = *(const u16 *)(palette + 0x28 + i * 2);
    }
    dest->artPalette[0] = 0;

    // The picture's box: every OBJ of every pose (they are placed from the cell's centre, -40..40 for 80x80)
    for (int c = 0; ok && c < cellCount; c++) {
        const u8 *entry = cells + 0x30 + c * 16;
        const int objCount = *(const u16 *)entry;
        const u8 *objs = objsBase + *(const u32 *)(entry + 4);
        for (int k = 0; k < objCount; k++) {
            const u16 attr0 = *(const u16 *)(objs + k * 6);
            const u16 attr1 = *(const u16 *)(objs + k * 6 + 2);
            const int shape = (attr0 >> 14) & 3;
            const int size = (attr1 >> 14) & 3;
            int x = attr1 & 0x1FF;
            int y = attr0 & 0xFF;
            if (shape > 2) {
                continue;
            }
            x = x >= 256 ? x - 512 : x;
            y = y >= 128 ? y - 256 : y;
            const int w = sArtObjWidth[shape][size];
            const int h = sArtObjHeight[shape][size];
            if (first) {
                left = x;
                top = y;
                right = x + w;
                bottom = y + h;
                first = FALSE;
            }
            left = x < left ? x : left;
            top = y < top ? y : top;
            right = x + w > right ? x + w : right;
            bottom = y + h > bottom ? y + h : bottom;
        }
    }
    const int width = right - left;
    const int height = bottom - top;
    ok = ok && !first && width > 0 && height > 0 && width <= 256 && height <= 128;

    // Each pose's OBJs as quads, each OBJ's pixels taken from its cell's block of tiles and shared with an earlier
    // rectangle of the same pixels
    for (int c = 0; ok && c < cellCount; c++) {
        const u8 *entry = cells + 0x30 + c * 16;
        const int objCount = *(const u16 *)entry;
        const u8 *objs = objsBase + *(const u32 *)(entry + 4);
        const u32 source = *(const u32 *)(transfer + c * 8);
        dest->artPoseFirst[c] = dest->artQuadCount;
        dest->artPoseQuads[c] = 0;
        for (int k = 0; ok && k < objCount; k++) {
            const u16 attr0 = *(const u16 *)(objs + k * 6);
            const u16 attr1 = *(const u16 *)(objs + k * 6 + 2);
            const u16 attr2 = *(const u16 *)(objs + k * 6 + 4);
            const int shape = (attr0 >> 14) & 3;
            const int size = (attr1 >> 14) & 3;
            int x = attr1 & 0x1FF;
            int y = attr0 & 0xFF;
            if (shape > 2) {
                continue;
            }
            x = x >= 256 ? x - 512 : x;
            y = y >= 128 ? y - 256 : y;
            const int w = sArtObjWidth[shape][size];
            const int h = sArtObjHeight[shape][size];
            const int firstTile = (attr2 & 0x3FF) * 2;
            if (dest->artQuadCount >= GEN5_ART_MAX_QUADS || rectCount >= ART_MAX_RECTS || poolUsed + w * h > ART_POOL_SIZE) {
                ok = FALSE;
                break;
            }
            ArtRect *rect = &rects[rectCount];
            rect->width = w;
            rect->height = h;
            rect->pixels = pool + poolUsed;
            for (int py = 0; py < h; py++) {
                for (int px = 0; px < w; px++) {
                    const int tile = firstTile + (py / 8) * (w / 8) + px / 8;
                    const u32 offset = source + tile * 32 + (py % 8) * 4 + (px % 8) / 2;
                    u8 pixel = 0;
                    if (offset < dataSize) {
                        const u8 pair = tileData[offset];
                        pixel = (px & 1) ? pair >> 4 : pair & 15;
                    }
                    rect->pixels[py * w + px] = pixel;
                }
            }
            int index = rectCount;
            for (int r = 0; r < rectCount; r++) {
                if (rects[r].width == w && rects[r].height == h && memcmp(rects[r].pixels, rect->pixels, w * h) == 0) {
                    index = r;
                    break;
                }
            }
            if (index == rectCount) {
                rectCount++;
                poolUsed += w * h;
            }
            // The quad is placed as HGSS places it: from the cell's centre, whose feet are at 40 below it (a pose
            // that jumps leaves the ground, as it does there; the bottom of the union of the poses isn't the ground).
            // The origin is under the feet; its texture place is set below
            dest->artQuad[dest->artQuadCount][0] = x;
            dest->artQuad[dest->artQuadCount][1] = 40 - y;
            dest->artQuad[dest->artQuadCount][2] = w;
            dest->artQuad[dest->artQuadCount][3] = h;
            dest->artQuad[dest->artQuadCount][4] = index; // the rectangle for now
            dest->artQuad[dest->artQuadCount][5] = 0;
            dest->artQuadCount++;
            dest->artPoseQuads[c]++;
        }
    }

    // The rectangles into the texture, in rows, the tallest first
    int order[ART_MAX_RECTS];
    for (int r = 0; ok && r < rectCount; r++) {
        order[r] = r;
    }
    for (int i = 1; ok && i < rectCount; i++) {
        const int v = order[i];
        int j = i - 1;
        while (j >= 0 && rects[order[j]].height < rects[v].height) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    int cursorX = 0, cursorY = 0, rowHeight = 0;
    for (int i = 0; ok && i < rectCount; i++) {
        ArtRect *rect = &rects[order[i]];
        if (cursorX + rect->width > 256) {
            cursorX = 0;
            cursorY += rowHeight;
            rowHeight = 0;
        }
        if (cursorY + rect->height > 128) {
            ok = FALSE;
            break;
        }
        rect->x = cursorX;
        rect->y = cursorY;
        cursorX += rect->width;
        rowHeight = rect->height > rowHeight ? rect->height : rowHeight;
        for (int py = 0; py < rect->height; py++) {
            for (int px = 0; px < rect->width; px++) {
                const int tx = rect->x + px;
                const int ty = rect->y + py;
                u8 *out = &dest->artTexture[(ty * 256 + tx) / 2];
                *out |= (tx & 1) ? rect->pixels[py * rect->width + px] << 4 : rect->pixels[py * rect->width + px];
            }
        }
    }
    for (int q = 0; ok && q < dest->artQuadCount; q++) {
        const ArtRect *rect = &rects[dest->artQuad[q][4]];
        dest->artQuad[q][4] = rect->x;
        dest->artQuad[q][5] = rect->y;
    }

    // The animation: sequence 1 plays through the poses once, ending on the pose that stays (sequence 0, one pose, if
    // there is no other). NANR's "KNBA" body at 0x18: sequence count (u16), frame count (u16), the sequences' offset,
    // the frames' and the frame data's (u32 each); a sequence (16 bytes): frame count (u16), ..., its frames' offset (u32
    // at 12); a frame (8 bytes): its data's offset (u32), its length in ticks (u16); the data: the cell (u16)
    if (ok) {
        const u8 *body = anims + 0x18;
        const int sequenceCount = *(const u16 *)body;
        const u32 sequenceOffset = *(const u32 *)(body + 4);
        const u32 frameOffset = *(const u32 *)(body + 8);
        const u32 dataOffset = *(const u32 *)(body + 12);
        const int wanted = sequenceCount > 1 ? 1 : 0;
        if (sequenceCount > 0) {
            const u8 *sequence = body + sequenceOffset + wanted * 16;
            const int frameCount = *(const u16 *)sequence;
            const u8 *frames = body + frameOffset + *(const u32 *)(sequence + 12);
            for (int f = 0; f < frameCount && f < GEN5_ART_MAX_SEQ; f++) {
                const u32 data = *(const u32 *)(frames + f * 8);
                const int cell = *(const u16 *)(body + dataOffset + data);
                // HGSS's battle ticks its animations once a logic frame (30 a second): two ticks of 1/60
                dest->artSeqPose[f] = cell < cellCount ? cell : 0;
                dest->artSeqTicks[f] = *(const u16 *)(frames + f * 8 + 4) * 2;
                dest->artSeqCount = f + 1;
            }
            if (dest->artSeqCount > 0) {
                dest->artLastPose = dest->artSeqPose[dest->artSeqCount - 1];
            }
        }
    }
    dest->artPoseCount = ok ? cellCount : 0;
    dest->artWidth = width;
    dest->artHeight = height;
    dest->hasArt = ok && dest->artQuadCount > 0;
    free((void *)tiles);
    free((void *)palette);
    free((void *)cells);
    free((void *)anims);
    free(rects);
    free(pool);
}


// The prize for a class (battle_command.c's sPrizeMoneyTbl: class and prize, by pairs)
extern const u16 sPrizeMoneyTbl[258];

static unsigned PrizeBase(int trainerClass) {
    for (int i = 0; i + 1 < (int)NELEMS(sPrizeMoneyTbl); i += 2) {
        if (sPrizeMoneyTbl[i] == trainerClass) {
            return sPrizeMoneyTbl[i + 1];
        }
    }
    return sPrizeMoneyTbl[2 * 2 + 1]; // a Youngster's, as CalcPrizeMoney falls back to
}

// A trainer of the battle (an opponent, or the player's partner) as the Gen 5 code takes it: its class, AI, items and
// party, its name, its class's name and the line it says when beaten from HGSS's messages, and its class's prize
static void TrainerToGen5(BattleSetup *setup, int battler, Gen5Trainer *dest) {
    const Trainer *trainer = &setup->trainer[battler];
    const int id = setup->trainerId[battler];
    String *text = String_New(GEN5_TEXT_LENGTH, HEAP_ID_FIELD2);

    dest->id = id;
    dest->platinumClass = trainer->data.trainerClass;
    dest->aiFlags = trainer->data.aiFlags;
    if (!TrainerClassHasBw2Sprite(dest->platinumClass)) {
        TrainerArtToGen5(dest->platinumClass, dest);
    }
    for (int i = 0; i < 4; i++) {
        dest->items[i] = trainer->data.items[i];
    }
    dest->prizeBase = PrizeBase(dest->platinumClass);
    // A trainer's name may be kept compressed (String_Cat_HandleTrainerName unpacks it)
    String *packed = String_New(PLAYER_NAME_LENGTH + 1, HEAP_ID_FIELD2);
    CopyU16ArrayToStringN(packed, trainer->name, PLAYER_NAME_LENGTH + 1);
    String_SetEmpty(text);
    String_Cat_HandleTrainerName(text, packed);
    String_Delete(packed);
    TextToGen5(String_cstr(text), String_GetLength(text) + 1, dest->name, 16);

    MsgData *classNames = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0730_bin, HEAP_ID_FIELD2);
    if (classNames != NULL) {
        ReadMsgDataIntoString(classNames, dest->platinumClass, text);
        DestroyMsgData(classNames);
        TextToGen5(String_cstr(text), String_GetLength(text) + 1, dest->className, 32);
    } else {
        dest->className[0] = 0xFFFF;
    }

    // What the trainer says during the battle (first hit, half HP, last Pokémon, last at half HP), as HGSS's battle shows
    // TRMSG_HIT_POKE_FIRST_TIME to TRMSG_LAST_POKE_HALF; BW2 shows them at the same moments (btl_client.c's hints)
    for (int k = 0; k < 4; k++) {
        dest->hintText[k][0] = 0xFFFF;
        if (TrainerMessageWithIdPairExists(id, TRMSG_HIT_POKE_FIRST_TIME + k, HEAP_ID_FIELD2)) {
            GetTrainerMessageByIdPair(id, TRMSG_HIT_POKE_FIRST_TIME + k, text, HEAP_ID_FIELD2);
            TextToGen5(String_cstr(text), String_GetLength(text) + 1, dest->hintText[k], GEN5_TEXT_LENGTH);
            printf("Gen 5: trainer %d has line %d\n", id, TRMSG_HIT_POKE_FIRST_TIME + k);
        }
    }

    dest->defeatText[0] = 0xFFFF;
    if (TrainerMessageWithIdPairExists(id, TRMSG_LOSE, HEAP_ID_FIELD2)) {
        GetTrainerMessageByIdPair(id, TRMSG_LOSE, text, HEAP_ID_FIELD2);
        TextToGen5(String_cstr(text), String_GetLength(text) + 1, dest->defeatText, GEN5_TEXT_LENGTH);
    } else {
        // A double battle's trainer (twins, a couple) has a line for each of its two people instead: both, a page each
        static const int types[] = { TRMSG_DBL_LOSE_1, TRMSG_DBL_LOSE_2 };
        int n = 0;
        for (int k = 0; k < (int)NELEMS(types); k++) {
            if (!TrainerMessageWithIdPairExists(id, types[k], HEAP_ID_FIELD2) || n + 4 >= GEN5_TEXT_LENGTH) {
                continue;
            }
            if (n > 0) {
                // A new page: BW2's stream command that clears the window
                dest->defeatText[n++] = 0xF000;
                dest->defeatText[n++] = 0xBE01;
                dest->defeatText[n++] = 0;
            }
            GetTrainerMessageByIdPair(id, types[k], text, HEAP_ID_FIELD2);
            TextToGen5(String_cstr(text), String_GetLength(text) + 1, dest->defeatText + n, GEN5_TEXT_LENGTH - n);
            while (dest->defeatText[n] != 0xFFFF) {
                n++;
            }
            while (n > 0 && dest->defeatText[n - 1] == 0xFFFE) {
                dest->defeatText[--n] = 0xFFFF;
            }
        }
    }
    String_Delete(text);

    Party *party = setup->party[battler];
    dest->partyCount = Party_GetCount(party);
    if (dest->partyCount > GEN5_PARTY_MAX) {
        dest->partyCount = GEN5_PARTY_MAX;
    }
    for (int i = 0; i < dest->partyCount; i++) {
        PokemonToGen5(Party_GetMonByIndex(party, i), &dest->party[i]);
    }
}

// --- Where the battle is ----------------------------------------------------------------------------------------

// BW2's battle backgrounds (the field environment's bgType)
enum {
    ARENA_BG_GRASS = 0,
    ARENA_BG_CITY = 2,
    ARENA_BG_INDOORS = 4,
    ARENA_BG_OVERCAST = 6,
    ARENA_BG_MOUNTAIN = 7,
    ARENA_BG_SEA = 8,
};

static BOOL IsRocketClass(int trainerClass) {
    return trainerClass == TRAINERCLASS_TEAM_ROCKET || trainerClass == TRAINERCLASS_TEAM_ROCKET_F
        || trainerClass == TRAINERCLASS_EXECUTIVE_ARIANA || trainerClass == TRAINERCLASS_EXECUTIVE_ARCHER
        || trainerClass == TRAINERCLASS_EXECUTIVE_PROTON || trainerClass == TRAINERCLASS_EXECUTIVE_PETREL
        || trainerClass == TRAINERCLASS_ROCKET_BOSS;
}

static BOOL IsRivalClass(int trainerClass) {
    return trainerClass == TRAINERCLASS_RIVAL;
}

static BOOL IsGymLeaderClass(int trainerClass) {
    return (trainerClass >= TRAINERCLASS_LEADER_MORTY && trainerClass <= TRAINERCLASS_LEADER_CLAIR)
        || trainerClass == TRAINERCLASS_LEADER_FALKNER || trainerClass == TRAINERCLASS_LEADER_BUGSY
        || trainerClass == TRAINERCLASS_LEADER_WHITNEY || trainerClass == TRAINERCLASS_LEADER_BROCK
        || (trainerClass >= TRAINERCLASS_LEADER_MISTY && trainerClass <= TRAINERCLASS_LEADER_BLAINE)
        || trainerClass == TRAINERCLASS_LEADER_BLUE;
}

static void ArenaFromSetup(const BattleSetup *setup, int *bg, int *terrain) {
    const int trainerClass = (setup->battleType & BATTLE_TYPE_TRAINER) ? setup->trainer[BATTLER_ENEMY].data.trainerClass : -1;
    if (IsRocketClass(trainerClass)) {
        // Team Rocket's own arena (lib/port/gen5_rocket.c): BW2's Champion room, darkened, with Rocket red streaks, a
        // rocket ship and the moon in its sky
        *bg = GEN5_ROCKET_BG;
        *terrain = GEN5_ROCKET_TERRAIN;
        return;
    }
    if (IsGymLeaderClass(trainerClass)) {
        // BW2's Gym Leaders battle on a stone floor in a dark hall (Clay's), as the Platinum fork's do
        *bg = ARENA_BG_INDOORS;
        *terrain = 19;
        return;
    }
    if (IsRivalClass(trainerClass) && MapHeader_GetMapType(setup->mapNumber) == MAP_TYPE_CITY_TOWN) {
        // Silver: his first battle (outside the lab in New Bark Town) and any other he starts in a town get BW2's
        // grass field with a dirt path (terrain 1), not the plain green one its own default terrain (0) gives
        *bg = ARENA_BG_GRASS;
        *terrain = 1;
        return;
    }
    // The Elite Four and Lance in BW2's Elite Four's and Champion's rooms (12 to 16), one each
    switch (trainerClass) {
    case TRAINERCLASS_ELITE_FOUR_WILL:
        *bg = 12;
        *terrain = 0;
        return;
    case TRAINERCLASS_ELITE_FOUR_KOGA:
        *bg = 13;
        *terrain = 0;
        return;
    case TRAINERCLASS_ELITE_FOUR_BRUNO:
        *bg = 14;
        *terrain = 0;
        return;
    case TRAINERCLASS_ELITE_FOUR_KAREN:
        *bg = 15;
        *terrain = 0;
        return;
    case TRAINERCLASS_CHAMPION:
        *bg = 16;
        *terrain = 0;
        return;
    }
    if (trainerClass >= 0 && setup->terrain == TERRAIN_PLAIN
        && (setup->battleBg == BATTLE_BG_GENERAL || setup->battleBg == BATTLE_BG_CITY)) {
        // Any trainer who challenges the player on a town's or a route's ordinary ground (a path, not tall grass,
        // sand or water) gets the same brown floor with a grassy edge as Silver in a town, not BW2's plain green
        // field or its city
        const u8 mapType = MapHeader_GetMapType(setup->mapNumber);
        if (mapType == MAP_TYPE_CITY_TOWN || mapType == MAP_TYPE_ROUTE) {
            *bg = ARENA_BG_GRASS;
            *terrain = 1;
            return;
        }
    }
    switch (setup->battleBg) {
    case BATTLE_BG_OCEAN:
        *bg = ARENA_BG_SEA;
        break;
    case BATTLE_BG_CITY:
        *bg = ARENA_BG_CITY;
        break;
    case BATTLE_BG_MOUNTAIN:
        *bg = ARENA_BG_MOUNTAIN;
        break;
    case BATTLE_BG_SNOW:
        *bg = ARENA_BG_OVERCAST;
        break;
    case BATTLE_BG_BUILDING_1:
    case BATTLE_BG_BUILDING_2:
    case BATTLE_BG_BUILDING_3:
    case BATTLE_BG_CAVE_1:
    case BATTLE_BG_CAVE_2:
    case BATTLE_BG_CAVE_3:
        *bg = ARENA_BG_INDOORS;
        break;
    default:
        *bg = ARENA_BG_GRASS;
        break;
    }
    switch (setup->terrain) {
    case TERRAIN_SAND:
        *terrain = 8;
        break;
    case TERRAIN_GRASS:
        *terrain = 5;
        break;
    case TERRAIN_PUDDLE:
        *terrain = 9;
        break;
    case TERRAIN_MOUNTAIN:
    case TERRAIN_CAVE:
        *terrain = 10;
        break;
    case TERRAIN_SNOW:
        *terrain = 14;
        break;
    case TERRAIN_ICE:
        *terrain = 13;
        break;
    case TERRAIN_WATER:
        *terrain = 6;
        break;
    case TERRAIN_BUILDING:
        *terrain = *bg == ARENA_BG_INDOORS ? 19 : 0;
        break;
    default:
        *terrain = 0;
        break;
    }
}

// --- The application --------------------------------------------------------------------------------------------

static void VBlank(void *data) {
    (void)data;
    Gen5_BattleVBlank();
}

// The pockets with what a battle can use
static const u8 sBattlePockets[] = { POCKET_ITEMS, POCKET_MEDICINE, POCKET_BALLS, POCKET_BERRIES, POCKET_BATTLE_ITEMS };

// Nuzlocke mode's rules, for BW2's engine (gen5/src/runtime.c and its patches). HGSS has no doubles-wild support
// yet (no wild2) and no Hard level-cap setting (Platinum's Gear_LevelCap), so this is simpler than that fork's
static void PortNuzlockeToGen5(BattleSetup *setup)
{
    sBattle.levelCap = NuzlockeMode_LevelCap();
    sBattle.critGuard = NuzlockeMode_CritGuard();
    if (!sBattle.isTrainer) {
        // The catch rule (NuzlockeMode_OnWildBattleStart ran before the battle, in encounter.c)
        Pokemon *wild = Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0);
        sBattle.wildCatchRefused = !NuzlockeMode_CatchAllowed(GetMonData(wild, MON_DATA_SPECIES, NULL), MonIsShiny(wild));
    }
    // BW2's text: Unicode, 0xFFFE a new line, 0xFFFF the end
    static const char kText[] = NUZLOCKE_CATCH_REFUSED_TEXT;
    int length = 0;
    for (const char *c = kText; *c != '\0' && length < GEN5_PORT_TEXT_LENGTH - 1; c++) {
        sBattle.catchRefusedText[length++] = (unsigned char)*c;
    }
    sBattle.catchRefusedText[length] = 0xFFFF;
}

static BOOL Init(OverlayManager *man, int *state) {
    (void)state;
    BattleSetup *setup = OverlayManager_GetArgs(man);
    Party *party = setup->party[BATTLER_PLAYER];
    PlayerProfile *profile = setup->profile[BATTLER_PLAYER];

    TestScript_OnEvent(TEST_EVENT_BATTLE);
    memset(&sBattle, 0, sizeof(sBattle));
    sBattle.apricornBalls = TRUE; // HGSS's Apricorn Balls' rules (btl_server_flow_sub.c's patch)
    sBattle.fishing = setup->isFishing != FALSE;
    TextToGen5(PlayerProfile_GetNamePtr(profile), PLAYER_NAME_LENGTH + 1, sBattle.player.name, 8);
    ArenaFromSetup(setup, &sBattle.arenaBg, &sBattle.arenaTerrain);
    {
        // For looking at arenas: POKEHEARTGOLD_GEN5_ARENA=bg,terrain (16,1 is Team Rocket's)
        const char *arena = getenv("POKEHEARTGOLD_GEN5_ARENA");
        int bg, terrain;
        if (arena != NULL && sscanf(arena, "%d,%d", &bg, &terrain) == 2) {
            sBattle.arenaBg = bg;
            sBattle.arenaTerrain = terrain;
        }
    }
    {
        RTCTime now;
        GF_RTC_CopyTime(&now);
        sBattle.hour = now.hour;
        sBattle.minute = now.minute;
        sBattle.zone = 0;
    }
    sBattle.textSpeed = Options_GetTextSpeed(setup->options);
    sBattle.setStyle = Options_GetBattleStyle(setup->options) == 1;
    sBattle.player.id = PlayerProfile_GetTrainerID(profile);
    sBattle.player.gender = PlayerProfile_GetTrainerGender(profile);
    sBattle.player.money = PlayerProfile_GetMoney(profile);
    sBattle.player.badges = PlayerProfile_CountBadges(profile);
    sBattle.partyCount = Party_GetCount(party);
    if (sBattle.partyCount > GEN5_PARTY_MAX) {
        sBattle.partyCount = GEN5_PARTY_MAX;
    }
    for (int i = 0; i < sBattle.partyCount; i++) {
        PokemonToGen5(Party_GetMonByIndex(party, i), &sBattle.party[i]);
    }
    if (setup->battleType == BATTLE_TYPE_SAFARI) {
        // The Safari Balls left, and the wild species' catch and flee rates (HGSS's data: BW2 has no flee rate). HGSS's
        // Safari rules are Platinum's (battle_controller_player.c's bait and mud, the opponent's flee check), which
        // gen5/src/safari.c plays
        const u16 species = GetMonData(Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0), MON_DATA_SPECIES, NULL);
        sBattle.safari = TRUE;
        sBattle.safariBalls = setup->safariBalls;
        sBattle.safariCatchRate = GetMonBaseStat(species, BASE_CATCH_RATE);
        sBattle.safariFleeRate = GetMonBaseStat(species, BASE_GREAT_MARSH_RATE);
    }
    sBattle.isTrainer = (setup->battleType & BATTLE_TYPE_TRAINER) != 0;
    sBattle.doubles = (setup->battleType & BATTLE_TYPE_DOUBLES) != 0;
    if (sBattle.isTrainer) {
        TrainerToGen5(setup, BATTLER_ENEMY, &sBattle.trainer);
        // Two trainers (TAG, or MULTI); a trainer who has both slots is one double battle's trainer
        if ((setup->battleType & (BATTLE_TYPE_TAG | BATTLE_TYPE_MULTI)) && setup->trainerId[BATTLER_ENEMY2] != 0
            && setup->trainerId[BATTLER_ENEMY2] != setup->trainerId[BATTLER_ENEMY]) {
            sBattle.hasTrainer2 = TRUE;
            TrainerToGen5(setup, BATTLER_ENEMY2, &sBattle.trainer2);
        }
        // The partner, whom BW2's AI plays beside the player
        if ((setup->battleType & BATTLE_TYPE_AI) && setup->trainerId[BATTLER_PLAYER2] != 0) {
            sBattle.hasPartner = TRUE;
            TrainerToGen5(setup, BATTLER_PLAYER2, &sBattle.partner);
        }
    } else {
        PokemonToGen5(Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0), &sBattle.wild);
    }
    PortNuzlockeToGen5(setup);
    for (int p = 0; p < (int)NELEMS(sBattlePockets); p++) {
        ItemSlot *slot;
        for (int i = 0; (slot = Bag_GetPocketSlotN(setup->bag, sBattlePockets[p], i)) != NULL && sBattle.itemCount < GEN5_ITEMS_MAX; i++) {
            if (slot->id != ITEM_NONE && slot->quantity != 0) {
                sBattle.items[sBattle.itemCount].item = slot->id;
                sBattle.items[sBattle.itemCount].count = slot->quantity;
                sBattle.itemCount++;
            }
        }
    }
    sItemsBefore = sBattle.itemCount;
    memcpy(sItemCountsBefore, sBattle.items, sizeof(sBattle.items));
    // Seen, as HGSS's battle registers a wild Pokémon when it appears; a trainer's, all of them for now (BW2 doesn't say
    // which it sent out)
    if (sBattle.isTrainer) {
        for (int b = BATTLER_ENEMY; b < BATTLER_MAX; b += 2) {
            if (b == BATTLER_ENEMY || sBattle.hasTrainer2) {
                for (int i = 0; i < Party_GetCount(setup->party[b]); i++) {
                    Pokedex_SetMonSeenFlag(setup->pokedex, Party_GetMonByIndex(setup->party[b], i));
                }
            }
        }
    } else {
        Pokedex_SetMonSeenFlag(setup->pokedex, Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0));
    }

    // HGSS's trainer classes and its Pokémon's origin game; White 2's light HP boxes (Platinum keeps Black 2's dark ones)
    Gen5_SetGame(gGameVersion);
    Gen5_SetWhiteGauges(1);
    // Before every battle: BW2 takes the archives it reads at its first start
    Gen5Rocket_PrepareArena();
    sFailed = !Gen5_BattleBegin(&sBattle, Gen5Rom_Dir());
    if (sFailed) {
        printf("Gen 5: the battle couldn't start\n");
    } else {
        Main_SetVBlankIntrCB(VBlank, NULL);
        sShown = TRUE;
        // BW2's shadows are translucent quads that overlap: the DS doesn't blend polygons of one ID over each other
        SIM_SetTranslucentPolygonIdRule(TRUE);
    }
    return TRUE;
}

// A caught Pokémon, as HGSS's battle takes it (battle_command.c's catch sequence): what the battle changed, the
// player's catch data, the Pokédex and the game stats, then into the party or the first box with room
static void StoreCaught(BattleSetup *setup) {
    Pokemon *mon = Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0);

    PokemonFromGen5(mon, &sBattle.caughtMon);
    // BW2 gives the ball's number (constants/balls.h); HGSS's catch data takes its item, which for the Apricorn Balls
    // is another number (the Fast Ball is ball 17, item 492)
    u32 ballItem = sBattle.caughtMon.ball;
    if (ballItem >= BALL_FAST && ballItem <= BALL_SPORT) {
        ballItem = ballItem - BALL_FAST + ITEM_FAST_BALL;
    } else if (ballItem < BALL_MASTER || ballItem > BALL_CHERISH) {
        ballItem = ITEM_POKE_BALL;
    }
    MonSetCatchData(mon, setup->profile[BATTLER_PLAYER], ballItem, setup->mapSection, setup->terrain, HEAP_ID_FIELD2);
    // A Friend Ball's catch starts friendly, as HGSS's catch sets it (battle_command.c)
    if (ballItem == ITEM_FRIEND_BALL) {
        u8 friendship = FRIENDSHIP_TIER_HI_MIN;
        SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    }
    Pokedex_SetMonCaughtFlag(setup->pokedex, mon);
    GameStats_Inc(setup->gameStats, GAME_STAT_CAUGHT_MON);
    setup->caughtBattlerId = BATTLER_ENEMY;
    if (Party_AddMon(setup->party[BATTLER_PLAYER], mon)) {
        printf("Gen 5: caught species %u, into the party\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL));
        return;
    }
    const int box = PCStorage_FindFirstBoxWithEmptySlot(setup->storagePC);
    if (box < 0 || box >= NUM_BOXES) {
        printf("Gen 5: caught species %u, but every box is full\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL));
        return;
    }
    PCStorage_SetActiveBox(setup->storagePC, box);
    for (int i = 0; i < 4; i++) {
        u32 maxPP = GetMonData(mon, MON_DATA_MOVE1_MAX_PP + i, NULL);
        SetMonData(mon, MON_DATA_MOVE1_PP + i, &maxPP);
    }
    Mon_UpdateGiratinaForm(mon);
    PCStorage_PlaceMonInBoxFirstEmptySlot(setup->storagePC, box, Mon_GetBoxMon(mon));
    printf("Gen 5: caught species %u, into box %d\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL), box + 1);
}

// What the battle left, into the BattleSetup, as HGSS's battle leaves it
static void Finish(BattleSetup *setup) {
    Main_SetVBlankIntrCB(NULL, NULL);
    sShown = FALSE;
    SIM_SetTranslucentPolygonIdRule(FALSE);
    Gen5_SetWidescreen(1.0f);
    const enum Gen5BattleResult result = sFailed ? GEN5_BATTLE_FAILED : Gen5_BattleEnd(&sBattle);

    if (!sFailed) {
        Party *party = setup->party[BATTLER_PLAYER];
        for (int i = 0; i < sBattle.partyCount; i++) {
            Pokemon *mon = Party_GetMonByIndex(party, i);
            if (PokemonFromGen5(mon, &sBattle.party[i])) {
                setup->levelUpFlag |= 1 << i;
            }
            printf("Gen 5: party %d after the battle: species %u, level %u, exp %u, HP %u/%u\n", i,
                (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL), (unsigned)GetMonData(mon, MON_DATA_LEVEL, NULL),
                (unsigned)GetMonData(mon, MON_DATA_EXPERIENCE, NULL), (unsigned)GetMonData(mon, MON_DATA_HP, NULL),
                (unsigned)GetMonData(mon, MON_DATA_MAX_HP, NULL));
        }
        for (int i = 0; i < sItemsBefore; i++) {
            const int before = sItemCountsBefore[i].count;
            const int after = sBattle.items[i].count;
            if (after < before) {
                Bag_TakeItem(setup->bag, sBattle.items[i].item, before - after, HEAP_ID_FIELD2);
            } else if (after > before) {
                Bag_AddItem(setup->bag, sBattle.items[i].item, after - before, HEAP_ID_FIELD2);
            }
        }
        PlayerProfile *profile = setup->profile[BATTLER_PLAYER];
        if (sBattle.moneyChange > 0) {
            PlayerProfile_AddMoney(profile, sBattle.moneyChange);
        } else if (sBattle.moneyChange < 0) {
            PlayerProfile_SubMoney(profile, -sBattle.moneyChange);
        }
    }
    if (sBattle.safari && !sFailed) {
        // What is left of the Safari Balls (the encounter writes it to the save's counter and ends the game at 0)
        setup->safariBalls = sBattle.safariBalls;
        printf("Gen 5: a Safari Zone battle, %d balls left%s\n", sBattle.safariBalls, sBattle.safariFled ? ", it fled" : "");
    }
    switch (result) {
    case GEN5_BATTLE_WON:
        setup->winFlag = BATTLE_OUTCOME_WIN;
        break;
    case GEN5_BATTLE_LOST:
        setup->winFlag = BATTLE_OUTCOME_LOSE;
        break;
    case GEN5_BATTLE_CAUGHT:
        setup->winFlag = BATTLE_OUTCOME_MON_CAUGHT; // stored after its naming screen (Main)
        break;
    default:
        // Ran, or failed: back to the overworld as if the player ran; in the Safari Zone the Pokémon may have fled
        setup->winFlag = sBattle.safari && sBattle.safariFled && !sFailed ? BATTLE_OUTCOME_FOE_FLED : BATTLE_OUTCOME_PLAYER_FLED;
        break;
    }
}

// HGSS's victory music for the battle won, as its battle picks it (battle_controller_player.c: by the first opposing
// trainer's class; battle_command.c for a wild Pokémon), played when BW2 asks for its own
static void PlayVictoryMusic(void) {
    u16 seq = SEQ_GS_WIN2;
    if (sBattle.isTrainer) {
        switch (sBattle.trainer.platinumClass) {
        case TRAINERCLASS_LEADER_FALKNER:
        case TRAINERCLASS_LEADER_BUGSY:
        case TRAINERCLASS_LEADER_WHITNEY:
        case TRAINERCLASS_LEADER_MORTY:
        case TRAINERCLASS_LEADER_PRYCE:
        case TRAINERCLASS_LEADER_JASMINE:
        case TRAINERCLASS_LEADER_CHUCK:
        case TRAINERCLASS_LEADER_CLAIR:
        case TRAINERCLASS_CHAMPION:
        case TRAINERCLASS_ELITE_FOUR_WILL:
        case TRAINERCLASS_ELITE_FOUR_KAREN:
        case TRAINERCLASS_ELITE_FOUR_KOGA:
        case TRAINERCLASS_ELITE_FOUR_BRUNO:
        case TRAINERCLASS_LEADER_BROCK:
        case TRAINERCLASS_LEADER_MISTY:
        case TRAINERCLASS_LEADER_LT_SURGE:
        case TRAINERCLASS_LEADER_ERIKA:
        case TRAINERCLASS_LEADER_JANINE:
        case TRAINERCLASS_LEADER_SABRINA:
        case TRAINERCLASS_LEADER_BLAINE:
        case TRAINERCLASS_LEADER_BLUE:
            seq = SEQ_GS_WIN3;
            break;
        case TRAINERCLASS_TOWER_TYCOON:
        case TRAINERCLASS_HALL_MATRON:
        case TRAINERCLASS_FACTORY_HEAD:
        case TRAINERCLASS_ARCADE_STAR:
        case TRAINERCLASS_CASTLE_VALET:
            seq = SEQ_GS_WINBRAIN;
            break;
        default:
            seq = SEQ_GS_WIN1;
            break;
        }
    }
    printf("Gen 5: the battle is won: HGSS's victory music %u\n", (unsigned)seq);
    PlayBGM(seq);
}

// After BW2's battle, as after HGSS's (battle_command.c's catch, battle_022378C0.c's end): the caught Pokémon's naming
// screen (left empty, it keeps its species' name), then each Pokémon that gained a level and evolves now
enum {
    STATE_BATTLE,
    STATE_NAMING,
    STATE_CHECK_EVOLUTION,
    STATE_EVOLUTION,
};

static NamingScreenArgs *sNamingArgs;
static OverlayManager *sNamingApp;

static void StartNaming(BattleSetup *setup) {
    Pokemon *mon = Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0);
    PokemonFromGen5(mon, &sBattle.caughtMon);
    Heap_Create(HEAP_ID_3, HEAP_ID_BATTLE, 0x30000);
    sNamingArgs = NamingScreen_CreateArgs(HEAP_ID_BATTLE, NAME_SCREEN_POKEMON, GetMonData(mon, MON_DATA_SPECIES, NULL),
        POKEMON_NAME_LENGTH, setup->options, NULL);
    // A full party: "{0} was transferred to someone's/Bill's PC!" under the name
    sNamingArgs->battleMsgId = Party_GetCount(setup->party[BATTLER_PLAYER]) < PARTY_SIZE ? 0 : 0x496 + (setup->metBill != FALSE);
    sNamingArgs->monForm = GetMonData(mon, MON_DATA_FORM, NULL);
    sNamingArgs->pcStorage = setup->storagePC;
    sNamingArgs->monGender = GetMonData(mon, MON_DATA_GENDER, NULL);
    sNamingApp = OverlayManager_New(&gOverlayTemplate_NamingScreen, sNamingArgs, HEAP_ID_BATTLE);
}

static void FinishNaming(BattleSetup *setup) {
    Pokemon *mon = Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0);
    if (!sNamingArgs->noInput) {
        SetMonData(mon, MON_DATA_NICKNAME_STRING_AND_FLAG, sNamingArgs->nameInputString);
        GameStats_Inc(setup->gameStats, GAME_STAT_NICKNAMES_GIVEN);
    }
    NamingScreen_DeleteArgs(sNamingArgs);
    OverlayManager_Delete(sNamingApp);
    sNamingArgs = NULL;
    sNamingApp = NULL;
    Heap_Destroy(HEAP_ID_BATTLE);
    StoreCaught(setup);
}

static BOOL Main(OverlayManager *man, int *state) {
    BattleSetup *setup = OverlayManager_GetArgs(man);

    switch (*state) {
    case STATE_BATTLE:
        if (!sFailed) {
            // The keys and touch the game read this frame (the test scripts' among them)
            Gen5_SetInput(gSystem.heldKeysRaw, gSystem.touchHeld, gSystem.touchX, gSystem.touchY);
            Gen5_SetWidescreen(SIM_IsWidescreenActive() ? SIM_WidescreenRatio() : 1.0f);
            const BOOL running = Gen5_BattleFrame();
            const unsigned events = Gen5_TakeEvents();
            if (events & GEN5_EVENT_COMMAND) {
                TestScript_OnEvent(TEST_EVENT_BATTLE_COMMAND);
            }
            if (events & GEN5_EVENT_VICTORY_MUSIC) {
                PlayVictoryMusic();
            }
            if (running) {
                return FALSE;
            }
        }
        Finish(setup);
        if (setup->winFlag == BATTLE_OUTCOME_MON_CAUGHT) {
            StartNaming(setup);
            *state = STATE_NAMING;
        } else {
            *state = STATE_CHECK_EVOLUTION;
        }
        return FALSE;
    case STATE_NAMING:
        if (OverlayManager_Run(sNamingApp)) {
            FinishNaming(setup);
            *state = STATE_CHECK_EVOLUTION;
        }
        return FALSE;
    case STATE_CHECK_EVOLUTION: {
        int slot;
        int condition;
        const int species = BattleSystem_CheckEvolution(setup, &slot, &condition);
        if (species == SPECIES_NONE) {
            return TRUE;
        }
        printf("Gen 5: party %d evolves into species %d\n", slot, species);
        Heap_Create(HEAP_ID_3, HEAP_ID_EVOLUTION, 0x30000);
        Pokemon *mon = Party_GetMonByIndex(setup->party[BATTLER_PLAYER], slot);
        setup->evolutionTaskData = EvolutionScene_New(setup->party[BATTLER_PLAYER], mon, species, setup->options,
            setup->unk_164, setup->pokedex, setup->bag, setup->gameStats, condition,
            EVO_FLAG_CAN_CANCEL | EVO_FLAG_PRINT_WHAT, HEAP_ID_EVOLUTION);
        *state = STATE_EVOLUTION;
        return FALSE;
    }
    case STATE_EVOLUTION:
        if (EvolutionScene_IsDone(setup->evolutionTaskData)) {
            EvolutionScene_Delete(setup->evolutionTaskData);
            setup->evolutionTaskData = NULL;
            Heap_Destroy(HEAP_ID_EVOLUTION);
            *state = STATE_CHECK_EVOLUTION;
        }
        return FALSE;
    }
    return TRUE;
}

static BOOL Exit(OverlayManager *man, int *state) {
    (void)man;
    (void)state;
    return TRUE;
}

const OverlayManagerTemplate gGen5BattleOverlayTemplate = {
    .init = Init,
    .exec = Main,
    .exit = Exit,
    .ovy_id = FS_OVERLAY_ID_NONE,
};

// --- Starting a wild battle (the settings' debug button, the test scripts' wild) ----------------------------------

typedef struct {
    int species;
    int level;
    u32 winFlag;
} WildStart;

static BOOL Task_StartWild(TaskManager *taskManager) {
    WildStart *start = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        (*state)++;
        SetupAndStartWildBattle(taskManager, start->species, start->level, &start->winFlag, TRUE, FALSE);
        return FALSE;
    case 1:
        (*state)++;
        // The encounter leaves a loss to its caller, as a script's wild battle does (it whites out)
        if (!IsBattleResultWin(start->winFlag)) {
            CallTask_Blackout(taskManager);
            return FALSE;
        }
        return TRUE;
    default:
        return TRUE;
    }
}

BOOL Gen5Battle_StartWild(int species, int level, BOOL gen5) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->taskman != NULL) {
        return FALSE;
    }
    if (gen5) {
        if (Gen5Rom_GameName() == NULL) {
            printf("Gen 5: no Black 2 or White 2 ROM imported\n");
            return FALSE;
        }
        Gen5Battle_RequestNext();
    }
    // Static: the field's heaps are torn down while the battle runs, under the task
    static WildStart sStart;
    WildStart *start = &sStart;
    start->species = species;
    start->level = level;
    start->winFlag = 0;
    FieldSystem_CreateTask(fieldSystem, Task_StartWild, start);
    return TRUE;
}

// --- Starting a Safari Zone battle (the test scripts' safari) --------------------------------------------------------

typedef struct {
    int species;
    int level;
    int balls;
} SafariStart;

static BOOL Task_StartSafari(TaskManager *taskManager) {
    SafariStart *start = TaskManager_GetEnvironment(taskManager);
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    SaveData *saveData = fieldSystem->saveData;

    // As the Safari Zone's grass does: the Safari flag on (the encounter is Task_SafariEncounter), its ball counter, and
    // the gate to go back to when the balls run out (here: where the player stands)
    Save_VarsFlags_SetSafariSysFlag(Save_VarsFlags_Get(saveData));
    *LocalFieldData_GetSafariBallsCounter(Save_LocalFieldData_Get(saveData)) = start->balls;
    Location here = *fieldSystem->location;
    here.warpId = -1;
    here.x = PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
    here.y = PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
    LocalFieldData_SetDynamicWarp(Save_LocalFieldData_Get(saveData), &here);
    BattleSetup *setup = BattleSetup_New_SafariZone(HEAP_ID_FIELD2, start->balls);
    BattleSetup_InitFromFieldSystem(setup, fieldSystem);
    FieldSystem_GenerateSingleWildPokemon(fieldSystem, start->species, start->level, FALSE, setup);
    FieldSystem_StartForcedWildBattle(fieldSystem, taskManager, setup);
    return FALSE;
}

BOOL Gen5Battle_StartSafari(int species, int level, int balls) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->taskman != NULL) {
        return FALSE;
    }
    static SafariStart sStart;
    sStart.species = species;
    sStart.level = level;
    sStart.balls = balls;
    FieldSystem_CreateTask(fieldSystem, Task_StartSafari, &sStart);
    return TRUE;
}

// --- Starting a trainer battle (the test scripts' trainer) ---------------------------------------------------------

typedef struct {
    u32 trainer1;
    u32 trainer2;
    u32 winFlag;
} TrainerStart;

static BOOL Task_StartTrainer(TaskManager *taskManager) {
    TrainerStart *start = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        (*state)++;
        SetupAndStartTrainerBattle(taskManager, start->trainer1, start->trainer2, 0, 0, 0, HEAP_ID_FIELD2, &start->winFlag);
        return FALSE;
    case 1:
        printf("Gen 5: trainer battle %u over, win flag %u\n", (unsigned)start->trainer1, (unsigned)start->winFlag);
        (*state)++;
        // A loss: the trainer's script whites the player out (whiteout, scrcmd_battle.c), the encounter doesn't
        if (!IsBattleResultWin(start->winFlag)) {
            CallTask_Blackout(taskManager);
            return FALSE;
        }
        return TRUE;
    default:
        return TRUE;
    }
}

BOOL Gen5Battle_StartTrainer(u32 trainer1, u32 trainer2) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->taskman != NULL) {
        return FALSE;
    }
    // Static: the field's heaps are torn down while the battle runs, under the task
    static TrainerStart sStart;
    TrainerStart *start = &sStart;
    start->trainer1 = trainer1;
    start->trainer2 = trainer2;
    start->winFlag = 0;
    FieldSystem_CreateTask(fieldSystem, Task_StartTrainer, start);
    return TRUE;
}

BOOL Gen5BattleUI_StartWild(int species, int level) {
    return Gen5Battle_StartWild(species, level, TRUE);
}

#endif
