#define SDL_MAIN_USE_CALLBACKS 1 /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "cards.h"

#if defined(WIN32) || defined(_WIN32) ||                                       \
    defined(__WIN32) && !defined(__CYGWIN__)
#define OS_PATH_SEPARATOR "\\"
#else
#define OS_PATH_SEPARATOR "/"
#endif

struct button {
	TTF_Text *text;
	SDL_FRect rect;
	bool pressing;
};

struct mouse_state {
	SDL_MouseButtonFlags flags;
	SDL_FPoint pos;
};

struct deck_data {
	card_t cards[SPANISH_DECK_SIZE];
	SDL_FPoint position;
	unsigned num_cards;
	struct button draw_button;
};

struct column_data {
	card_t cards[SPANISH_DECK_SIZE / 2];
	SDL_FPoint position;
	SDL_FPoint position_receiving;
	float margin;
	unsigned num_cards;
	unsigned num_draggable_cards;
	unsigned num_receiving;
};

struct delayed_dragging_data {
	Uint64 delay_started;
	unsigned i;
	SDL_FPoint pointer;
};
#define DRAG_START_DELAY_MS 150
#define DRAG_START_MOTION_THRESHOLD 4.f

struct dragging_data {
	card_t cards[SPANISH_DECK_SIZE / 4];
	SDL_FPoint position;
	unsigned num_cards;
	int orig_column;
	bool animate_to_pointer;
};

struct bank_data {
	card_t top_card;
	card_t top_card_receiving;
	SDL_FPoint position;
	SDL_FPoint position_receiving;
};

struct game_state {
	struct deck_data deck;
	struct column_data column[8];
	struct bank_data bank[4];
	struct dragging_data dragging;
	struct delayed_dragging_data delayed_dragging;
	SDL_FPoint card_size;
};

struct app_state {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *table_texture;
	SDL_Texture *deck_texture;
	TTF_TextEngine *text_engine;
	TTF_Font *font;
	float font_size;
	SDL_Point window_size;
	SDL_FPoint card_texture_size;
	Uint64 last_frame_ticks;
	struct game_state game;
};

const char *app_name = "Card Game";

static bool get_path_to_resource(char **path, const char *kind,
				 const char *name) {
#ifdef __ANDROID__
	SDL_asprintf(path, "%s" OS_PATH_SEPARATOR "%s", kind, name);
#else
	SDL_asprintf(path, "%s%s" OS_PATH_SEPARATOR "%s", SDL_GetBasePath(),
		     kind, name);
#endif
	return *path != NULL;
}

