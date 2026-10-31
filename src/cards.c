#include "cards.h"

#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>

static const card_t suits[NUM_SUITS] = {ORS, COPES, ESPASES, BASTOS};

void fill_spanish_deck(card_t deck[SPANISH_DECK_SIZE]) {
	unsigned n = 0;
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		for (unsigned value = 1; value <= NUM_SUIT_CARDS; ++value) {
			deck[n++] = (card_t)(suits[i] + value);
		}
	}
}

void fill_small_spanish_deck(card_t deck[SMALL_SPANISH_DECK_SIZE]) {
	unsigned n = 0;
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		for (unsigned value = 1; value <= NUM_SUIT_CARDS; ++value) {
			if (value <= SMALL_NUM_VALUES || value > NUM_VALUES) {
				deck[n++] = (card_t)(suits[i] + value);
			}
		}
	}
}

static void swap(card_t *set, unsigned i, unsigned j) {
	if (j != i) {
		card_t tmp = set[j];
		set[j] = set[i];
		set[i] = tmp;
	}
}

void shuffle(card_t *set, unsigned n) {
	unsigned i;
	for (i = n; i > 1; --i) {
		swap(set, i - 1, (unsigned)SDL_rand((Sint32)i));
	}
}

bool is_short_suit(card_t card) {
	const unsigned suit = card & SUIT_MASK;
	return suit == ORS || suit == COPES;
}
