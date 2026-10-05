#ifndef CARDS_H
#define CARDS_H

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
extern const card_t SPANISH_DECK[SPANISH_DECK_SIZE];

#define SMALL_NUM_VALUES 7U
#define SMALL_NUM_SUIT_CARDS (SMALL_NUM_VALUES + NUM_FIGURES)
#define SMALL_SPANISH_DECK_SIZE (SMALL_NUM_SUIT_CARDS * NUM_SUITS)
extern const card_t SMALL_SPANISH_DECK[SMALL_SPANISH_DECK_SIZE];

void shuffle(card_t *set, unsigned n);
void sort(card_t *set, unsigned n);
/* 0 -> same suit, 1 -> same suit length, -1 -> different suit length */
int compare_suit(card_t a, card_t b);

#endif // !CARDS_H
