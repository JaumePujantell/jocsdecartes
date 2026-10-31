#include "solitari.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <math.h>
#include <stdbool.h>

#include "app.h"
#include "card_motion.h"
#include "card_texture.h"
#include "cards.h"
#include "gui.h"

struct deck_data {
	card_t cards[SPANISH_DECK_SIZE];
	SDL_FPoint position;
	unsigned num_cards;
	struct button *draw_button;
};

struct column_data {
	card_t cards[SPANISH_DECK_SIZE];
	SDL_FPoint position;
	float margin;
	unsigned num_cards;
	unsigned num_receiving;
	struct card_motion motion;
};

struct delayed_dragging_data {
	Uint64 delay_started;
	unsigned i;
	SDL_FPoint pointer;
};
#define DRAG_START_DELAY_MS 150
#define DRAG_START_MOTION_THRESHOLD 4.F

struct dragging_data {
	card_t cards[NUM_SUIT_CARDS];
	SDL_FPoint position;
	unsigned num_cards;
	unsigned orig_column;
	struct card_motion motion;
};

struct bank_data {
	card_t top_card;
	card_t top_card_receiving;
	SDL_FPoint position;
	struct card_motion motion;
};

#define CARD_FLIGHT_MS 250U
#define DRAG_CATCH_UP_MS 100U

static const SDL_Color deck_empty_color = {100, 100, 100, 200};
static const SDL_Color deck_button_color = {120, 205, 120, 220};
static const SDL_Color deck_button_hover_color = {140, 220, 140,
						  SDL_ALPHA_OPAQUE};

#define NUM_COLUMNS 8U
#define CARD_SHOW_MARGIN 0.2F

struct solitari_state {
	struct deck_data deck;
	struct column_data column[NUM_COLUMNS];
	struct bank_data bank[NUM_SUITS];
	struct dragging_data dragging;
	struct delayed_dragging_data delayed_dragging;
	SDL_FPoint card_size;
};

static bool card_fits_on_card(card_t card, card_t target) {
	return (card & VAL_MASK) + 1 == (target & VAL_MASK) &&
	       is_short_suit(card) != is_short_suit(target);
}

static SDL_FRect card_rect(const struct solitari_state *gs,
			   SDL_FPoint position) {
	return (SDL_FRect){.x = position.x,
			   .y = position.y,
			   .w = gs->card_size.x,
			   .h = gs->card_size.y};
}

static SDL_FRect column_rect(const struct solitari_state *gs,
			     const struct column_data *column) {
	const unsigned n = column->num_cards == 0 ? 1 : column->num_cards;
	SDL_FRect rect = card_rect(gs, column->position);
	rect.h += (float)(n - 1) * column->margin;
	return rect;
}

static SDL_FPoint drag_position(const struct solitari_state *gs,
				SDL_FPoint point) {
	const float margin = CARD_SHOW_MARGIN * gs->card_size.x;
	return (SDL_FPoint){point.x - (gs->card_size.x / 2.F),
			    point.y - (margin / 2.F)};
}

static SDL_FPoint first_receiving_position(const struct column_data *column) {
	const unsigned first_receiving =
	    column->num_cards - column->num_receiving;
	return (SDL_FPoint){column->position.x,
			    column->position.y +
				((float)first_receiving * column->margin)};
}

static void deal_cards(struct solitari_state *gs) {
	for (unsigned i = 0; i < NUM_COLUMNS && gs->deck.num_cards > 0; ++i) {
		struct column_data *column = &gs->column[i];
		--gs->deck.num_cards;
		column->cards[column->num_cards] =
		    gs->deck.cards[gs->deck.num_cards];
		++column->num_cards;
		column->num_receiving = 1;
		card_motion_start(&column->motion, gs->deck.position, 0.0,
				  first_receiving_position(column),
				  gs->card_size.x, CARD_FLIGHT_MS);
	}
}

static void deck_button_clicked(void *userdata) {
	deal_cards((struct solitari_state *)userdata);
}

void solitari_restart(struct solitari_state *gamestate) {
	fill_spanish_deck(gamestate->deck.cards);
	gamestate->deck.num_cards = SPANISH_DECK_SIZE;
	shuffle(gamestate->deck.cards, SPANISH_DECK_SIZE);
	float margin = CARD_SHOW_MARGIN * gamestate->card_size.x;
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		gamestate->column[i].num_cards = 0;
		gamestate->column[i].margin = margin;
	}
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		gamestate->bank[i].top_card = 0;
		gamestate->bank[i].top_card_receiving = 0;
		card_motion_stop(&gamestate->bank[i].motion);
	}
	gamestate->dragging.num_cards = 0;
	card_motion_stop(&gamestate->dragging.motion);
	gamestate->delayed_dragging.delay_started = 0;
	deal_cards(gamestate);
}

