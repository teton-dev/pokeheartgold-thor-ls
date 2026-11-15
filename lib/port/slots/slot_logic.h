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
    uint8_t betIndex; // JP +0x39: the machine's mode, 0 to 2 (the odds tables' row), which the payout decision moves
    uint8_t reelStop[3]; // each reel's strip index at row 0 (rows 1 to 3 are the window: top, middle, bottom)
    uint8_t reelStopped[3];
    uint8_t winSymbol, winLine; // the last spin's result: 0 none
    uint8_t machineId; // JP +0xA0: the bonus machine's Pokemon
    uint8_t symbol; // 0 to 6
    uint8_t outcome; // 3 = no payout
    uint8_t outcomeClass; // 0 miss, 1 hit, 2 big hit
    uint8_t reach; // JP +0xA2
    uint8_t spin, spinLimit; // bonus spin counter (8, or 15)
    uint8_t streak; // max 10
    int8_t balance; // the hidden payout balance, -99 to 99
    uint8_t reelFlags[3]; // JP +0x50: in the bonus game, the reels that slip onto the picture (the rest stop where pressed)
    uint8_t reelHit[3]; // JP +0x53 (the reel's row rounding while it moves; not used by the port)
    uint8_t reelSpeed, speedTier; // JP +0x4E, +0x4F: the bonus game's reel speed, 8, 12 or 24 pixels a frame
    uint8_t bonusWins; // JP +0x40: pictures lined up this bonus game
    uint8_t bonusCoins; // JP +0x42: what this bonus spin pays if its picture lines up
    uint16_t bonusTotal; // JP +0x4A: coins won in this bonus game (max 999)
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
// The bonus game (JP 0x021E6A58): starts after a Poke Ball or 7 win (winSymbol 5 or 6: 8 or 15 spins of 1 coin),
// ends with the machine's mode back to 1 and its balance set back
void Slot_StartBonus(SlotGame *g, uint8_t winSymbol);
void Slot_EndBonus(SlotGame *g);
int Slot_InBonus(const SlotGame *g);
// A bonus spin's stop (JP 0x021E9714): the reels in reelFlags stop by themselves on the drawn picture's column; the
// player's slip onto it when it is within 2, 3 or 10 symbols (by the reel speed), else stop as pressed (the last one
// slipping 1 or 2 if another picture would line up)
int Slot_ChooseBonusStop(SlotGame *g, int reel, int pressed);
// -1: the strip index falls as the reel turns (the symbols move down); 1: it rises (a bonus reel turning the other way)
int Slot_ReelDirection(const SlotGame *g, int reel);
// The bonus spin's result (JP 0x021E8EF8): 1 if the drawn picture's middle row is on the middle line; pays and counts
int Slot_JudgeBonus(SlotGame *g);

// A bonus roll: 0 to 2, or 3 for none; adds to the jackpot counters (JP 0x021E6E48)
int Slot_BonusRoll(SlotGame *g);
// The hot/cold payout decision: sets g->outcome, returns the bonus level (JP 0x021E6EE0)
int Slot_PayoutDecision(SlotGame *g);
// The symbol (sprite cell, 0 to 5) a reel shows at a row (0 to 4)
uint8_t Slot_ReelCell(const SlotGame *g, int reel, int row);
// Where a reel stops when the player presses its stop at strip index pressed: up to 4 symbols further on (the reel
// moves to lower indices), so that the drawn symbol lines up when it can and nothing else wins (JP 0x021E95A4 and
// the functions after it do this with per-symbol rules not read yet; this follows the same aim)
int Slot_ChooseStop(SlotGame *g, int reel, int pressed);
// The result once the reels stand: the first of symbols 1 to 6 on one of the 5 lines (berries: on reel 1 of the
// top, middle or bottom line), as JP 0x021E8E48 checks them; 0 for none. *line: the line
int Slot_Judge(const SlotGame *g, int *line);
// Coins for a win in the base game (JP 0x021E7198): the table, plus the jackpot counter for symbols 5 and 6
int Slot_WinPayout(const SlotGame *g, uint8_t symbol);

// Coins for a winning symbol (the JP's table {0, 0, 15, 10, 2, 100, 100}) at a bet
int Slot_SymbolPayout(uint8_t symbol, uint8_t bet);
// Add coins, capped
void Slot_AddCoins(SlotGame *g, uint32_t amount);

#ifdef __cplusplus
}
#endif

#endif
