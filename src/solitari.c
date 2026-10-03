#include <math.h>
#include <stdbool.h>

#include "cards.h"
#include "gui.h"
#include "solitari.h"

struct mouse_state {
	SDL_MouseButtonFlags flags;
	SDL_FPoint pos;
};

struct deck_data {
	card_t cards[SPANISH_DECK_SIZE];
	SDL_FPoint position;
	unsigned num_cards;
	struct button *draw_button;
};

struct column_data {
	card_t cards[SPANISH_DECK_SIZE];
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
	unsigned orig_column;
	bool animate_to_pointer;
};

struct bank_data {
	card_t top_card;
	card_t top_card_receiving;
	SDL_FPoint position;
	SDL_FPoint position_receiving;
};

struct solitari_state {
	struct deck_data deck;
	struct column_data column[8];
	struct bank_data bank[4];
	struct dragging_data dragging;
	struct delayed_dragging_data delayed_dragging;
	SDL_FPoint card_size;
};

static void deal_cards(struct solitari_state *gs) {
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

static void deck_button_clicked(void *userdata) {
	deal_cards((struct solitari_state *)userdata);
}

void solitari_restart(struct solitari_state *gs) {
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
		gs->bank[i].top_card_receiving = 0;
	}
	gs->dragging.num_cards = 0;
	gs->delayed_dragging.delay_started = 0;
	deal_cards(gs);
}

void solitari_resize(struct app_state *as) {
	struct solitari_state *gs = as->solitari;
	float card_ratio = as->card_texture_size.x / as->card_texture_size.y;
	float x1 = as->window_size.x / 9.1f;
	float x2 = as->window_size.y / 3.6f * card_ratio;

	gs->card_size.x = fminf(x1, x2);
	gs->card_size.y = gs->card_size.x / card_ratio;
	float margin = 0.2f * gs->card_size.x;

	float left = (as->window_size.x - gs->card_size.x * 8.7f) / 2.f;
	float top = margin * 2.f;

	button_fit_width(gs->deck.draw_button, gs->card_size.x / 2.f);

	gs->deck.position.x = left;
	gs->deck.position.y = top;
	SDL_FPoint button_dim = button_size(gs->deck.draw_button);
	button_set_position(
	    gs->deck.draw_button,
	    gs->deck.position.x + (gs->card_size.x - button_dim.x) / 2.f,
	    gs->deck.position.y + (gs->card_size.y - button_dim.y) / 2.f);
	for (int i = 0; i < 8; ++i) {
		gs->column[i].position.x = left + i * gs->card_size.x * 1.1;
		gs->column[i].position.y = top + gs->card_size.y * 1.1;
		gs->column[i].margin = margin;
	}
	for (int i = 0; i < 4; ++i) {
		gs->bank[i].position.x = left + (i + 4) * gs->card_size.x * 1.1;
		gs->bank[i].position.y = top;
	}
}

