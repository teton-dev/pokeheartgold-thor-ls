#ifndef POKEHEARTGOLD_PORT_SLOT_LOGIC_H
#define POKEHEARTGOLD_PORT_SLOT_LOGIC_H

// The Japanese HeartGold/SoulSilver slot machine's rules (overlay 77), rewritten in C.
// Read from the JP ROM; see docs/thor/dev/hgss-game-corner.md in the Platinum fork.
// No ROM data is in this file: the odds and payout tables are read from the player's own JP overlay (SlotTables).
// Standalone (no SDK headers) so it can be tested on the host.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The JP overlay 77's bytes (as loaded at 0x021E4E40); tables are read from them by their JP addresses
#define SLOT_OVERLAY_BASE 0x021E4E40u
typedef struct SlotTables {
    const uint8_t *data;
    uint32_t size;
} SlotTables;

// Flag word bits (the JP work struct's +0x6C)
#define SLOT_FLAG_BONUS     (1u << 0) // in the bonus game
#define SLOT_FLAG_REACH     (1u << 4)
#define SLOT_FLAG_HOT       (1u << 6)
#define SLOT_FLAG_JACKSYM   (1u << 7) // the drawn symbol is a jackpot symbol

typedef struct SlotGame {
    const SlotTables *tables;
    uint32_t rng; // the DS's linear congruential generator
    uint32_t coins; // cap 50000
    uint8_t type; // 0 to 3: which odds table (lucky machines are 1 to 3)
    uint8_t bet; // 1 or 3
    uint8_t betIndex; // 0 to 2
    uint8_t machineId; // JP +0xA0: the bonus machine's Pokemon
    uint8_t symbol; // 0 to 6
    uint8_t outcome; // 3 = no payout
    uint8_t outcomeClass; // 0 miss, 1 hit, 2 big hit
    uint8_t reach; // JP +0xA2
    uint8_t spin, spinLimit; // bonus spin counter (8, or 15)
    uint8_t streak; // max 10
    int8_t balance; // the hidden payout balance, -99 to 99
    uint8_t reelFlags[3]; // JP +0x50
    uint8_t reelHit[3]; // JP +0x53
    uint8_t bonusPayout, bonusKind; // JP +0x4E, +0x4F
    uint16_t jackpot[2]; // cap 899
    uint32_t flags;
} SlotGame;

#define SLOT_COINS_MAX 50000

// Reel strips: 0 to 2 the normal reels (symbols 0 to 5, which are also their sprite cells), 3 to 5 the bonus game's
// (sprite cells of the big pictures, 6 blank). 21 places each
#define SLOT_REEL_LENGTH 21
uint8_t Slot_StripSymbol(const SlotTables *tables, int strip, int index);

void Slot_Init(SlotGame *g, const SlotTables *tables, uint32_t seed, uint8_t type, uint32_t coins);
uint16_t Slot_Rand(SlotGame *g, uint16_t n); // 0 to n - 1
int Slot_IsJackpotSymbol(uint8_t symbol);

// The base game: draw the symbol (0 to 6) and how it is to turn out (class 0, 1, 2)
void Slot_DrawSymbol(SlotGame *g);
// The bonus game's draw (JP 0x021E6CD8)
void Slot_DrawBonusSpin(SlotGame *g);
// A bonus roll: 0 to 2, or 3 for none; adds to the jackpot counters (JP 0x021E6E48)
int Slot_BonusRoll(SlotGame *g);
// The hot/cold payout decision: sets g->outcome, returns the bonus level (JP 0x021E6EE0)
int Slot_PayoutDecision(SlotGame *g);
// Coins for a winning symbol (the JP's table {0, 0, 15, 10, 2, 100, 100}) at a bet
int Slot_SymbolPayout(uint8_t symbol, uint8_t bet);
// Add coins, capped
void Slot_AddCoins(SlotGame *g, uint32_t amount);

#ifdef __cplusplus
}
#endif

#endif
