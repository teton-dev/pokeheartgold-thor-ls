#include "slot_logic.h"

#include <stddef.h>

// JP addresses of the tables, in overlay 77
#define TBL_ODDS        0x021EA284u // 4 types x 3 bets x 7 u16 weights (sum 1000), from the top of 0..999
#define TBL_CLASS       0x021EA204u // 7 symbols x 3 bets x 3 u16
#define TBL_BONUS_ROLL  0x021EA154u // 3 bytes per machine
#define TBL_JACKPOT_ADD 0x021EA11Cu // 2 bytes per type
#define TBL_HOLD        0x021EA114u // per bet
#define TBL_BAL_STEP    0x021EA108u // per bet
#define TBL_LEVEL       0x021EA130u // 3 bytes x (reach + 2 * reach flag)
#define TBL_KEEP        0x021EA16Cu // per (reach flag, type, bet)
#define TBL_MISS        0x021EA184u // per (bet, type) 3 bytes
#define TBL_RETURN      0x021EA124u // per (bet, outcome)
#define TBL_LINES       0x021EA0E8u // 5 lines x 3 (reel, row)
#define TBL_PAYOUT      0x021EA098u // coins per symbol 0 to 6
#define TBL_STRIPS      0x021EA32Cu // 6 strips x 21 u32
#define TBL_ASSIST      0x021EA1A8u // bytes: 15 per streak (0 to 5), 5 per phase
#define JACKPOT_CAP     899

static uint8_t T8(const SlotGame *g, uint32_t addr) {
    return g->tables->data[addr - SLOT_OVERLAY_BASE];
}

static uint16_t T16(const SlotGame *g, uint32_t addr) {
    const uint8_t *p = &g->tables->data[addr - SLOT_OVERLAY_BASE];
    return (uint16_t)(p[0] | p[1] << 8);
}

uint8_t Slot_StripSymbol(const SlotTables *tables, int strip, int index) {
    index %= SLOT_REEL_LENGTH;
    if (index < 0) {
        index += SLOT_REEL_LENGTH;
    }
    uint32_t at = TBL_STRIPS - SLOT_OVERLAY_BASE + (uint32_t)(strip * SLOT_REEL_LENGTH + index) * 4;
    return at < tables->size ? tables->data[at] : 0;
}

void Slot_Init(SlotGame *g, const SlotTables *tables, uint32_t seed, uint8_t type, uint32_t coins) {
    uint8_t *p = (uint8_t *)g;
    for (unsigned i = 0; i < sizeof(*g); i++) {
        p[i] = 0;
    }
    g->tables = tables;
    g->rng = seed;
    g->type = type > 3 ? 0 : type;
    g->coins = coins > SLOT_COINS_MAX ? SLOT_COINS_MAX : coins;
    g->bet = 1;
    g->outcome = 3;
}

uint16_t Slot_Rand(SlotGame *g, uint16_t n) {
    g->rng = g->rng * 0x41C64E6Du + 0x6073u;
    return (uint16_t)((g->rng >> 16) % n);
}

int Slot_IsJackpotSymbol(uint8_t symbol) {
    return symbol >= 5;
}

void Slot_DrawSymbol(SlotGame *g) {
    uint32_t row = TBL_ODDS + 0x2A * g->type + 0x0E * g->betIndex;
    uint16_t r = Slot_Rand(g, 1000);
    if (!(g->flags & SLOT_FLAG_BONUS)) {
        int acc = 1000;
        g->symbol = 0;
        for (int i = 0; i < 7; i++) {
            acc -= T16(g, row + 2 * i);
            if (r >= acc) {
                g->symbol = (uint8_t)i;
                break;
            }
        }
    }
    if (Slot_IsJackpotSymbol(g->symbol)) {
        g->flags |= SLOT_FLAG_JACKSYM;
    }
    r = Slot_Rand(g, 1000);
    uint32_t p = TBL_CLASS + 0x12 * g->symbol + 6 * g->betIndex;
    if (g->symbol >= 4) {
        g->outcomeClass = r >= 1000 - T16(g, p) ? 0 : 1;
    } else {
        int a = T16(g, p), b = T16(g, p + 2);
        if (r >= 1000 - a) {
            g->outcomeClass = 0;
        } else if (r >= 1000 - (a + b)) {
            g->outcomeClass = 1;
        } else {
            g->outcomeClass = 2;
        }
    }
}