#define BLOCK_MARGIN 0.1F
#define OUTER_MARGIN 0.2F
#define NUM_SHOWN_CARDS 6

void solitari_resize(struct app_state *appstate) {
	struct solitari_state *gs = appstate->solitari;
	float card_ratio = card_texture_aspect_ratio(appstate->card_texture);
	const float window_width = (float)appstate->window_size.x;
	const float window_height = (float)appstate->window_size.y;
	float x1 =
	    window_width /
	    (NUM_COLUMNS + BLOCK_MARGIN * (NUM_COLUMNS - 1) + 2 * OUTER_MARGIN);
	float x2 = window_height /
		   (2 * OUTER_MARGIN + 2 + BLOCK_MARGIN +
		    NUM_SHOWN_CARDS * CARD_SHOW_MARGIN) *
		   card_ratio;

	gs->card_size.x = fminf(x1, x2);
	gs->card_size.y = gs->card_size.x / card_ratio;
	float margin = CARD_SHOW_MARGIN * gs->card_size.x;

	float left = (window_width -
		      (gs->card_size.x *
		       (NUM_COLUMNS + BLOCK_MARGIN * (NUM_COLUMNS - 1)))) /
		     2.F;
	float top = margin * 2.F;

	button_fit_width(gs->deck.draw_button, gs->card_size.x / 2.F);

	gs->deck.position.x = left;
	gs->deck.position.y = top;
	SDL_FPoint button_dim = button_get_size(gs->deck.draw_button);
	button_set_position(
	    gs->deck.draw_button,
	    gs->deck.position.x + ((gs->card_size.x - button_dim.x) / 2.F),
	    gs->deck.position.y + ((gs->card_size.y - button_dim.y) / 2.F));
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		gs->column[i].position.x =
		    left + ((float)i * gs->card_size.x * (1 + BLOCK_MARGIN));
		gs->column[i].position.y =
		    top + (gs->card_size.y * (1 + BLOCK_MARGIN));
		gs->column[i].margin = margin;
	}
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		gs->bank[i].position.x =
		    left + ((float)(i + NUM_COLUMNS - NUM_SUITS) *
			    gs->card_size.x * (1 + BLOCK_MARGIN));
		gs->bank[i].position.y = top;
	}
}

