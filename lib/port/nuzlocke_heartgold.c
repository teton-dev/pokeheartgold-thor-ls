// HGSS's side of Nuzlocke mode (port_nuzlocke_mode_game.h): the running save, where the records go in the save
// image, and the dead flag on a Pokemon. The Platinum fork's src/port/nuzlocke_platinum.c, against HGSS's save
// (src/save.c) and bag (bag.h) APIs, which are named differently from Platinum's fork.
#include <nitro.h>

#include "constants/charcode.h"
#include "constants/items.h"

#include "field_system.h"
#include "bag.h"
#include "item.h"
#include "player_data.h"
#include "pokemon.h"
#include "port_nuzlocke_mode.h"
#include "port_nuzlocke_mode_game.h"
#include "save.h"
#include "system.h"

// src/save.c (SDK_PORT): the same raw flash primitives Platinum's SaveData_CardLoad/CardSave are, under HGSS's own
// names (lib/port/save_migrate_v1.c declares them the same way; there's no shared header for them).
BOOL PortSave_FlashLoad(u32 offset, void *data, u32 size);
s32 PortSave_FlashWrite(u32 offset, void *data, u32 size);

// Each half of the save image is SAVE_PAGE_MAX (35) sectors of game data, then the extra chunks
// (gExtraSaveChunkHeaders, src/save_arrays.c: the Hall of Fame and battle records, through sector 45 or so) and 64
// sectors total; the backup half starts 64 sectors after the primary (src/save.c's FlashWriteChunk calls, "+ 64").
// The records take the last four of the primary half, well clear of the extra chunks.
#define RECORD_SECTOR_FIRST 60
#define RECORD_SECTOR_COUNT 4
#define BACKUP_SECTOR_OFFSET 64

// Pokemon data's block B, 0x1A (MON_DATA_UNUSED_114): unused in Platinum and HGSS. Bit 0 is the
// Nuzlocke dead flag; the other bits are free.
#define PORT_MON_FLAGS MON_DATA_UNUSED_114
#define PORT_MON_FLAG_NUZLOCKE_DEAD 0x0001

BOOL NuzlockeModeGame_RunningSave(u32 *saveId, int *slot)
{
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return FALSE;
    }
    PlayerProfile *profile = Save_PlayerData_GetProfile(fieldSystem->saveData);
    *saveId = PlayerProfile_GetTrainerID(profile);
    *slot = 0; // HGSS has no save-slot feature yet
    return TRUE;
}

void NuzlockeModeGame_RecordSectors(u32 *primary, u32 *backup, int *count, u32 *sectorSize)
{
    *primary = RECORD_SECTOR_FIRST;
    *backup = RECORD_SECTOR_FIRST + BACKUP_SECTOR_OFFSET;
    *count = RECORD_SECTOR_COUNT;
    *sectorSize = SAVE_SECTOR_SIZE;
}

BOOL NuzlockeModeGame_CanWrite(void)
{
    // Set while the game's own save (or record mixing) is writing flash
    return !gSystem.softResetDisabled;
}

BOOL NuzlockeModeGame_HasPokeBalls(void)
{
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return FALSE;
    }
    Bag *bag = Save_Bag_Get(fieldSystem->saveData);
    for (u32 slot = 0;; slot++) {
        ItemSlot *item = Bag_GetPocketSlotN(bag, POCKET_BALLS, slot);
        if (item == NULL) {
            return FALSE;
        }
        if (item->id != ITEM_NONE && item->quantity > 0) {
            return TRUE;
        }
    }
}

BOOL NuzlockeModeGame_ReadFlash(u32 address, void *data, u32 size)
{
    return PortSave_FlashLoad(address, data, size);
}

BOOL NuzlockeModeGame_WriteFlash(u32 address, void *data, u32 size)
{
    return PortSave_FlashWrite(address, data, size) != 0;
}