void Slot_DrawBonusSpin(SlotGame *g) {
    int level = g->streak > 5 ? 5 : g->streak;
    int phase;
    if (g->spinLimit == 15) {
        phase = g->spin < 6 ? 0 : (g->spin < 11 ? 1 : 2);
    } else {
        phase = g->spin < 4 ? 0 : (g->spin < 6 ? 1 : 2);
    }
    uint32_t b = TBL_ASSIST + 15 * level + 5 * phase;
    g->symbol = (uint8_t)Slot_Rand(g, 3);
    for (int i = 0; i < 3; i++) {
        g->reelHit[i] = Slot_Rand(g, 100) < T8(g, b + 4) ? 0 : 1;
    }
    int r = Slot_Rand(g, 100);
    if (r < T8(g, b + 2)) {
        g->bonusPayout = 8;
        g->bonusKind = 0;
    } else if (r < T8(g, b + 2) + T8(g, b + 3)) {
        g->bonusPayout = 12;
        g->bonusKind = 1;
    } else {
        g->bonusPayout = 24;
        g->bonusKind = 2;
    }
    r = Slot_Rand(g, 100);
    if (r < T8(g, b)) {
        int pick = Slot_Rand(g, 3);
        for (int i = 0; i < 3; i++) {
            g->reelFlags[i] = i == pick ? 0 : 1;
        }
    } else if (r < T8(g, b) + T8(g, b + 1)) {
        int pick = Slot_Rand(g, 3);
        for (int i = 0; i < 3; i++) {
            g->reelFlags[i] = i == pick ? 1 : 0;
        }
    } else {
        g->reelFlags[0] = g->reelFlags[1] = g->reelFlags[2] = 0;
    }
}

int Slot_BonusRoll(SlotGame *g) {
    int r = Slot_Rand(g, 100);
    int result = 3, t = 100;
    for (int k = 0; k < 3; k++) {
        t -= T8(g, TBL_BONUS_ROLL + 3 * g->machineId + k);
        if (r >= t) {
            result = k;
            break;
        }
    }
    if (Slot_IsJackpotSymbol(g->machineId) || result == 0) {
        return result;
    }
    for (int i = 0; i < 2; i++) {
        uint32_t v = g->jackpot[i] + T8(g, TBL_JACKPOT_ADD + 2 * g->type + i) * result;
        g->jackpot[i] = v > JACKPOT_CAP ? JACKPOT_CAP : (uint16_t)v;
    }
    return result;
}

int Slot_PayoutDecision(SlotGame *g) {
    int level = 3;
    g->outcome = 3;
    if (g->reach == 2 || g->balance <= 0) {
        if (!(g->flags & SLOT_FLAG_BONUS) && g->balance <= 0) {
            g->balance = (int8_t)(g->balance + T8(g, TBL_HOLD + g->betIndex));
        }
        return 3;
    }
    int reachFlag = (g->flags & SLOT_FLAG_REACH) ? 1 : 0;
    int r = Slot_Rand(g, 100);
    int t = 100;
    uint32_t row = TBL_LEVEL + 3 * (g->reach + 2 * reachFlag);
    for (int k = 0; k < 3; k++) {
        t -= T8(g, row + k);
        if (r >= t) {
            level = k;
            break;
        }
    }
    int bal = g->balance + T8(g, TBL_BAL_STEP + g->betIndex) * level;
    g->balance = (int8_t)(bal > 99 ? 99 : bal);
    if (g->balance <= 0) {
        return level;
    }
    int keep = Slot_Rand(g, 100);
    int limit = (T8(g, TBL_KEEP + 12 * reachFlag + 3 * g->type + g->betIndex) * g->balance) / 15;
    if (keep >= limit) {
        return level;
    }
    r = Slot_Rand(g, 100);
    g->outcome = 2;
    t = 100;
    for (int k = 0; k < 3; k++) {
        t -= T8(g, TBL_MISS + 12 * g->betIndex + 3 * g->type + k);
        if (r >= t) {
            g->outcome = (uint8_t)k;
            break;
        }
    }
    g->balance = (int8_t)((-g->balance * T8(g, TBL_RETURN + 3 * g->betIndex + g->outcome)) / 10);
    g->streak = 0;
    g->flags &= ~SLOT_FLAG_HOT;
    return level;
}

