#ifndef CARDS_H
#define CARDS_H

typedef unsigned char card_t;

#define ORS 0x10u
#define COPES 0x20u
#define ESPASES 0x30u
#define BASTOS 0x40u
#define VAL_MASK 0x0Fu
#define SUIT_MASK 0xF0u
#define SUIT_OFFSET 4u

#define SPANISH_DECK_SIZE 48u // = 12 * 4;
extern const card_t SPANISH_DECK[SPANISH_DECK_SIZE];
#define SMALL_SPANISH_DECK_SIZE 40u // = 10 * 4;
extern const card_t SMALL_SPANISH_DECK[SMALL_SPANISH_DECK_SIZE];

void shuffle(card_t *set, unsigned n);
void sort(card_t *set, unsigned n);
/* 0 -> equal, 1 -> same kind, -1 -> different kind */
int compare_suit(card_t a, card_t b);

#endif // !CARDS_H
