#include "cards.h"

#include <stdlib.h>

static void swap(card_t *set, unsigned i, unsigned j) {
	if (j != i) {
		card_t tmp = set[j];
		set[j] = set[i];
		set[i] = tmp;
	}
}

void shuffle(card_t *set, unsigned n) {
	unsigned i;
	for (i = 1; i < n; ++i) {
		unsigned j = rand() / (RAND_MAX / i + 1);
		swap(set, i, j);
	}
}

static unsigned higest_index(card_t *set, unsigned size) {
	unsigned i, res = 0;
	for (i = 1; i < size; ++i) {
		if (set[i] > set[res]) {
			res = i;
		}
	}
	return res;
}

void sort(card_t *set, unsigned n) {
	unsigned i;
	for (i = 0; i < n; ++i) {
		unsigned j = higest_index(set, n - i);
		swap(set, n - i - 1, j);
	}
}

/* 0: equal, 1: same lenght, -1: alternate length */
int compare_suit(card_t a, card_t b) {
	a = a & SUIT_MASK;
	if (a == b)
		return 0;
	switch (a) {
	case ORS:
	case COPES:
		if (b < ESPASES)
			return 1;
		return -1;
	case ESPASES:
	case BASTOS:
		if (b < ESPASES)
			return -1;
		return 1;
	}
}
