// The Japanese Game Corners (teton-dev fork; docs/thor/dev/hgss-game-corner.md in the Platinum fork): the US ROM still
// has the Japanese games' Game Corner maps (MAP_GOLDENROD_GAME_CORNER_JP, MAP_CELADON_GAME_CORNER_JP), unused, with
// their slot machine scripts but no English text. With Settings > Game Corner > Japanese Game Corner on:
// - the doors into the US Game Corners lead to the Japanese ones (SlotMaps_RedirectWarps, from map_events.c);
// - their text is the English below, translated from the Japanese game's (the US ROM's rows are empty);
// - their coin clerk gives a Coin Case and 50 Coins to a player without one (SlotMaps_PatchScripts, from
//   script_manager.c): the Japanese game had the Coin Case lying in Goldenrod Tunnel B1F, a ball the US game removed
//   (giving it from Mr. Game in the US Game Corner instead), so a player sent to these maps would have no way to get one.
#include "port_slot_maps.h"

#include <string.h>

#include "global.h"

#include "constants/items.h"
#include "constants/maps.h"

#include "msgdata/msg.naix"

#include "heap.h"
#include "port_slot_app.h"

// --- Warps --------------------------------------------------------------------------------------------------------

void SlotMaps_RedirectWarps(WarpEvent *warps, u32 count) {
    if (warps == NULL || !SlotApp_IsEnabled()) {
        return;
    }
    for (u32 i = 0; i < count; i++) {
        // Both Japanese maps have their one warp, back out, at index 0, as the US ones do
        if (warps[i].header == MAP_GOLDENROD_GAME_CORNER) {
            warps[i].header = MAP_GOLDENROD_GAME_CORNER_JP;
        } else if (warps[i].header == MAP_CELADON_GAME_CORNER) {
            warps[i].header = MAP_CELADON_GAME_CORNER_JP;
        }
    }
}

// --- Text ---------------------------------------------------------------------------------------------------------

// Written as UTF-8 with the message escapes: \n a new line, \r the next box, \f the box scrolled on, {YESNO} and
// {CMD hhhh p...} the game's control codes (0xFFFE, code, parameter count, parameters), as the Japanese text has them
// ({CMD 0203 90} the text at x 90, {CMD 0103 0} the player's name, {CMD 0108 0} an item, {CMD 0100 1} a prize, {CMD 0112 1} a pocket).