bool solitari_init(struct app_state *appstate) {
	struct gui_state *guistate = appstate->gui;
	struct solitari_state *gs = SDL_malloc(sizeof(*gs));
	if (!gs)
		return false;
	SDL_memset(gs, 0, sizeof(*gs));

	gs->deck.draw_button =
	    button_init(guistate, "Roba", deck_button_clicked, gs);
	if (!gs->deck.draw_button) {
		SDL_free(gs);
		return false;
	}
	appstate->solitari = gs;

	solitari_resize(appstate);
	solitari_restart(gs);
	return true;
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

static void start_drag(struct solitari_state *gs, unsigned i,
		       const SDL_FPoint *p) {
	if (gs->delayed_dragging.delay_started)
		gs->delayed_dragging.delay_started = 0;
	struct column_data *column = &gs->column[i];
	struct dragging_data *dragging = &gs->dragging;
	if (column->num_receiving != 0)
		return;
	int n = floorf((p->y - column->position.y) / column->margin);
	if (n >= (int)column->num_cards)
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

static void start_drag_delayed(struct solitari_state *gs, unsigned i,
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

static bool try_move_bottom_card_to_bank(struct solitari_state *gs,
					 unsigned i) {
	struct column_data *column = &gs->column[i];
	if (column->num_cards == 0)
		return false;
	card_t dc = column->cards[column->num_cards - 1];
	for (unsigned b = 0; b < 4; ++b) {
		if (gs->bank[b].top_card_receiving != 0 ||
		    !card_fits_on_bank(gs->bank[b].top_card, dc))
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

static void end_drag(struct solitari_state *gs, SDL_FPoint *p) {
	SDL_FRect r = {.w = gs->card_size.x};
	unsigned dest = gs->dragging.orig_column;
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
			if (gs->bank[i].top_card_receiving == 0 &&
			    card_fits_on_bank(bc, dc)) {
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

void solitari_event(struct app_state *appstate, SDL_Event *event) {
	struct solitari_state *gamestate = appstate->solitari;
	SDL_FPoint p = {0, 0};
	SDL_FRect r = {0, 0, 0, 0};
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button == SDL_BUTTON_LEFT) {
			if (button_event(gamestate->deck.draw_button, event))
				break;
			p.x = event->button.x;
			p.y = event->button.y;
			for (unsigned i = 0; i < 8; ++i) {
				if (gamestate->column[i].num_cards == 0)
					continue;
				r.x = gamestate->column[i].position.x;
				r.y = gamestate->column[i].position.y;
				r.w = gamestate->card_size.x;
				r.h = gamestate->card_size.y +
				      (gamestate->column[i].num_cards - 1) *
					  gamestate->column[i].margin;
				if (SDL_PointInRectFloat(&p, &r)) {
					if (is_bottom_card_click(
						&gamestate->column[i], &p)) {
						if (event->button.clicks == 1)
							start_drag_delayed(
							    gamestate, i, &p);
						else if (
						    !try_move_bottom_card_to_bank(
							gamestate, i))
							start_drag(gamestate, i,
								   &p);

					} else {
						start_drag(gamestate, i, &p);
					}
					break;
				}
			}
		}
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if (gamestate->delayed_dragging.delay_started) {
			p.x = event->motion.x;
			p.y = event->motion.y;
			float dist =
			    hypot(p.x - gamestate->delayed_dragging.pointer.x,
				  p.y - gamestate->delayed_dragging.pointer.y);
			if (dist > DRAG_START_MOTION_THRESHOLD)
				start_drag(
				    gamestate, gamestate->delayed_dragging.i,
				    &gamestate->delayed_dragging.pointer);
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button == SDL_BUTTON_LEFT) {
			if (gamestate->delayed_dragging.delay_started)
				gamestate->delayed_dragging.delay_started = 0;
			if (button_event(gamestate->deck.draw_button, event))
				break;
			p.x = event->button.x;
			p.y = event->button.y;
			if (gamestate->dragging.num_cards != 0)
				end_drag(gamestate, &p);
		}
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		solitari_resize(appstate);
		break;
	case SDL_EVENT_KEY_DOWN:
		if (event->key.key == SDLK_R && !event->key.mod) {
			solitari_restart(gamestate);
		}
		break;
	default:
		break;
	}
}

static void draw_deck(struct app_state *as, struct solitari_state *gs,
		      const struct mouse_state *mouse) {
	SDL_FRect dest_rect = {.x = gs->deck.position.x,
			       .y = gs->deck.position.y,
			       .w = gs->card_size.x,
			       .h = gs->card_size.y};
	SDL_FRect origin_rect = {.x = as->card_texture_size.x,
				 .y = as->card_texture_size.y * 4,
				 .w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};
	if (gs->deck.num_cards == 0) {
		origin_rect.x = as->card_texture_size.x * 2;
		origin_rect.y = as->card_texture_size.y * 4;
	}
	SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
			  &dest_rect);
	SDL_Color color;
	if (gs->deck.num_cards == 0) {
		color = (SDL_Color){100, 100, 100, 200};
	} else if (gs->dragging.num_cards == 0 &&
		   button_is_hovered(gs->deck.draw_button, mouse->pos)) {
		color = button_is_pressing(gs->deck.draw_button)
			    ? (SDL_Color){90, 170, 90, SDL_ALPHA_OPAQUE}
			    : (SDL_Color){140, 220, 140, SDL_ALPHA_OPAQUE};
	} else {
		color = (SDL_Color){120, 205, 120, 220};
	}
	button_draw(as, gs->deck.draw_button, color);
}

static void draw_column(struct app_state *as, struct solitari_state *gs,
			unsigned i, const struct mouse_state *mouse, float dt) {
	struct column_data *column = &gs->column[i];
	const unsigned n = column->num_cards;
	SDL_FRect dest_rect = {.x = column->position.x,
			       .y = column->position.y,
			       .w = gs->card_size.x,
			       .h = gs->card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};
	SDL_FRect visible_rect = {
	    .x = column->position.x, .w = gs->card_size.x, .h = column->margin};
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
			float step =
			    gs->card_size.x * 12 * dt; // FIXME: magic constant
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
		    gs->dragging.num_cards == 0 &&
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

static void draw_column_receiving(struct app_state *as,
				  struct solitari_state *gs, unsigned i) {
	struct column_data *column = &gs->column[i];
	const unsigned n = column->num_cards;
	const unsigned first_receiving = n - column->num_receiving;
	SDL_FRect dest_rect = {.x = column->position_receiving.x,
			       .y = column->position_receiving.y,
			       .w = gs->card_size.x,
			       .h = gs->card_size.y};
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

static void draw_bank(struct app_state *as, struct solitari_state *gs,
		      unsigned i, float dt) {
	struct bank_data *bank = &gs->bank[i];
	SDL_FRect dest_rect = {.x = bank->position.x,
			       .y = bank->position.y,
			       .w = gs->card_size.x,
			       .h = gs->card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};

	card_t c = bank->top_card_receiving;
	if (c != 0) {
		float last_x = bank->position_receiving.x;
		float last_y = bank->position_receiving.y;
		float dist = hypot(dest_rect.x - last_x, dest_rect.y - last_y);
		float move = gs->card_size.x * 12 * dt; // FIXME: magic constant
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

static void draw_bank_receiving(struct app_state *as, struct solitari_state *gs,
				unsigned i) {
	struct bank_data *bank = &gs->bank[i];
	SDL_FRect dest_rect = {.x = bank->position_receiving.x,
			       .y = bank->position_receiving.y,
			       .w = gs->card_size.x,
			       .h = gs->card_size.y};
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

static void draw_floating(struct app_state *as, struct solitari_state *gs,
			  const SDL_FPoint mouse_pos, float dt) {
	if (gs->dragging.num_cards == 0)
		return;
	const float margin = 0.2f * gs->card_size.x;
	float x = mouse_pos.x - gs->card_size.x / 2.f;
	float y = mouse_pos.y - margin / 2;
	if (gs->dragging.animate_to_pointer) {
		const float last_x = gs->dragging.position.x;
		const float last_y = gs->dragging.position.y;
		const float dist = hypot(x - last_x, y - last_y);
		float move = gs->card_size.x * 24 * dt; // FIXME: magic constant
		if (dist <= move) {
			gs->dragging.animate_to_pointer = false;
		} else {
			gs->dragging.position.x += (x - last_x) / dist * move;
			gs->dragging.position.y += (y - last_y) / dist * move;
			x = gs->dragging.position.x;
			y = gs->dragging.position.y;
		}
	}
	const unsigned n = gs->dragging.num_cards;
	SDL_FRect dest_rect = {
	    .x = x, .y = y, .w = gs->card_size.x, .h = gs->card_size.y};
	SDL_FRect origin_rect = {.w = as->card_texture_size.x,
				 .h = as->card_texture_size.y};
	for (unsigned j = 0; j < n; ++j) {
		const card_t c = gs->dragging.cards[j];
		origin_rect.x = as->card_texture_size.x * ((c & VAL_MASK) - 1);
		origin_rect.y = as->card_texture_size.y *
				(((c & SUIT_MASK) >> SUIT_OFFSET) - 1);
		dest_rect.y = y + margin * j;
		SDL_RenderTexture(as->renderer, as->deck_texture, &origin_rect,
				  &dest_rect);
	}
}

void solitari_iterate(struct app_state *appstate) {
	struct solitari_state *gamestate = appstate->solitari;
	struct mouse_state mouse;
	if (gamestate->delayed_dragging.delay_started &&
	    appstate->current_frame_ticks -
		    gamestate->delayed_dragging.delay_started >
		DRAG_START_DELAY_MS)
		start_drag(gamestate, gamestate->delayed_dragging.i,
			   &gamestate->delayed_dragging.pointer);
	const float dt =
	    (appstate->current_frame_ticks - appstate->last_frame_ticks) /
	    1000.f;
	mouse.flags = SDL_GetMouseState(&mouse.pos.x, &mouse.pos.y);
	draw_deck(appstate, gamestate, &mouse);
	for (unsigned i = 0; i < 8; ++i) {
		draw_column(appstate, gamestate, i, &mouse, dt);
	}
	for (unsigned i = 0; i < 4; ++i) {
		draw_bank(appstate, gamestate, i, dt);
	}
	for (unsigned i = 0; i < 8; ++i) {
		if (gamestate->column[i].num_receiving != 0)
			draw_column_receiving(appstate, gamestate, i);
	}
	for (unsigned i = 0; i < 4; ++i) {
		if (gamestate->bank[i].top_card_receiving != 0)
			draw_bank_receiving(appstate, gamestate, i);
	}
	draw_floating(appstate, gamestate, mouse.pos, dt);
}

void solitari_quit(struct solitari_state *gamestate) {
	button_quit(gamestate->deck.draw_button);
	SDL_free(gamestate);
}
