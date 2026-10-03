#include "cards.h"

#include <stdlib.h>

// Aproximation of C23 unreachable
#ifdef _MSC_VER // MSVC
#define unreachable() (__assume(0))
#else // __GNUC__ // GCC, Clang, ICC
#define unreachable() (__builtin_unreachable())
#endif

static void swap(card_t *set, unsigned i, unsigned j) {
	if (j != i) {
		card_t tmp = set[j];
		set[j] = set[i];
		set[i] = tmp;
	}
}

static unsigned random_below(unsigned n) {
	unsigned bucket = ((unsigned)RAND_MAX + 1u) / n;
	unsigned r;
	do {
		r = (unsigned)rand() / bucket;
	} while (r >= n);
	return r;
}

void shuffle(card_t *set, unsigned n) {
	unsigned i;
	for (i = n; i > 1; --i) {
		swap(set, i - 1, random_below(i));
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

int compare_suit(card_t a, card_t b) {
	a = a & SUIT_MASK;
	b = b & SUIT_MASK;
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
	unreachable();
}
