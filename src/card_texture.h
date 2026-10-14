#ifndef CARD_TEXTURE_H
#define CARD_TEXTURE_H

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>

#include "cards.h"

#define CARD_BACK 0x01U
#define CARD_OUTLINE 0x02U

struct card_texture;

struct card_texture *card_texture_init(SDL_Renderer *renderer);
void card_texture_quit(struct card_texture *ct);

float card_texture_aspect_ratio(const struct card_texture *ct);

void card_texture_draw(const struct card_texture *ct, card_t card,
		       const SDL_FRect *dest_rect);
void card_texture_draw_rotated(const struct card_texture *ct, card_t card,
			       const SDL_FRect *dest_rect, double angle);

#endif // !CARD_TEXTURE_H
