#include "card_texture.h"

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>

#include "app.h"
#include "cards.h"

#define DECK_TEXTURE_FILE "baraja_espanola_GNU.png"
#define DECK_TEXTURE_COLUMNS NUM_SUIT_CARDS
#define DECK_TEXTURE_ROWS (NUM_SUITS + 1)
#define BACK_COLUMN 1
#define OUTLINE_COLUMN 2

struct card_texture {
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_FPoint card_size;
};

struct card_texture *card_texture_init(SDL_Renderer *renderer) {
	struct card_texture *ct = SDL_malloc(sizeof(*ct));
	if (!ct) {
		return NULL;
	}
	ct->renderer = renderer;
	ct->texture = load_texture(renderer, DECK_TEXTURE_FILE);
	if (!ct->texture) {
		SDL_free(ct);
		return NULL;
	}
	SDL_GetTextureSize(ct->texture, &ct->card_size.x, &ct->card_size.y);
	ct->card_size.x /= DECK_TEXTURE_COLUMNS;
	ct->card_size.y /= DECK_TEXTURE_ROWS;
	return ct;
}

void card_texture_quit(struct card_texture *ct) {
	SDL_DestroyTexture(ct->texture);
	SDL_free(ct);
}

float card_texture_aspect_ratio(const struct card_texture *ct) {
	return ct->card_size.x / ct->card_size.y;
}

static SDL_FRect source_rect(const struct card_texture *ct, card_t card) {
	unsigned column;
	unsigned row;
	if (card == CARD_BACK) {
		column = BACK_COLUMN;
		row = NUM_SUITS;
	} else if (card == CARD_OUTLINE) {
		column = OUTLINE_COLUMN;
		row = NUM_SUITS;
	} else {
		column = (card & VAL_MASK) - 1;
		row = ((card & SUIT_MASK) >> SUIT_OFFSET) - 1;
	}
	return (SDL_FRect){.x = ct->card_size.x * (float)column,
			   .y = ct->card_size.y * (float)row,
			   .w = ct->card_size.x,
			   .h = ct->card_size.y};
}

void card_texture_draw(const struct card_texture *ct, card_t card,
		       const SDL_FRect *dest_rect) {
	const SDL_FRect origin_rect = source_rect(ct, card);
	SDL_RenderTexture(ct->renderer, ct->texture, &origin_rect, dest_rect);
}

void card_texture_draw_rotated(const struct card_texture *ct, card_t card,
			       const SDL_FRect *dest_rect, double angle) {
	const SDL_FRect origin_rect = source_rect(ct, card);
	SDL_RenderTextureRotated(ct->renderer, ct->texture, &origin_rect,
				 dest_rect, angle, NULL, SDL_FLIP_NONE);
}