static const char *const sGoldenrodText[] = {
    /* 00 */ "Welcome! This is the Game Corner!\r",
    /* 01 */ "Do you need Coins for the games?\rHuh? You don't have a\nCoin Case to keep them in!",
    /* 02 */ "Do you need Coins for the games?\rIt's $1,000 for 50 Coins!\nWould you like some Coins?{YESNO}",
    /* 03 */ "Here you go! Thank you!\nHere are your Coins!{YESNO}",
    /* 04 */ "You don't have enough money!",
    /* 05 */ "Whoops…! Your Coin Case is full!",
    /* 06 */ "…You don't want any? Well, come again!",
    /* 07 */ "Welcome!\rWe exchange the Coins you win\nin the games here for\fthe prize of your choice!{YESNO}",
    /* 08 */ "Oh, I see. Come back when\nyou've won more Coins!",
    /* 09 */ "Oh, you don't have enough Coins.{YESNO}",
    /* 10 */ "{CMD 0108 0}.\nIs that right?{YESNO}",
    /* 11 */ "Here you go!\r",
    /* 12 */ "It looks like your Bag is full.\r",
    /* 13 */ "You can't take any\nmore Pokémon with you.\rPlease make room in your\nparty and come back.",
    /* 14 */ "You'd like {CMD 0100 1}?{YESNO}",
    /* 15 */ "{CMD 0103 0} received\n{CMD 0100 1}!{YESNO}",
    /* 16 */ "Welcome!\rWe exchange the Coins you win\nin the games here for\fthe Pokémon of your choice!{YESNO}",
    /* 17 */ "You put away the {CMD 0108 0}\nin the {CMD 0112 1} Pocket.{YESNO}",
    /* 18 */ "I always play this slot machine.\nSomehow it feels like it pays\fout more than the others.",
    /* 19 */ "How about that? Amazing,\nhuh?! Every slot machine in\fhere is the latest model!\rYou won't find a Game Corner\nthis fine anywhere in Kanto!",
    /* 20 */ "Life's all about the big bet!\nI'll spin and spin and spin!",
    /* 21 */ "The reels on these slots\nalways stop from the left.\rThe reel you can stop\nnext has its lamp blinking.",
    /* 22 */ "I'm not quitting until I win!",
    /* 23 */ "I taught my Pokémon Ice Beam!\rSaving up the Coins was\ntough, but it was worth it.",
    /* 24 */ "No matter how long I stick at\nit, I never win a single thing!\rI got so mad, I threw my Coin Case\naway in the Goldenrod Underground!",
    /* 25 */ "You can't play without a Coin Case!",
    /* 26 */ "You don't have enough Coins to play!",
    /* 27 */ "Want to hear a long, geeky\ntalk about the slots?{YESNO}",
    /* 28 */ "Want to hear even more\nlong, geeky talk?{YESNO}",
    /* 29 */ "If you ever want to hear\nthe long talk, come back!",
    /* 30 */ "Was my long, geeky talk any help?",
    /* 31 */ "The trick to the slots is to\nwatch how the reels move.\rRead the reels ahead and\npress just a little early, and\fthey're easier to line up.\rThe 7 really stands out, so\nit's a good one to aim for.",
    /* 32 */ "When you line up Pikachu,\nMarill, Apricorn or Replay,\rkeep a close eye on the\nSmeargle in the monitor.\rIf Smeargle is pleased, you get a few\nmore Coins when you line up 7 or Ball!\rOnce the reels line up, keep\nyour eyes on Smeargle!",
    /* 33 */ "The slot game has three modes\nthat show how Smeargle is feeling.\rThe first is Normal Mode.\rWhen you start playing, you\nalways start in this mode.\rThe second is Slump Mode.\rWhen the scenery around\nSmeargle turns blue, that's\fthe sign of Slump Mode.\r7 and Ball are hard to line up,\rbut Pikachu, Apricorn, Marill\nand Replay are easy to line up.\rThe third is Happy Mode.\rWhen the scenery around\nSmeargle turns red, that's\fthe sign of Happy Mode.\r7 and Ball pay lots of Coins,\nand they're easy to line up,\fso that's your chance!!\rOnce you know the three\nmodes, you'll win a lot more,\fso keep a sharp eye out!",
    /* 34 */ "When Smeargle shows an angry\nface, it's in a bad mood.\rIf you miss a reach on 7 or Ball,\rits anger might explode\nand switch the mode!\rThe more angry faces it shows,\nthe sooner it seems to explode.\rWhen the mode switches, Smeargle\ncheers right up and smiles all over,\rso the mode won't\nswitch again for a while.\rIts mood slowly sinks while the\nreels don't line up, though.",
    /* 35 */ "Line up 7 or Ball and you\nget to play the Bonus Game!\rIn the Bonus Game the reels\nare easier to line up than\fusual, so you win lots of Coins!\rAnd when it's over, you\nalways go into Happy Mode!\rIt's packed with chances\nto win big again and again!\rLine up lots in the Bonus\nGame and Happy Mode lasts\flonger, so give it your all!",
    /* 36 */ "Cancel",
    /* 37 */ "TMs",
    /* 38 */ "Held items",
    /* 39 */ "Substitute\n2,000 Coins",
    /* 40 */ "Swords Dance\n4,000 Coins",
    /* 41 */ "Rest\n6,000 Coins",
    /* 42 */ "Flamethrower\n10,000 Coins",
    /* 43 */ "Ice Beam\n10,000 Coins",
    /* 44 */ "Thunderbolt\n10,000 Coins",
    /* 45 */ "Silk Scarf\n1,000 Coins",
    /* 46 */ "Wide Lens\n1,000 Coins",
    /* 47 */ "Zoom Lens\n1,000 Coins",
    /* 48 */ "Metronome\n1,000 Coins",
    /* 49 */ "Abra{CMD 0203 90}200 Coins",
    /* 50 */ "Ekans{CMD 0203 90}700 Coins",
    /* 51 */ "Dratini{CMD 0203 90}2,100 Coins",
    /* 52 */ "Sandshrew{CMD 0203 90}700 Coins",
    // The port's own: the clerk's Coin Case
    /* 53 */ "Hm? You don't have a Coin\nCase? That won't do at all!\rHere, take this one.\nIt's on the house!\r",
    /* 54 */ "And here are 50 Coins\nto get you started!\rGood luck at the slots!",
};

