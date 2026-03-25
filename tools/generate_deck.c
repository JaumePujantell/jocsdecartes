#include "cards.h"
#include <stdio.h>

int main() {
	printf("#include \"cards.h\"\n");
	printf("const card_t SPANISH_DECK[SPANISH_DECK_SIZE] = {");
	const int suits[4] = {ORS, COPES, ESPASES, BASTOS};
	int i = 0;
	for (; i < 4; ++i) {
		int j = 1;
		for (; j <= 12; ++j) {
			printf("%i,", suits[i] + j);
		}
	}
	printf("};\n");
	printf("const card_t SMALL_SPANISH_DECK[SMALL_SPANISH_DECK_SIZE] = {");
	for (i = 0; i < 4; ++i) {
		int j = 1;
		for (; j <= 7; ++j) {
			printf("%i,", suits[i] + j);
		}
		for (j = 10; j <= 12; ++j) {
			printf("%i,", suits[i] + j);
		}
	}
	printf("};\n");
}