bool solitari_init(struct app_state *appstate) {
	struct gui_state *guistate = appstate->gui;
	struct solitari_state *gs = SDL_calloc(1, sizeof(*gs));
	if (!gs) {
		return false;
	}

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

static unsigned num_draggable_cards(const struct column_data *column) {
	const card_t *cards = column->cards;
	unsigned n = column->num_cards == 0 ? 0 : 1;
	while (n < column->num_cards &&
	       card_fits_on_card(cards[column->num_cards - n],
				 cards[column->num_cards - n - 1])) {
		++n;
	}
	return n;
}

static bool card_fits_on_bank(card_t bank_top, card_t card) {
	return (bank_top == 0 && (card & VAL_MASK) == 1) ||
	       bank_top + 1 == card;
}

static unsigned card_index_at(const struct column_data *column,
			      const SDL_FPoint *p) {
	const unsigned last = column->num_cards - 1;
	const float strip =
	    floorf((p->y - column->position.y) / column->margin);
	if (strip <= 0.F) {
		return 0;
	}
	return strip >= (float)last ? last : (unsigned)strip;
}

static void start_drag(struct solitari_state *gs, unsigned i,
		       const SDL_FPoint *p) {
	gs->delayed_dragging.delay_started = 0;
	struct column_data *column = &gs->column[i];
	struct dragging_data *dragging = &gs->dragging;
	if (column->num_cards == 0 || card_motion_is_moving(&column->motion)) {
		return;
	}
	const unsigned n = card_index_at(column, p);
	if (column->num_cards - n > num_draggable_cards(column)) {
		return;
	}
	dragging->num_cards = column->num_cards - n;
	dragging->position.x = column->position.x;
	dragging->position.y = column->position.y + ((float)n * column->margin);
	card_motion_start(&dragging->motion, dragging->position, 0.0,
			  drag_position(gs, *p), gs->card_size.x,
			  DRAG_CATCH_UP_MS);
	column->num_cards = n;
	for (unsigned j = 0; j < dragging->num_cards; ++j) {
		dragging->cards[j] = column->cards[n + j];
	}
	dragging->orig_column = i;
}

static void start_drag_delayed(struct solitari_state *gs, unsigned i,
			       const SDL_FPoint *p) {
	gs->delayed_dragging.delay_started = SDL_GetTicks();
	gs->delayed_dragging.i = i;
	gs->delayed_dragging.pointer = *p;
}

static bool is_bottom_card_click(const struct column_data *column,
				 const SDL_FPoint *p) {
	return card_index_at(column, p) == column->num_cards - 1;
}

static bool try_move_bottom_card_to_bank(struct solitari_state *gs,
					 unsigned i) {
	struct column_data *column = &gs->column[i];
	if (column->num_cards == 0) {
		return false;
	}
	card_t dc = column->cards[column->num_cards - 1];
	for (unsigned b = 0; b < NUM_SUITS; ++b) {
		if (card_motion_is_moving(&gs->bank[b].motion) ||
		    !card_fits_on_bank(gs->bank[b].top_card, dc)) {
			continue;
		}
		gs->bank[b].top_card_receiving = dc;
		const SDL_FPoint from = {
		    column->position.x,
		    column->position.y +
			((float)(column->num_cards - 1) * column->margin)};
		card_motion_start(&gs->bank[b].motion, from, 0.0,
				  gs->bank[b].position, gs->card_size.x,
				  CARD_FLIGHT_MS);
		--column->num_cards;
		if (column->num_receiving > 0) {
			--column->num_receiving;
			if (column->num_receiving == 0) {
				card_motion_stop(&column->motion);
			}
		}
		return true;
	}
	return false;
}

static bool drop_on_bank(struct solitari_state *gs, const SDL_FPoint *p,
			 SDL_FPoint drop_position) {
	const card_t dc = gs->dragging.cards[0];
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		struct bank_data *bank = &gs->bank[i];
		if (card_motion_is_moving(&bank->motion) ||
		    !card_fits_on_bank(bank->top_card, dc)) {
			continue;
		}
		const SDL_FRect rect = card_rect(gs, bank->position);
		if (SDL_PointInRectFloat(p, &rect)) {
			bank->top_card_receiving = dc;
			card_motion_start(&bank->motion, drop_position, 0.0,
					  bank->position, gs->card_size.x,
					  CARD_FLIGHT_MS);
			return true;
		}
	}
	return false;
}

static unsigned drop_column(const struct solitari_state *gs,
			    const SDL_FPoint *p) {
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		const struct column_data *column = &gs->column[i];
		const SDL_FRect rect = column_rect(gs, column);
		if (!SDL_PointInRectFloat(p, &rect)) {
			continue;
		}
		if (column->num_cards == 0 ||
		    card_fits_on_card(gs->dragging.cards[0],
				      column->cards[column->num_cards - 1])) {
			return i;
		}
		break;
	}
	return gs->dragging.orig_column;
}

static void end_drag(struct solitari_state *gs, const SDL_FPoint *p) {
	const unsigned num_cards = gs->dragging.num_cards;
	const SDL_FPoint drop_position =
	    card_motion_is_moving(&gs->dragging.motion) ? gs->dragging.position
							: drag_position(gs, *p);
	gs->dragging.num_cards = 0;
	if (num_cards == 1 && drop_on_bank(gs, p, drop_position)) {
		return;
	}
	struct column_data *column = &gs->column[drop_column(gs, p)];
	const unsigned n = column->num_cards;
	for (unsigned i = 0; i < num_cards; ++i) {
		column->cards[n + i] = gs->dragging.cards[i];
	}
	column->num_cards += num_cards;
	column->num_receiving = num_cards;
	card_motion_start(&column->motion, drop_position, 0.0,
			  first_receiving_position(column), gs->card_size.x,
			  CARD_FLIGHT_MS);
}

static void click_column(struct solitari_state *gs, unsigned i,
			 const SDL_FPoint *p, Uint8 clicks) {
	const bool bottom_card = is_bottom_card_click(&gs->column[i], p);
	if (bottom_card && clicks == 1) {
		start_drag_delayed(gs, i, p);
	} else if (!bottom_card || !try_move_bottom_card_to_bank(gs, i)) {
		start_drag(gs, i, p);
	}
}