/* Load to card_texture a png, without using image lib */
static SDL_Texture *load_texture(SDL_Renderer *renderer, char *file_name) {
	char *png_path = NULL;
	if (!get_path_to_resource(&png_path, "textures", file_name))
		return NULL;
	SDL_Surface *surface = SDL_LoadPNG(png_path);
	SDL_free(png_path);
	if (!surface) {
		return NULL;
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_DestroySurface(surface);
	return texture;
}

static void deal_cards(struct game_state *gs) {
	for (unsigned i = 0; i < 8 && gs->deck.num_cards > 0; ++i) {
		card_t *col_cards = gs->column[i].cards;
		col_cards[gs->column[i].num_cards] =
		    gs->deck.cards[gs->deck.num_cards - 1];
		++gs->column[i].num_cards;
		--gs->deck.num_cards;
		if (gs->column[i].num_cards == 1) {
			gs->column[i].num_draggable_cards = 1;
		} else {
			card_t a = col_cards[gs->column[i].num_cards - 1];
			card_t b = col_cards[gs->column[i].num_cards - 2];
			if ((a & VAL_MASK) + 1 == (b & VAL_MASK) &&
			    compare_suit(a, b) == -1)
				++gs->column[i].num_draggable_cards;
			else
				gs->column[i].num_draggable_cards = 1;
		}
		gs->column[i].num_receiving = 1;
		gs->column[i].position_receiving = gs->deck.position;
	}
}

static void reset_game_state(struct game_state *gs) {
	SDL_memcpy(gs->deck.cards, SPANISH_DECK, sizeof(SPANISH_DECK));
	gs->deck.num_cards = SPANISH_DECK_SIZE;
	shuffle(gs->deck.cards, SPANISH_DECK_SIZE);
	float margin = 0.2f * gs->card_size.x;
	for (unsigned i = 0; i < 8; ++i) {
		gs->column[i].num_cards = 0;
		gs->column[i].margin = margin;
	}
	for (unsigned i = 0; i < 4; ++i) {
		gs->bank[i].top_card = 0;
	}
	gs->dragging.num_cards = 0;
	deal_cards(gs);
}

static void calc_positions(struct app_state *as) {
	float card_ratio = as->card_texture_size.x / as->card_texture_size.y;
	float x1 = as->window_size.x / 9.1f;
	float x2 = as->window_size.y / 3.6f * card_ratio;

	as->game.card_size.x = fminf(x1, x2);
	as->game.card_size.y = as->game.card_size.x / card_ratio;
	float margin = 0.2f * as->game.card_size.x;

	float left = (as->window_size.x - as->game.card_size.x * 8.7f) / 2.f;
	float top = margin * 2.f;

	int w, h, old_w = 0;
	TTF_GetTextSize(as->game.deck.draw_button.text, &w, &h);
	const float objective = as->game.card_size.x / 2.f;
	while (w < objective - 1 || w > objective + 1) {
		as->font_size += (objective / w - 1) * as->font_size;
		TTF_SetFontSize(as->font, as->font_size);
		TTF_GetTextSize(as->game.deck.draw_button.text, &w, &h);
		if (w == old_w)
			break;
		old_w = w;
	}
	as->game.deck.draw_button.rect.w = w + 4;
	as->game.deck.draw_button.rect.h = h;

	as->game.deck.position.x = left;
	as->game.deck.position.y = top;
	as->game.deck.draw_button.rect.x =
	    as->game.deck.position.x +
	    (as->game.card_size.x - as->game.deck.draw_button.rect.w) / 2.f;
	as->game.deck.draw_button.rect.y =
	    as->game.deck.position.y +
	    (as->game.card_size.y - as->game.deck.draw_button.rect.h) / 2.f;
	for (int i = 0; i < 8; ++i) {
		as->game.column[i].position.x =
		    left + i * as->game.card_size.x * 1.1;
		as->game.column[i].position.y =
		    top + as->game.card_size.y * 1.1;
		as->game.column[i].margin = margin;
	}
	for (int i = 0; i < 4; ++i) {
		as->game.bank[i].position.x =
		    left + (i + 4) * as->game.card_size.x * 1.1;
		as->game.bank[i].position.y = top;
	}
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
	srand(time(NULL));
	SDL_SetAppMetadata(app_name, "0.1", NULL);

	struct app_state *as = SDL_malloc(sizeof(*as));
	if (!as)
		return SDL_APP_FAILURE;
	*appstate = as;
	SDL_memset(as, 0, sizeof(*as));

	/* Only affects Android/iOS. */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

	if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init())
		return SDL_APP_FAILURE;

	as->window = SDL_CreateWindow(app_name, 640, 480, SDL_WINDOW_RESIZABLE);
	if (!as->window)
		return SDL_APP_FAILURE;
	as->renderer = SDL_CreateRenderer(as->window, NULL);
	if (!as->renderer)
		return SDL_APP_FAILURE;

	as->table_texture = load_texture(as->renderer, "wood_tile.png");
	if (!as->table_texture)
		return SDL_APP_FAILURE;
	as->deck_texture =
	    load_texture(as->renderer, "baraja_espanola_GNU.png");
	if (!as->deck_texture)
		return SDL_APP_FAILURE;

	as->text_engine = TTF_CreateRendererTextEngine(as->renderer);
	if (!as->text_engine)
		return SDL_APP_FAILURE;

	char *font_path = NULL;
	if (!get_path_to_resource(&font_path, "fonts", "OpenSans-Regular.ttf"))
		return SDL_APP_FAILURE;
	as->font_size = 14.f;
	as->font = TTF_OpenFont(font_path, as->font_size);
	SDL_free(font_path);
	if (!as->font)
		return SDL_APP_FAILURE;

	as->game.deck.draw_button.text =
	    TTF_CreateText(as->text_engine, as->font, "Roba", 4);
	if (!as->game.deck.draw_button.text)
		return SDL_APP_FAILURE;

	TTF_SetTextColor(as->game.deck.draw_button.text, 0, 0, 0,
			 SDL_ALPHA_OPAQUE);

	SDL_SetRenderDrawBlendMode(as->renderer, SDL_BLENDMODE_BLEND);

	SDL_GetTextureSize(as->deck_texture, &as->card_texture_size.x,
			   &as->card_texture_size.y);

	as->card_texture_size.x /= 12;
	as->card_texture_size.y /= 5;

	reset_game_state(&as->game);

	SDL_GetWindowSize(as->window, &as->window_size.x, &as->window_size.y);
	calc_positions(as);

	as->last_frame_ticks = SDL_GetTicks();

	return SDL_APP_CONTINUE;
}