static const char *const sCeladonText[] = {
    /* 00 */ "Welcome! This is the Game Corner!\r",
    /* 01 */ "Do you need Coins for the games?\rHuh? You don't have a\nCoin Case to keep them in!",
    /* 02 */ "Do you need Coins for the games?\rIt's $1,000 for 50 Coins!\nWould you like some Coins?{YESNO}",
    /* 03 */ "Here you go! Thank you!\nHere are your Coins!{YESNO}",
    /* 04 */ "You don't have enough money!",
    /* 05 */ "Whoops…! Your Coin Case is full!",
    /* 06 */ "…You don't want any? Well, come again!",
    /* 07 */ "Welcome!\rTrade the Coins you win in the\ngames for any prize you like\fat the exchange next door!",
    /* 08 */ "How about that? Amazing,\nhuh?! Every slot machine in\fhere is the latest model!\rYou won't find a Game Corner\nthis fine in Johto, right?",
    /* 09 */ "This is the one! This machine!\rIt hardly paid out any Coins\nyesterday, so today it's sure to pay!",
    /* 10 */ "I've got a feeling this\none's going to pay out…\rThey say some machines pay\nout a lot, and some don't.",
    /* 11 */ "Gwahahaha! The Coins just keep\ncoming! I can't stop laughing!\r…Hm? You want to play the games?\rAll right, I'll share\nsome of my luck with you!\r",
    /* 12 */ "{CMD 0103 0} received\n18 Coins!",
    /* 13 */ "Gwahahaha!\rBeing kind to people sure feels good!",
    /* 14 */ "Whoa there! You don't have\na Coin Case, do you? Then\fI can't give you any Coins.",
    /* 15 */ "What's this?! Your Coin Case is\nfull! Looks like you're on a roll!",
    /* 16 */ "Oh! A future legend!\nHere to play the slots?\rI'm after one of the prize\nPokémon myself, but the\fCoins just won't pile up!",
    /* 17 */ "Hmm… By the odds, the Pikachu line is\nthe best, but… Argh, I can't decide…!",
    /* 18 */ "A poster for a nostalgic\ngame is up on the wall…",
    /* 19 */ "Someone's saving this\nmachine by leaving a DS on it.\rThey should either play\nthe slots or play the DS!",
    /* 20 */ "There's a juice can here. Is\nsomeone saving this machine\fwith it…? Huh…? It's empty!\r",
    /* 21 */ "You can't play without a Coin Case!",
    /* 22 */ "You don't have enough Coins to play!",
    // The port's own: the clerk's Coin Case
    /* 23 */ "Hm? You don't have a Coin\nCase? That won't do at all!\rHere, take this one.\nIt's on the house!\r",
    /* 24 */ "And here are 50 Coins\nto get you started!\rGood luck at the slots!",
};

typedef struct SlotMapText {
    u16 fileId;
    const char *const *rows;
    u32 count;
} SlotMapText;

static const SlotMapText sTexts[] = {
    { NARC_msg_msg_0599_T25R1101_bin, sGoldenrodText, NELEMS(sGoldenrodText) },
    { NARC_msg_msg_0508_T07R0401_bin, sCeladonText, NELEMS(sCeladonText) },
};

#define SLOT_TEXT_MAX 1024
static u16 sEncoded[SLOT_TEXT_MAX];

static u32 DecodeUtf8(const char **text) {
    const u8 *s = (const u8 *)*text;
    u32 c = s[0];
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra > 0) {
        c &= 0x3F >> extra;
    }
    for (int i = 1; i <= extra; i++) {
        c = c << 6 | (s[i] & 0x3F);
    }
    *text += 1 + extra;
    return c;
}