int Slot_SymbolPayout(uint8_t symbol, uint8_t bet) {
    static const uint8_t payout[7] = { 0, 0, 15, 10, 2, 100, 100 };
    (void)bet;
    return symbol < 7 ? payout[symbol] : 0;
}

void Slot_AddCoins(SlotGame *g, uint32_t amount) {
    g->coins += amount;
    if (g->coins > SLOT_COINS_MAX) {
        g->coins = SLOT_COINS_MAX;
    }
}

uint8_t Slot_ReelCell(const SlotGame *g, int reel, int row) {
    return Slot_StripSymbol(g->tables, reel, g->reelStop[reel] + row);
}

// Does symbol (1 to 6) win with the stopped reels: all of them on a line show it (only reel 1 for berries); with
// partial, whether the stopped ones still could
static int Slot_LineShows(const SlotGame *g, int symbol, int line, int partial) {
    const uint8_t cell = (uint8_t)(symbol - 1);
    const int reels = symbol == 4 ? 1 : 3;
    if (symbol == 4 && line > 2) {
        return 0;
    }
    for (int i = 0; i < reels; i++) {
        const int reel = T8(g, TBL_LINES + 6 * line + 2 * i), row = T8(g, TBL_LINES + 6 * line + 2 * i + 1);
        if (!g->reelStopped[reel]) {
            if (partial) {
                continue;
            }
            return 0;
        }
        if (Slot_ReelCell(g, reel, row) != cell) {
            return 0;
        }
    }
    return 1;
}

int Slot_Judge(const SlotGame *g, int *line) {
    for (int symbol = 1; symbol <= 6; symbol++) {
        for (int l = 0; l < 5; l++) {
            if (Slot_LineShows(g, symbol, l, 0)) {
                if (line != NULL) {
                    *line = l;
                }
                return symbol;
            }
        }
    }
    return 0;
}

// With the reels stopped so far: could the drawn symbol still win, and has anything else won already
static int Slot_StopKeepsTarget(const SlotGame *g) {
    if (g->symbol == 0 || g->symbol > 6) {
        return 1;
    }
    for (int l = 0; l < 5; l++) {
        if (Slot_LineShows(g, g->symbol, l, 1)) {
            return 1;
        }
    }
    return 0;
}

static int Slot_StopAvoidsOthers(const SlotGame *g) {
    for (int symbol = 1; symbol <= 6; symbol++) {
        if (symbol == g->symbol) {
            continue;
        }
        for (int l = 0; l < 5; l++) {
            // A complete line, or berries on reel 1 (which win on their own)
            const int complete = symbol == 4 ? g->reelStopped[0] : (g->reelStopped[0] && g->reelStopped[1] && g->reelStopped[2]);
            if (complete && Slot_LineShows(g, symbol, l, 0)) {
                return 0;
            }
        }
    }
    return 1;
}

int Slot_ChooseStop(SlotGame *g, int reel, int pressed) {
    int fallback = -1;
    g->reelStopped[reel] = 1;
    for (int slip = 0; slip <= 4; slip++) {
        int index = (pressed - slip) % SLOT_REEL_LENGTH;
        if (index < 0) {
            index += SLOT_REEL_LENGTH;
        }
        g->reelStop[reel] = (uint8_t)index;
        if (!Slot_StopAvoidsOthers(g)) {
            continue;
        }
        if (Slot_StopKeepsTarget(g)) {
            return index;
        }
        if (fallback < 0) {
            fallback = index;
        }
    }
    if (fallback < 0) {
        fallback = ((pressed % SLOT_REEL_LENGTH) + SLOT_REEL_LENGTH) % SLOT_REEL_LENGTH;
    }
    g->reelStop[reel] = (uint8_t)fallback;
    return fallback;
}

int Slot_WinPayout(const SlotGame *g, uint8_t symbol) {
    if (symbol > 6) {
        return 0;
    }
    int coins = T8(g, TBL_PAYOUT + symbol);
    if (Slot_IsJackpotSymbol(symbol)) {
        coins += g->jackpot[symbol - 5];
    }
    return coins;
}