static void update_num_draggable_cards(struct column_data *column) {
	if (column->num_draggable_cards != 0)
		return;
	if (column->num_cards < 2) {
		column->num_draggable_cards = column->num_cards;
		return;
	}
	unsigned nd = 1;
	for (unsigned j = column->num_cards - 1; j > 0; --j) {
		card_t a = column->cards[j];
		card_t b = column->cards[j - 1];
		if ((a & VAL_MASK) + 1 == (b & VAL_MASK) &&
		    compare_suit(a, b) == -1)
			++nd;
		else
			break;
	}
	column->num_draggable_cards = nd;
}

static bool card_fits_on_bank(card_t bank_top, card_t card) {
	return (bank_top == 0 && (card & VAL_MASK) == 1) ||
	       bank_top + 1 == card;
}

static void start_drag(struct game_state *gs, unsigned i, const SDL_FPoint *p) {
	if (gs->delayed_dragging.delay_started)
		gs->delayed_dragging.delay_started = 0;
	struct column_data *column = &gs->column[i];
	struct dragging_data *dragging = &gs->dragging;
	int n = floorf((p->y - column->position.y) / column->margin);
	if (n >= column->num_cards)
		n = column->num_cards - 1;
	if (column->num_cards - n > column->num_draggable_cards)
		return;
	dragging->num_cards = column->num_cards - n;
	dragging->position.x = column->position.x;
	dragging->position.y = column->position.y + n * column->margin;
	dragging->animate_to_pointer = true;
	column->num_cards = n;
	for (unsigned j = 0; j < dragging->num_cards; ++j) {
		dragging->cards[j] = column->cards[n + j];
	}
	dragging->orig_column = i;
	column->num_draggable_cards -= dragging->num_cards;
	update_num_draggable_cards(column);
}

static void start_drag_delayed(struct game_state *gs, unsigned i,
			       const SDL_FPoint *p) {
	gs->delayed_dragging.delay_started = SDL_GetTicks();
	gs->delayed_dragging.i = i;
	gs->delayed_dragging.pointer = *p;
}

static bool is_bottom_card_click(const struct column_data *column,
				 const SDL_FPoint *p) {
	unsigned n = floorf((p->y - column->position.y) / column->margin);
	return n >= column->num_cards - 1;
}

static bool try_move_bottom_card_to_bank(struct game_state *gs, unsigned i) {
	struct column_data *column = &gs->column[i];
	if (column->num_cards == 0)
		return false;
	card_t dc = column->cards[column->num_cards - 1];
	for (unsigned b = 0; b < 4; ++b) {
		if (!card_fits_on_bank(gs->bank[b].top_card, dc))
			continue;
		gs->bank[b].top_card_receiving = dc;
		gs->bank[b].position_receiving.x = column->position.x;
		gs->bank[b].position_receiving.y =
		    column->position.y +
		    (column->num_cards - 1) * column->margin;
		--column->num_cards;
		if (column->num_receiving > 0)
			--column->num_receiving;
		if (column->num_draggable_cards > 0)
			--column->num_draggable_cards;
		update_num_draggable_cards(column);
		return true;
	}
	return false;
}