static void mouse_button_down(struct solitari_state *gs,
			      const SDL_Event *event) {
	if (event->button.button != SDL_BUTTON_LEFT ||
	    button_event(gs->deck.draw_button, event)) {
		return;
	}
	const SDL_FPoint p = {event->button.x, event->button.y};
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		const struct column_data *column = &gs->column[i];
		if (column->num_cards == 0) {
			continue;
		}
		const SDL_FRect rect = column_rect(gs, column);
		if (SDL_PointInRectFloat(&p, &rect)) {
			click_column(gs, i, &p, event->button.clicks);
			return;
		}
	}
}

static void mouse_motion(struct solitari_state *gs, const SDL_Event *event) {
	if (!gs->delayed_dragging.delay_started) {
		return;
	}
	float dist = hypotf(event->motion.x - gs->delayed_dragging.pointer.x,
			    event->motion.y - gs->delayed_dragging.pointer.y);
	if (dist > DRAG_START_MOTION_THRESHOLD) {
		start_drag(gs, gs->delayed_dragging.i,
			   &gs->delayed_dragging.pointer);
	}
}

static void mouse_button_up(struct solitari_state *gs, const SDL_Event *event) {
	if (event->button.button != SDL_BUTTON_LEFT) {
		return;
	}
	gs->delayed_dragging.delay_started = 0;
	if (button_event(gs->deck.draw_button, event)) {
		return;
	}
	const SDL_FPoint p = {event->button.x, event->button.y};
	if (gs->dragging.num_cards != 0) {
		end_drag(gs, &p);
	}
}

void solitari_event(struct app_state *appstate, SDL_Event *event) {
	struct solitari_state *gamestate = appstate->solitari;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		mouse_button_down(gamestate, event);
		break;
	case SDL_EVENT_MOUSE_MOTION:
		mouse_motion(gamestate, event);
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		mouse_button_up(gamestate, event);
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		solitari_resize(appstate);
		break;
	default:
		break;
	}
}

static void draw_deck(struct app_state *as, struct solitari_state *gs,
		      SDL_FPoint mouse) {
	const SDL_FRect dest_rect = card_rect(gs, gs->deck.position);
	card_texture_draw(as->card_texture,
			  gs->deck.num_cards == 0 ? CARD_OUTLINE : CARD_BACK,
			  &dest_rect);
	SDL_Color color;
	if (gs->deck.num_cards == 0) {
		color = deck_empty_color;
	} else if (gs->dragging.num_cards == 0 &&
		   button_is_hovered(gs->deck.draw_button, mouse)) {
		color = button_is_pressing(gs->deck.draw_button)
			    ? green_button_pressed_color
			    : deck_button_hover_color;
	} else {
		color = deck_button_color;
	}
	button_draw(as, gs->deck.draw_button, color);
}

static void update_column(struct solitari_state *gs, unsigned i, Uint64 now) {
	struct column_data *column = &gs->column[i];
	if (card_motion_arrived(&column->motion, now)) {
		column->num_receiving = 0;
	}
}

static void update_bank(struct solitari_state *gs, unsigned i, Uint64 now) {
	struct bank_data *bank = &gs->bank[i];
	if (card_motion_arrived(&bank->motion, now)) {
		bank->top_card = bank->top_card_receiving;
		bank->top_card_receiving = 0;
	}
}

static void update_floating(struct solitari_state *gs, SDL_FPoint mouse_pos,
			    Uint64 now) {
	struct dragging_data *dragging = &gs->dragging;
	if (dragging->num_cards == 0) {
		return;
	}
	card_motion_arrived(&dragging->motion, now);
	dragging->position = card_motion_position(
	    &dragging->motion, drag_position(gs, mouse_pos), now);
}

static void solitari_update(struct app_state *appstate, SDL_FPoint mouse_pos) {
	struct solitari_state *gamestate = appstate->solitari;
	if (gamestate->delayed_dragging.delay_started &&
	    appstate->current_frame_ticks -
		    gamestate->delayed_dragging.delay_started >
		DRAG_START_DELAY_MS) {
		start_drag(gamestate, gamestate->delayed_dragging.i,
			   &gamestate->delayed_dragging.pointer);
	}
	const Uint64 now = appstate->current_frame_ticks;
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		update_column(gamestate, i, now);
	}
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		update_bank(gamestate, i, now);
	}
	update_floating(gamestate, mouse_pos, now);
}