BOOL NuzlockeMode_BoxIsDead(BoxPokemon *boxMon)
{
    return (GetBoxMonData(boxMon, PORT_MON_FLAGS, NULL) & PORT_MON_FLAG_NUZLOCKE_DEAD) != 0;
}

BOOL NuzlockeMode_IsDead(Pokemon *mon)
{
    return (GetMonData(mon, PORT_MON_FLAGS, NULL) & PORT_MON_FLAG_NUZLOCKE_DEAD) != 0;
}

void NuzlockeMode_SetDead(Pokemon *mon, BOOL dead)
{
    u16 flags = (u16)GetMonData(mon, PORT_MON_FLAGS, NULL);
    flags = dead ? (u16)(flags | PORT_MON_FLAG_NUZLOCKE_DEAD) : (u16)(flags & ~PORT_MON_FLAG_NUZLOCKE_DEAD);
    SetMonData(mon, PORT_MON_FLAGS, &flags);
}

// --- the rules -------------------------------------------------------------------------

int Gear_LevelCap(void);
int Gear_LevelCapLow(void);

// The next boss's ace (Hard: its weakest Pokemon) from the Gear's boss table (gear_hgss.c); 0 when there is none
int NuzlockeMode_LevelCap(void)
{
    if (!NuzlockeMode_On()) {
        return 0;
    }
    const int cap = NuzlockeMode_Level() == NUZLOCKE_HARD ? Gear_LevelCapLow() : Gear_LevelCap();
    return cap > 0 && cap <= MAX_LEVEL ? cap : 0;
}

static BOOL IsRealMon(Pokemon *mon)
{
    return GetMonData(mon, MON_DATA_SPECIES_EXISTS, NULL) && !GetMonData(mon, MON_DATA_IS_EGG, NULL);
}

u32 NuzlockeMode_FilterHP(Pokemon *mon, u32 hp)
{
    if (hp == 0 || !NuzlockeMode_On()) {
        return hp;
    }
    return NuzlockeMode_IsDead(mon) ? 0 : hp;
}

void NuzlockeMode_OnHPSet(Pokemon *mon, u32 hp)
{
    // Fainting: HP set to 0 on a Pokemon with stats (a new one's HP is set by its stats, never to 0)
    if (hp != 0 || !NuzlockeMode_On() || GetMonData(mon, MON_DATA_MAX_HP, NULL) == 0 || !IsRealMon(mon)
        || NuzlockeMode_IsDead(mon)) {
        return;
    }
    NuzlockeMode_SetDead(mon, TRUE);
}

static BOOL sNicknameOffer;

// "nickname" in HGSS's character codes (the message's text is searched as it is read from the ROM)
void NuzlockeMode_NoteScriptMessage(const u16 *text)
{
    static const u16 kWord[] = { CHAR_i, CHAR_c, CHAR_k, CHAR_n, CHAR_a, CHAR_m, CHAR_e }; // [N]ickname
    sNicknameOffer = FALSE;
    for (const u16 *c = text; *c != EOS && !sNicknameOffer; c++) {
        int k = 0;
        while (k < (int)NELEMS(kWord) && c[k] == kWord[k]) {
            k++;
        }
        sNicknameOffer = k == (int)NELEMS(kWord);
    }
}

BOOL NuzlockeMode_NicknameOfferPending(void)
{
    return sNicknameOffer;
}

BOOL NuzlockeMode_RefusesItem(Pokemon *mon, u16 item, int heapID)
{
    if (!NuzlockeMode_On()) {
        return FALSE;
    }
    if (NuzlockeMode_IsDead(mon)) {
        return TRUE;
    }
    const int cap = NuzlockeMode_LevelCap();
    return cap > 0 && GetItemAttr(item, ITEMATTR_LEVEL_UP, (enum HeapID)heapID) && (int)GetMonData(mon, MON_DATA_LEVEL, NULL) >= cap;
}