static void end_drag(struct game_state *gs, SDL_FPoint *p) {
	SDL_FRect r = {.w = gs->card_size.x};
	int dest = gs->dragging.orig_column;
	const unsigned num_cards = gs->dragging.num_cards;
	const float margin = 0.2f * gs->card_size.x;
	SDL_FPoint drop_position;
	if (gs->dragging.animate_to_pointer) {
		drop_position = gs->dragging.position;
	} else {
		drop_position.x = p->x - gs->card_size.x / 2.f;
		drop_position.y = p->y - margin / 2.f;
	}
	gs->dragging.num_cards = 0;
	if (num_cards == 1) {
		card_t dc = gs->dragging.cards[0];
		for (unsigned i = 0; i < 4; ++i) {
			card_t bc = gs->bank[i].top_card;
			if (card_fits_on_bank(bc, dc)) {
				r.x = gs->bank[i].position.x;
				r.y = gs->bank[i].position.y;
				r.h = gs->card_size.y;
				if (SDL_PointInRectFloat(p, &r)) {
					gs->bank[i].top_card_receiving = dc;
					gs->bank[i].position_receiving =
					    drop_position;
					return;
				}
			}
		}
	}
	for (unsigned i = 0; i < 8; ++i) {
		unsigned n = gs->column[i].num_cards;
		if (n == 0)
			n = 1;
		r.x = gs->column[i].position.x;
		r.y = gs->column[i].position.y;
		r.h = gs->card_size.y + (n - 1) * gs->column[i].margin;
		if (SDL_PointInRectFloat(p, &r)) {
			if (i == dest)
				break;
			if (gs->column[i].num_cards == 0) {
				dest = i;
				break;
			}
			card_t a = gs->dragging.cards[0];
			card_t b = gs->column[i].cards[n - 1];
			if ((a & VAL_MASK) + 1 == (b & VAL_MASK) &&
			    compare_suit(a, b) == -1)
				dest = i;
			break;
		}
	}
	unsigned n = gs->column[dest].num_cards;
	for (unsigned i = 0; i < num_cards; ++i) {
		gs->column[dest].cards[n + i] = gs->dragging.cards[i];
	}
	if (n == 0) {
		gs->column[dest].num_draggable_cards = num_cards;
	} else {
		card_t a = gs->column[dest].cards[n];
		card_t b = gs->column[dest].cards[n - 1];
		if ((a & VAL_MASK) + 1 == (b & VAL_MASK) &&
		    compare_suit(a, b) == -1)
			gs->column[dest].num_draggable_cards += num_cards;
		else
			gs->column[dest].num_draggable_cards = num_cards;
	}
	gs->column[dest].num_cards += num_cards;
	gs->column[dest].position_receiving = drop_position;
	gs->column[dest].num_receiving = num_cards;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	struct app_state *as = (struct app_state *)appstate;
	SDL_FPoint p = {0, 0};
	SDL_FRect r = {0, 0, 0, 0};
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button == SDL_BUTTON_LEFT) {
			p.x = event->button.x;
			p.y = event->button.y;
			if (SDL_PointInRectFloat(
				&p, &as->game.deck.draw_button.rect)) {
				as->game.deck.draw_button.pressing = true;
				break;
			}
			for (unsigned i = 0; i < 8; ++i) {
				if (as->game.column[i].num_cards == 0)
					continue;
				r.x = as->game.column[i].position.x;
				r.y = as->game.column[i].position.y;
				r.w = as->game.card_size.x;
				r.h = as->game.card_size.y +
				      (as->game.column[i].num_cards - 1) *
					  as->game.column[i].margin;
				if (SDL_PointInRectFloat(&p, &r)) {
					if (is_bottom_card_click(
						&as->game.column[i], &p)) {
						if (event->button.clicks == 1)
							start_drag_delayed(
							    &as->game, i, &p);
						else if (
						    !try_move_bottom_card_to_bank(
							&as->game, i))
							start_drag(&as->game, i,
								   &p);

					} else {
						start_drag(&as->game, i, &p);
					}
					break;
				}
			}
		}
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if (as->game.delayed_dragging.delay_started) {
			p.x = event->motion.x;
			p.y = event->motion.y;
			float dist =
			    hypot(p.x - as->game.delayed_dragging.pointer.x,
				  p.y - as->game.delayed_dragging.pointer.y);
			if (dist > DRAG_START_MOTION_THRESHOLD)
				start_drag(&as->game,
					   as->game.delayed_dragging.i,
					   &as->game.delayed_dragging.pointer);
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button == SDL_BUTTON_LEFT) {
			if (as->game.delayed_dragging.delay_started)
				as->game.delayed_dragging.delay_started = 0;
			p.x = event->button.x;
			p.y = event->button.y;
			if (as->game.deck.draw_button.pressing) {
				if (SDL_PointInRectFloat(
					&p, &as->game.deck.draw_button.rect)) {
					deal_cards(&as->game);
				}
				as->game.deck.draw_button.pressing = false;
				break;
			}
			if (as->game.dragging.num_cards != 0)
				end_drag(&as->game, &p);
		}
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		as->window_size.x = event->window.data1;
		as->window_size.y = event->window.data2;
		calc_positions(as);
		break;
	case SDL_EVENT_KEY_DOWN:
		if (event->key.key == SDLK_R && !event->key.mod) {
			reset_game_state(&as->game);
		}
		break;
	case SDL_EVENT_QUIT:
		return SDL_APP_SUCCESS;
	default:
		break;
	}
	return SDL_APP_CONTINUE;
}