// A character to the game's (charmap.txt): what this English uses
static u16 GameChar(u32 c) {
    if (c >= '0' && c <= '9') {
        return 0x121 + (c - '0');
    }
    if (c >= 'A' && c <= 'Z') {
        return 0x12B + (c - 'A');
    }
    if (c >= 'a' && c <= 'z') {
        return 0x145 + (c - 'a');
    }
    switch (c) {
    case ' ':
        return 0x1DE;
    case '!':
        return 0x1AB;
    case '?':
        return 0x1AC;
    case ',':
        return 0x1AD;
    case '.':
        return 0x1AE;
    case 0x2026: // …
        return 0x1AF;
    case '/':
        return 0x1B1;
    case '\'':
    case 0x2019:
        return 0x1B3;
    case '(':
        return 0x1B9;
    case ')':
        return 0x1BA;
    case '-':
        return 0x1BE;
    case ':':
        return 0x1C4;
    case '&':
        return 0x1C2;
    case '$':
        return 0x1A8;
    case 0xE9: // é
        return 0x188;
    case '\n':
        return 0xE000;
    case '\r':
        return 0x25BC;
    case '\f':
        return 0x25BD;
    default:
        return 0x1AC;
    }
}

static u32 Encode(const char *text, u16 *out, u32 max) {
    u32 n = 0;
    while (*text != '\0' && n + 8 < max) {
        if (strncmp(text, "{YESNO}", 7) == 0) {
            out[n++] = 0xFFFE;
            out[n++] = 0x0200;
            out[n++] = 1;
            out[n++] = 0;
            text += 7;
        } else if (strncmp(text, "{CMD ", 5) == 0) {
            text += 5;
            u32 code = 0;
            while (*text != ' ' && *text != '}') {
                const char h = *text++;
                code = code * 16 + (u32)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
            }
            u16 params[4];
            u32 count = 0;
            while (*text == ' ' && count < 4) {
                text++;
                u32 p = 0;
                while (*text >= '0' && *text <= '9') {
                    p = p * 10 + (u32)(*text++ - '0');
                }
                params[count++] = (u16)p;
            }
            if (*text == '}') {
                text++;
            }
            out[n++] = 0xFFFE;
            out[n++] = (u16)code;
            out[n++] = (u16)count;
            for (u32 i = 0; i < count; i++) {
                out[n++] = params[i];
            }
        } else {
            out[n++] = GameChar(DecodeUtf8(&text));
        }
    }
    out[n] = 0xFFFF;
    return n;
}

static const SlotMapText *FindText(u16 narcId, u16 fileId) {
    if (narcId != NARC_msgdata_msg) {
        return NULL;
    }
    for (u32 i = 0; i < NELEMS(sTexts); i++) {
        if (sTexts[i].fileId == fileId) {
            return &sTexts[i];
        }
    }
    return NULL;
}

const u16 *SlotMaps_Message(u16 narcId, u16 fileId, u32 msgNo, u32 *length) {
    const SlotMapText *text = FindText(narcId, fileId);
    if (text == NULL || msgNo >= text->count) {
        return NULL;
    }
    // One buffer: the readers copy the message out before the next one is asked for
    *length = Encode(text->rows[msgNo], sEncoded, SLOT_TEXT_MAX);
    return sEncoded;
}

u32 SlotMaps_MessageCount(u16 narcId, u16 fileId, u32 count) {
    const SlotMapText *text = FindText(narcId, fileId);
    return text != NULL ? text->count : count;
}

// --- The clerk's Coin Case ------------------------------------------------------------------------------------------

// The script commands (asm/macros/script.inc)
enum {
    CMD_END = 2,
    CMD_COMPARE_VAR_TO_VALUE = 17,
    CMD_CALL_STD = 20,
    CMD_GOTO_IF = 28,
    CMD_SET_VAR = 41,
    CMD_NPC_MSG = 45,
    CMD_WAIT_BUTTON = 50,
    CMD_CLOSE_MSG = 53,
    CMD_PLAY_SE = 73,
    CMD_LOCK_ALL = 96,
    CMD_RELEASE_ALL = 97,
    CMD_FACE_PLAYER = 104,
    CMD_GIVE_COINS = 120,
    CMD_HAS_ITEM = 128,
};

