#ifndef CARDS_H
#define CARDS_H

#include <stdbool.h>

typedef unsigned char card_t;

#define ORS 0x10U
#define COPES 0x20U
#define ESPASES 0x30U
#define BASTOS 0x40U
#define VAL_MASK 0x0FU
#define SUIT_MASK 0xF0U
#define SUIT_OFFSET 4U

#define NUM_SUITS 4U
#define NUM_VALUES 9U
#define NUM_FIGURES 3U
#define NUM_SUIT_CARDS (NUM_VALUES + NUM_FIGURES)

#define SPANISH_DECK_SIZE (NUM_SUIT_CARDS * NUM_SUITS)

#define SMALL_NUM_VALUES 7U
#define SMALL_NUM_SUIT_CARDS (SMALL_NUM_VALUES + NUM_FIGURES)
#define SMALL_SPANISH_DECK_SIZE (SMALL_NUM_SUIT_CARDS * NUM_SUITS)

void fill_spanish_deck(card_t deck[SPANISH_DECK_SIZE]);
void fill_small_spanish_deck(card_t deck[SMALL_SPANISH_DECK_SIZE]);
void shuffle(card_t *set, unsigned n);
bool is_short_suit(card_t card);

#endif // !CARDS_H