static void draw_background(struct app_state *as) {
	SDL_SetRenderDrawColor(as->renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(as->renderer);
	SDL_FRect rect = {
	    .x = 0, .y = 0, .w = as->window_size.x, .h = as->window_size.y};
	SDL_RenderTextureTiled(as->renderer, as->table_texture, NULL, 1, &rect);
}

static void draw_deck(const struct app_state *as,
		      const struct mouse_state *mouse) {
	SDL_FRect dest_rect = {.x = as->game.deck.position.x,
			       .y = as->game.deck.position.y,
			       .w = as->game.card_size.x,
			       .h = as->game.card_size.y};
	SDL_FRect origin_rect = {.x = as->card_texture_size.x,
				 .y = as->card_texture_size.y * 4,
				 .w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};
	if (as->game.deck.num_cards == 0) {
		origin_rect.x = as->card_texture_size.x * 2;
		origin_rect.y = as->card_texture_size.y * 4;
	}
	SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
			  &dest_rect);
	if (as->game.deck.num_cards == 0) {
		SDL_SetRenderDrawColor(as->renderer, 100, 100, 100, 200);
	} else if (as->game.dragging.num_cards == 0 &&
		   SDL_PointInRectFloat(&mouse->pos,
					&as->game.deck.draw_button.rect)) {
		if (as->game.deck.draw_button.pressing)
			SDL_SetRenderDrawColor(as->renderer, 50, 200, 50,
					       SDL_ALPHA_OPAQUE);
		else
			SDL_SetRenderDrawColor(as->renderer, 0, 255, 0,
					       SDL_ALPHA_OPAQUE);
	} else
		SDL_SetRenderDrawColor(as->renderer, 0, 255, 0, 200);
	SDL_RenderFillRect(as->renderer, &as->game.deck.draw_button.rect);
	TTF_DrawRendererText(as->game.deck.draw_button.text,
			     as->game.deck.draw_button.rect.x + 2,
			     as->game.deck.draw_button.rect.y);
}