#define VAR_SPECIAL_x8004 0x8004
#define VAR_SPECIAL_x8005 0x8005
#define VAR_RESULT 0x800C
#define STD_GIVE_ITEM_VERBOSE 2033
#define SE_SELECT 1500
#define CLERK_COINS 50

typedef struct ScriptWriter {
    u8 *data;
    u32 at;
} ScriptWriter;

static void Put16(ScriptWriter *w, u16 v) {
    w->data[w->at++] = (u8)v;
    w->data[w->at++] = (u8)(v >> 8);
}

static void Put32(ScriptWriter *w, u32 v) {
    Put16(w, (u16)v);
    Put16(w, (u16)(v >> 16));
}

// The clerk's script, ahead of the map's own: without a Coin Case, the case and 50 Coins; with one, the clerk as it was
static u32 WriteClerkScript(u8 *out, u32 at, u32 original, u8 firstMessage) {
    ScriptWriter w = { out, at };
    Put16(&w, CMD_HAS_ITEM);
    Put16(&w, ITEM_COIN_CASE);
    Put16(&w, 1);
    Put16(&w, VAR_RESULT);
    Put16(&w, CMD_COMPARE_VAR_TO_VALUE);
    Put16(&w, VAR_RESULT);
    Put16(&w, 1);
    Put16(&w, CMD_GOTO_IF);
    w.data[w.at++] = 1; // equal
    Put32(&w, original - (w.at + 4));
    Put16(&w, CMD_PLAY_SE);
    Put16(&w, SE_SELECT);
    Put16(&w, CMD_LOCK_ALL);
    Put16(&w, CMD_FACE_PLAYER);
    Put16(&w, CMD_NPC_MSG);
    w.data[w.at++] = firstMessage;
    Put16(&w, CMD_SET_VAR); // GiveItemNoCheck ITEM_COIN_CASE, 1
    Put16(&w, VAR_SPECIAL_x8004);
    Put16(&w, ITEM_COIN_CASE);
    Put16(&w, CMD_SET_VAR);
    Put16(&w, VAR_SPECIAL_x8005);
    Put16(&w, 1);
    Put16(&w, CMD_CALL_STD);
    Put16(&w, STD_GIVE_ITEM_VERBOSE);
    Put16(&w, CMD_GIVE_COINS);
    Put16(&w, CLERK_COINS);
    Put16(&w, CMD_NPC_MSG);
    w.data[w.at++] = (u8)(firstMessage + 1);
    Put16(&w, CMD_WAIT_BUTTON);
    Put16(&w, CMD_CLOSE_MSG);
    Put16(&w, CMD_RELEASE_ALL);
    Put16(&w, CMD_END);
    return w.at;
}

#define CLERK_SCRIPT_SIZE 96

u8 *SlotMaps_PatchScripts(u32 mapId, u8 *scripts, u32 size, enum HeapID heapID) {
    u8 firstMessage;
    if (mapId == MAP_GOLDENROD_GAME_CORNER_JP) {
        firstMessage = 53;
    } else if (mapId == MAP_CELADON_GAME_CORNER_JP) {
        firstMessage = 23;
    } else {
        return scripts;
    }
    if (scripts == NULL || size < 4) {
        return scripts;
    }
    // Script 0 (the clerk, object 0 in both maps): its offset is the first word, relative to the word's end
    u8 *patched = Heap_Alloc(heapID, size + CLERK_SCRIPT_SIZE + 2);
    if (patched == NULL) {
        return scripts;
    }
    memcpy(patched, scripts, size);
    const u32 original = 4 + (u32)(patched[0] | patched[1] << 8 | patched[2] << 16 | patched[3] << 24);
    const u32 start = (size + 1) & ~1u;
    if (start != size) {
        patched[size] = 0;
    }
    WriteClerkScript(patched, start, original, firstMessage);
    const u32 entry = start - 4;
    patched[0] = (u8)entry;
    patched[1] = (u8)(entry >> 8);
    patched[2] = (u8)(entry >> 16);
    patched[3] = (u8)(entry >> 24);
    Heap_Free(scripts);
    return patched;
}