static void draw_column(struct app_state *as, struct solitari_state *gs,
			unsigned i, SDL_FPoint mouse) {
	struct column_data *column = &gs->column[i];
	const unsigned n = column->num_cards;
	SDL_FRect dest_rect = card_rect(gs, column->position);
	SDL_FRect visible_rect = {
	    .x = column->position.x, .w = gs->card_size.x, .h = column->margin};
	if (n == 0 || n == column->num_receiving) {
		card_texture_draw(as->card_texture, CARD_OUTLINE, &dest_rect);
		if (n == 0) {
			return;
		}
	}

	bool draw_extra = false;
	card_t draw_extra_card;
	SDL_FRect draw_extra_dest;
	const unsigned num_settled = n - column->num_receiving;
	for (unsigned j = 0; j < num_settled; ++j) {
		card_texture_draw(as->card_texture, column->cards[j],
				  &dest_rect);
		visible_rect.y = dest_rect.y;
		if (column->num_receiving == 0 && j < n - 1 &&
		    gs->dragging.num_cards == 0 &&
		    SDL_PointInRectFloat(&mouse, &visible_rect)) {
			draw_extra = true;
			draw_extra_card = column->cards[j];
			draw_extra_dest = dest_rect;
		}
		dest_rect.y += column->margin;
	}
	if (draw_extra) {
		card_texture_draw(as->card_texture, draw_extra_card,
				  &draw_extra_dest);
	}
}

static void draw_column_receiving(struct app_state *as,
				  struct solitari_state *gs, unsigned i) {
	struct column_data *column = &gs->column[i];
	const unsigned n = column->num_cards;
	const unsigned first_receiving = n - column->num_receiving;
	const SDL_FPoint position = card_motion_position(
	    &column->motion, first_receiving_position(column),
	    as->current_frame_ticks);
	SDL_FRect dest_rect = card_rect(gs, position);
	for (unsigned j = first_receiving; j < n; ++j) {
		card_texture_draw(as->card_texture, column->cards[j],
				  &dest_rect);
		dest_rect.y += column->margin;
	}
}

static void draw_bank(struct app_state *as, struct solitari_state *gs,
		      unsigned i) {
	struct bank_data *bank = &gs->bank[i];
	const SDL_FRect dest_rect = card_rect(gs, bank->position);
	const card_t c = bank->top_card;
	card_texture_draw(as->card_texture, c == 0 ? CARD_OUTLINE : c,
			  &dest_rect);
}

static void draw_bank_receiving(struct app_state *as, struct solitari_state *gs,
				unsigned i) {
	struct bank_data *bank = &gs->bank[i];
	const SDL_FPoint position = card_motion_position(
	    &bank->motion, bank->position, as->current_frame_ticks);
	const SDL_FRect dest_rect = card_rect(gs, position);
	card_texture_draw(as->card_texture, bank->top_card_receiving,
			  &dest_rect);
}

static void draw_floating(struct app_state *as, struct solitari_state *gs) {
	const unsigned n = gs->dragging.num_cards;
	const float margin = CARD_SHOW_MARGIN * gs->card_size.x;
	SDL_FRect dest_rect = card_rect(gs, gs->dragging.position);
	for (unsigned j = 0; j < n; ++j) {
		dest_rect.y = gs->dragging.position.y + (margin * (float)j);
		card_texture_draw(as->card_texture, gs->dragging.cards[j],
				  &dest_rect);
	}
}

void solitari_iterate(struct app_state *appstate) {
	struct solitari_state *gamestate = appstate->solitari;
	SDL_FPoint mouse;
	SDL_GetMouseState(&mouse.x, &mouse.y);
	solitari_update(appstate, mouse);
	draw_deck(appstate, gamestate, mouse);
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		draw_column(appstate, gamestate, i, mouse);
	}
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		draw_bank(appstate, gamestate, i);
	}
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		if (card_motion_is_moving(&gamestate->column[i].motion)) {
			draw_column_receiving(appstate, gamestate, i);
		}
	}
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		if (card_motion_is_moving(&gamestate->bank[i].motion)) {
			draw_bank_receiving(appstate, gamestate, i);
		}
	}
	draw_floating(appstate, gamestate);
}

void solitari_quit(struct solitari_state *gamestate) {
	button_quit(gamestate->deck.draw_button);
	SDL_free(gamestate);
}