static void draw_column(struct app_state *as, unsigned i,
			const struct mouse_state *mouse, float dt) {
	struct column_data *column = &as->game.column[i];
	const unsigned n = column->num_cards;
	SDL_FRect dest_rect = {.x = column->position.x,
			       .y = column->position.y,
			       .w = as->game.card_size.x,
			       .h = as->game.card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};
	SDL_FRect visible_rect = {.x = column->position.x,
				  .w = as->game.card_size.x,
				  .h = column->margin};
	if (n == 0 || n == column->num_receiving) {
		origin_rect.x = as->card_texture_size.x * 2;
		origin_rect.y = as->card_texture_size.y * 4;
		SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
				  &dest_rect);
		if (n == 0)
			return;
	}
	bool draw_extra = false;
	SDL_FRect draw_extra_origin, draw_extra_dest;
	const unsigned first_receiving = n - column->num_receiving;
	for (unsigned j = 0; j < n; ++j) {
		const card_t c = column->cards[j];
		origin_rect.x = as->card_texture_size.x * ((c & VAL_MASK) - 1);
		origin_rect.y = as->card_texture_size.y *
				(((c & SUIT_MASK) >> SUIT_OFFSET) - 1);
		if (j == first_receiving) {
			float last_x = column->position_receiving.x;
			float last_y = column->position_receiving.y;
			float dist =
			    hypot(dest_rect.x - last_x, dest_rect.y - last_y);
			float step = as->game.card_size.x * 12 *
				     dt; // FIXME: magic constant
			if (dist <= step) {
				column->num_receiving = 0;
			} else {
				column->position_receiving.x +=
				    (dest_rect.x - last_x) / dist * step;
				column->position_receiving.y +=
				    (dest_rect.y - last_y) / dist * step;
				break;
			}
		}
		SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
				  &dest_rect);
		visible_rect.y = dest_rect.y;
		if (column->num_receiving == 0 && mouse && j < n - 1 &&
		    as->game.dragging.num_cards == 0 &&
		    SDL_PointInRectFloat(&mouse->pos, &visible_rect)) {
			draw_extra = true;
			draw_extra_origin = origin_rect;
			draw_extra_dest = dest_rect;
		}
		dest_rect.y += column->margin;
	}
	if (draw_extra) {
		SDL_RenderTexture(as->renderer, as->deck_texture,
				  &draw_extra_origin, &draw_extra_dest);
	}
}

static void draw_column_receiving(struct app_state *as, unsigned i) {
	struct column_data *column = &as->game.column[i];
	const unsigned n = column->num_cards;
	const unsigned first_receiving = n - column->num_receiving;
	SDL_FRect dest_rect = {.x = column->position_receiving.x,
			       .y = column->position_receiving.y,
			       .w = as->game.card_size.x,
			       .h = as->game.card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};

	if (column->num_receiving == 0)
		return;

	for (unsigned j = first_receiving; j < n; ++j) {
		const card_t c = column->cards[j];
		origin_rect.x = as->card_texture_size.x * ((c & VAL_MASK) - 1);
		origin_rect.y = as->card_texture_size.y *
				(((c & SUIT_MASK) >> SUIT_OFFSET) - 1);
		SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
				  &dest_rect);
		dest_rect.y += column->margin;
	}
}

static void draw_bank(struct app_state *as, unsigned i, float dt) {
	struct bank_data *bank = &as->game.bank[i];
	SDL_FRect dest_rect = {.x = bank->position.x,
			       .y = bank->position.y,
			       .w = as->game.card_size.x,
			       .h = as->game.card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};

	card_t c = bank->top_card_receiving;
	if (c != 0) {
		float last_x = bank->position_receiving.x;
		float last_y = bank->position_receiving.y;
		float dist = hypot(dest_rect.x - last_x, dest_rect.y - last_y);
		float move =
		    as->game.card_size.x * 12 * dt; // FIXME: magic constant
		if (dist <= move) {
			bank->top_card = c;
			bank->top_card_receiving = 0;
		} else {
			bank->position_receiving.x +=
			    (dest_rect.x - last_x) / dist * move;
			bank->position_receiving.y +=
			    (dest_rect.y - last_y) / dist * move;
		}
	}

	c = bank->top_card;
	if (c == 0) {
		origin_rect.x = as->card_texture_size.x * 2;
		origin_rect.y = as->card_texture_size.y * 4;
	} else {
		origin_rect.x = as->card_texture_size.x * ((c & VAL_MASK) - 1);
		origin_rect.y = as->card_texture_size.y *
				(((c & SUIT_MASK) >> SUIT_OFFSET) - 1);
	}

	SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
			  &dest_rect);
}

