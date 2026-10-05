#include <stdio.h>

#include "cards.h"

int main(void) {
	printf("#include \"cards.h\"\n");
	printf("const card_t SPANISH_DECK[SPANISH_DECK_SIZE] = {");
	const unsigned suits[NUM_SUITS] = {ORS, COPES, ESPASES, BASTOS};
	unsigned i = 0;
	for (; i < NUM_SUITS; ++i) {
		unsigned j = 1;
		for (; j <= NUM_SUIT_CARDS; ++j) {
			printf("%u,", suits[i] + j);
		}
	}
	printf("};\n");
	printf("const card_t SMALL_SPANISH_DECK[SMALL_SPANISH_DECK_SIZE] = {");
	for (i = 0; i < NUM_SUITS; ++i) {
		unsigned j = 1;
		for (; j <= SMALL_NUM_VALUES; ++j) {
			printf("%u,", suits[i] + j);
		}
		for (j = NUM_VALUES + 1; j <= NUM_SUIT_CARDS; ++j) {
			printf("%u,", suits[i] + j);
		}
	}
	printf("};\n");
	return 0;
}