static void draw_bank_receiving(struct app_state *as, unsigned i) {
	struct bank_data *bank = &as->game.bank[i];
	SDL_FRect dest_rect = {.x = bank->position_receiving.x,
			       .y = bank->position_receiving.y,
			       .w = as->game.card_size.x,
			       .h = as->game.card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};

	card_t c = bank->top_card_receiving;
	if (c == 0)
		return;
	origin_rect.x = as->card_texture_size.x * ((c & VAL_MASK) - 1);
	origin_rect.y =
	    as->card_texture_size.y * (((c & SUIT_MASK) >> SUIT_OFFSET) - 1);
	SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
			  &dest_rect);
}

static void draw_floating(struct app_state *as, const SDL_FPoint mouse_pos,
			  float dt) {
	if (as->game.dragging.num_cards == 0)
		return;
	const float margin = 0.2f * as->game.card_size.x;
	float x = mouse_pos.x - as->game.card_size.x / 2.f;
	float y = mouse_pos.y - margin / 2;
	if (as->game.dragging.animate_to_pointer) {
		const float last_x = as->game.dragging.position.x;
		const float last_y = as->game.dragging.position.y;
		const float dist = hypot(x - last_x, y - last_y);
		float move =
		    as->game.card_size.x * 24 * dt; // FIXME: magic constant
		if (dist <= move) {
			as->game.dragging.animate_to_pointer = false;
		} else {
			as->game.dragging.position.x +=
			    (x - last_x) / dist * move;
			as->game.dragging.position.y +=
			    (y - last_y) / dist * move;
			x = as->game.dragging.position.x;
			y = as->game.dragging.position.y;
		}
	}
	const unsigned n = as->game.dragging.num_cards;
	SDL_FRect dest_rect = {.x = x,
			       .y = y,
			       .w = as->game.card_size.x,
			       .h = as->game.card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};
	for (unsigned j = 0; j < n; ++j) {
		const card_t c = as->game.dragging.cards[j];
		origin_rect.x = as->card_texture_size.x * ((c & VAL_MASK) - 1);
		origin_rect.y = as->card_texture_size.y *
				(((c & SUIT_MASK) >> SUIT_OFFSET) - 1);
		dest_rect.y = y + margin * j;
		SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
				  &dest_rect);
	}
}

SDL_AppResult SDL_AppIterate(void *appstate) {
	struct app_state *as = (struct app_state *)appstate;
	struct mouse_state mouse;
	const float card_ratio =
	    as->card_texture_size.y / as->card_texture_size.x;
	const Uint64 now = SDL_GetTicks();
	if (as->game.delayed_dragging.delay_started &&
	    now - as->game.delayed_dragging.delay_started > DRAG_START_DELAY_MS)
		start_drag(&as->game, as->game.delayed_dragging.i,
			   &as->game.delayed_dragging.pointer);
	const float dt = (now - as->last_frame_ticks) / 1000.f;
	as->last_frame_ticks = now;
	mouse.flags = SDL_GetMouseState(&mouse.pos.x, &mouse.pos.y);
	draw_background(as);
	draw_deck(as, &mouse);
	for (unsigned i = 0; i < 8; ++i) {
		draw_column(as, i, &mouse, dt);
	}
	for (unsigned i = 0; i < 4; ++i) {
		draw_bank(as, i, dt);
	}
	for (unsigned i = 0; i < 8; ++i) {
		if (as->game.column[i].num_receiving != 0)
			draw_column_receiving(as, i);
	}
	for (unsigned i = 0; i < 4; ++i) {
		if (as->game.bank[i].top_card_receiving != 0)
			draw_bank_receiving(as, i);
	}
	draw_floating(as, mouse.pos, dt);
	SDL_RenderPresent(as->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	struct app_state *as = (struct app_state *)appstate;
	if (result == SDL_APP_FAILURE)
		SDL_Log("App finished due to failure: %s", SDL_GetError());

	if (as->game.deck.draw_button.text)
		TTF_DestroyText(as->game.deck.draw_button.text);
	if (as->font)
		TTF_CloseFont(as->font);
	if (as->text_engine)
		TTF_DestroyRendererTextEngine(as->text_engine);
	if (as->table_texture)
		SDL_DestroyTexture(as->table_texture);
	if (as->deck_texture)
		SDL_DestroyTexture(as->deck_texture);
	SDL_free(as);
}
