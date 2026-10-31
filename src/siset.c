#include "siset.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include <math.h>
#include <stdbool.h>

#include "app.h"
#include "card_motion.h"
#include "card_texture.h"
#include "cards.h"
#include "gui.h"
#include "table.h"

#define SIX 6U
#define NUM_COLUMNS NUM_SUITS
#define NO_COLUMN NUM_COLUMNS
#define DEALT_DECK_SIZE (SPANISH_DECK_SIZE - NUM_COLUMNS)

#define CARD_DEAL_MS 120U
#define CARD_PLAY_MS 400U
#define BOT_STEP_MS 700U

#define COLUMN_GAP 0.3F
#define COLUMN_SLOT_OFFSET 0.35F
#define DRAG_START_DISTANCE 0.1F
#define DRAG_PLAY_DISTANCE 0.5F
#define ACTION_GAP 0.3F
#define BUTTON_WIDTH 1.6F
#define TURN_HINT_WIDTH 2.F

struct column_data {
	unsigned suit;
	unsigned low;
	unsigned high;
};

enum column_slot { HIGH_SLOT = -1, SIX_SLOT = 0, LOW_SLOT = 1 };

struct player_data {
	card_t cards[SPANISH_DECK_SIZE];
	unsigned num_cards;
	unsigned num_receiving;
	unsigned place;
};

struct held_data {
	unsigned index;
	bool was_selected;
	bool dragging;
	SDL_FPoint press_point;
	SDL_FPoint pointer;
	SDL_FPoint grab_offset;
};

struct siset_state {
	card_t deck[SPANISH_DECK_SIZE];
	unsigned deck_size;
	struct player_data player[NUM_PLAYERS];
	struct column_data columns[NUM_COLUMNS];
	enum player_id dealing_to;
	enum player_id current_player;
	unsigned selected;
	struct held_data held;
	unsigned num_out;
	bool game_over;
	Uint64 bot_ready_ticks;
	struct flying_data flying;
	struct card_motion play_motion;
	card_t playing_card;
	unsigned playing_column;
	bool choosing_options;
	unsigned num_players;
	SDL_FPoint card_size;
	struct players_options options;
	struct label *turn_hint_label;
	struct button *pass_button;
	struct button *new_game_button;
	struct label *place_labels[NUM_PLACES];
};

static void fill_deck_and_columns(struct siset_state *gamestate) {
	card_t *deck = gamestate->deck;
	fill_spanish_deck(deck);
	unsigned last_position = SPANISH_DECK_SIZE - 1;
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		struct column_data *column = &gamestate->columns[i];
		column->suit = suits[i];
		column->high = SIX;
		column->low = SIX;
		deck[(i * NUM_SUIT_CARDS) + SIX - 1] = deck[last_position--];
	}
}

static void start_game(struct siset_state *gamestate) {
	gamestate->choosing_options = false;
	const unsigned players = players_in_input(gamestate->options.input);
	if (players != 0) {
		gamestate->num_players = players;
	}
	fill_deck_and_columns(gamestate);
	shuffle(gamestate->deck, DEALT_DECK_SIZE);
	gamestate->deck_size = DEALT_DECK_SIZE;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		gamestate->player[i].num_cards = 0;
		gamestate->player[i].num_receiving = 0;
		gamestate->player[i].place = 0;
	}
	card_motion_stop(&gamestate->flying.motion);
	card_motion_stop(&gamestate->play_motion);
	const enum player_id first =
	    random_active_player(gamestate->num_players);
	gamestate->dealing_to = first;
	gamestate->current_player = first;
	gamestate->num_out = 0;
	gamestate->selected = NO_CARD_INDEX;
	gamestate->held.index = NO_CARD_INDEX;
	gamestate->game_over = false;
}

void siset_restart(struct siset_state *gamestate) { start_game(gamestate); }

void siset_choose_options(struct siset_state *gamestate) {
	gamestate->choosing_options = true;
}

bool siset_is_choosing_options(const struct siset_state *gamestate) {
	return gamestate->choosing_options;
}

static void start_game_clicked(void *userdata) {
	start_game((struct siset_state *)userdata);
}

static void position_actions(struct app_state *appstate) {
	struct siset_state *gs = appstate->siset;
	const float window_width = (float)appstate->window_size.x;
	const float bottom =
	    (float)appstate->window_size.y -
	    ((1.F + OUTER_MARGIN + ACTION_GAP) * gs->card_size.y);
	label_fit_width(gs->turn_hint_label, TURN_HINT_WIDTH * gs->card_size.x);
	const SDL_FPoint hint_size = label_get_size(gs->turn_hint_label);
	label_set_position(gs->turn_hint_label,
			   (window_width - hint_size.x) / 2.F,
			   bottom - hint_size.y);
	struct button *buttons[] = {gs->pass_button, gs->new_game_button};
	button_group_fit_width(buttons, SDL_arraysize(buttons),
			       BUTTON_WIDTH * gs->card_size.x);
	for (unsigned i = 0; i < SDL_arraysize(buttons); ++i) {
		const SDL_FPoint size = button_get_size(buttons[i]);
		button_set_position(buttons[i], (window_width - size.x) / 2.F,
				    bottom - size.y);
	}
}

void siset_resize(struct app_state *appstate) {
	struct siset_state *gs = appstate->siset;
	gs->card_size = table_card_size(appstate);
	players_options_position(appstate, &gs->options);
	position_actions(appstate);
	place_labels_fit(gs->place_labels, gs->card_size);
}

static void quit_gui(struct app_state *appstate, struct siset_state *gs) {
	players_options_quit(appstate, &gs->options);
	place_labels_quit(gs->place_labels);
	if (gs->turn_hint_label) {
		label_quit(gs->turn_hint_label);
	}
	struct button *buttons[] = {gs->pass_button, gs->new_game_button};
	for (unsigned i = 0; i < SDL_arraysize(buttons); ++i) {
		if (buttons[i]) {
			button_quit(buttons[i]);
		}
	}
}

static void pass_clicked(void *userdata);

static bool init_gui(struct app_state *appstate, struct siset_state *gs) {
	struct gui_state *gui = appstate->gui;
	const bool options_ok =
	    players_options_init(gui, &gs->options, start_game_clicked, gs);
	gs->turn_hint_label = label_init(gui, "És el teu torn");
	gs->pass_button = button_init(gui, "Passa", pass_clicked, appstate);
	gs->new_game_button =
	    button_init(gui, "Nova partida", start_game_clicked, gs);
	const bool places_ok = place_labels_init(gui, gs->place_labels);
	return options_ok && places_ok && gs->turn_hint_label &&
	       gs->pass_button && gs->new_game_button;
}

bool siset_init(struct app_state *appstate) {
	struct siset_state *gs = SDL_calloc(1, sizeof(*gs));
	if (!gs) {
		return false;
	}
	gs->num_players = NUM_PLAYERS;
	if (!init_gui(appstate, gs)) {
		quit_gui(appstate, gs);
		SDL_free(gs);
		return false;
	}
	appstate->siset = gs;

	siset_resize(appstate);
	siset_choose_options(gs);
	return true;
}

static SDL_FRect column_slot_rect(const struct app_state *as, unsigned column,
				  enum column_slot slot) {
	const SDL_FPoint card_size = as->siset->card_size;
	const float gap = COLUMN_GAP * card_size.x;
	const float row_width = ((float)NUM_COLUMNS * card_size.x) +
				((float)(NUM_COLUMNS - 1) * gap);
	return (SDL_FRect){
	    .x = (((float)as->window_size.x - row_width) / 2.F) +
		 ((float)column * (card_size.x + gap)),
	    .y = (((float)as->window_size.y - card_size.y) / 2.F) +
		 ((float)slot * COLUMN_SLOT_OFFSET * card_size.y),
	    .w = card_size.x,
	    .h = card_size.y,
	};
}

static SDL_FRect deck_rect(const struct app_state *as) {
	const SDL_FPoint card_size = as->siset->card_size;
	return (SDL_FRect){
	    .x = ((float)as->window_size.x - card_size.x) / 2.F,
	    .y = ((float)as->window_size.y - card_size.y) / 2.F,
	    .w = card_size.x,
	    .h = card_size.y,
	};
}

static enum column_slot card_slot(card_t card) {
	const unsigned value = card & VAL_MASK;
	if (value < SIX) {
		return LOW_SLOT;
	}
	return value == SIX ? SIX_SLOT : HIGH_SLOT;
}

static bool card_fits_column(const struct siset_state *gs, card_t card,
			     unsigned i) {
	const struct column_data *column = &gs->columns[i];
	const unsigned value = card & VAL_MASK;
	return column->suit == (card & SUIT_MASK) &&
	       (value + 1 == column->low || value == column->high + 1);
}

static unsigned target_column(const struct siset_state *gs, card_t card) {
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		if (card_fits_column(gs, card, i)) {
			return i;
		}
	}
	return NO_COLUMN;
}

static bool has_valid_play(const struct siset_state *gs, enum player_id id) {
	const struct player_data *player = &gs->player[id];
	for (unsigned j = 0; j < player->num_cards; ++j) {
		if (target_column(gs, player->cards[j]) != NO_COLUMN) {
			return true;
		}
	}
	return false;
}

static bool in_game(const struct siset_state *gs, enum player_id id) {
	return player_is_active(gs->num_players, id) &&
	       gs->player[id].place == 0;
}

static unsigned num_in_game(const struct siset_state *gs) {
	unsigned count = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		count += in_game(gs, (enum player_id)i) ? 1 : 0;
	}
	return count;
}

static enum player_id next_player(const struct siset_state *gs,
				  enum player_id id) {
	do {
		id = (enum player_id)((id + 1) % NUM_PLAYERS);
	} while (!in_game(gs, id));
	return id;
}

static void deal_card(struct app_state *as) {
	struct siset_state *gs = as->siset;
	struct player_data *player = &gs->player[gs->dealing_to];
	--gs->deck_size;
	player->cards[player->num_cards] = gs->deck[gs->deck_size];
	++player->num_cards;
	player->num_receiving = 1;
	const SDL_FRect start = deck_rect(as);
	start_flight_to_hand(as, gs->card_size, &gs->flying, gs->dealing_to,
			     player->num_cards, (SDL_FPoint){start.x, start.y},
			     0.0, CARD_DEAL_MS);
	gs->dealing_to = next_player(gs, gs->dealing_to);
}

static int compare_cards(const void *a, const void *b) {
	return (int)*(const card_t *)a - (int)*(const card_t *)b;
}

static void finish_dealing(struct siset_state *gs, Uint64 now) {
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		struct player_data *player = &gs->player[i];
		SDL_qsort(player->cards, player->num_cards,
			  sizeof(player->cards[0]), compare_cards);
	}
	gs->bot_ready_ticks = now + BOT_STEP_MS;
}

static bool is_dealing(const struct siset_state *gs) {
	return gs->deck_size > 0 || card_motion_is_moving(&gs->flying.motion);
}

static bool user_can_act(const struct siset_state *gs) {
	return !gs->game_over && !is_dealing(gs) &&
	       !card_motion_is_moving(&gs->play_motion) &&
	       gs->current_player == USER;
}

static bool user_must_pass(const struct siset_state *gs) {
	return user_can_act(gs) && !has_valid_play(gs, USER);
}

static void end_turn(struct siset_state *gs, Uint64 now) {
	gs->current_player = next_player(gs, gs->current_player);
	gs->selected = NO_CARD_INDEX;
	gs->held.index = NO_CARD_INDEX;
	gs->bot_ready_ticks = now + BOT_STEP_MS;
}

static void play_card(struct app_state *as, enum player_id id, unsigned j,
		      unsigned column, SDL_FPoint from, double from_angle) {
	struct siset_state *gs = as->siset;
	struct player_data *player = &gs->player[id];
	gs->playing_card = player->cards[j];
	gs->playing_column = column;
	--player->num_cards;
	SDL_memmove(&player->cards[j], &player->cards[j + 1],
		    (player->num_cards - j) * sizeof(player->cards[0]));
	gs->selected = NO_CARD_INDEX;
	gs->held.index = NO_CARD_INDEX;
	const SDL_FRect end =
	    column_slot_rect(as, column, card_slot(gs->playing_card));
	card_motion_start(&gs->play_motion, from, from_angle,
			  (SDL_FPoint){end.x, end.y}, gs->card_size.x,
			  CARD_PLAY_MS);
}

static void play_hand_card(struct app_state *as, enum player_id id, unsigned j,
			   unsigned column) {
	const struct siset_state *gs = as->siset;
	const struct card_placement from =
	    hand_card(as, gs->card_size, id, j, gs->player[id].num_cards);
	play_card(as, id, j, column, (SDL_FPoint){from.rect.x, from.rect.y},
		  from.angle);
}

static void land_played_card(struct siset_state *gs, Uint64 now) {
	struct column_data *column = &gs->columns[gs->playing_column];
	const unsigned value = gs->playing_card & VAL_MASK;
	if (value < SIX) {
		column->low = value;
	} else {
		column->high = value;
	}
	struct player_data *player = &gs->player[gs->current_player];
	if (player->num_cards == 0) {
		player->place = ++gs->num_out;
		if (num_in_game(gs) <= 1) {
			gs->game_over = true;
			return;
		}
	}
	end_turn(gs, now);
}

static void bot_step(struct app_state *as) {
	struct siset_state *gs = as->siset;
	const enum player_id id = gs->current_player;
	const struct player_data *player = &gs->player[id];
	unsigned playable[SPANISH_DECK_SIZE];
	unsigned num_playable = 0;
	for (unsigned j = 0; j < player->num_cards; ++j) {
		if (target_column(gs, player->cards[j]) != NO_COLUMN) {
			playable[num_playable++] = j;
		}
	}
	if (num_playable == 0) {
		end_turn(gs, as->current_frame_ticks);
		return;
	}
	const unsigned j = playable[SDL_rand((Sint32)num_playable)];
	play_hand_card(as, id, j, target_column(gs, player->cards[j]));
}

static void siset_update(struct app_state *as) {
	struct siset_state *gs = as->siset;
	const Uint64 now = as->current_frame_ticks;
	if (card_motion_is_moving(&gs->play_motion)) {
		if (!card_motion_arrived(&gs->play_motion, now)) {
			return;
		}
		land_played_card(gs, now);
	}
	if (card_motion_is_moving(&gs->flying.motion)) {
		if (!card_motion_arrived(&gs->flying.motion, now)) {
			return;
		}
		gs->player[gs->flying.player].num_receiving = 0;
		if (gs->deck_size == 0) {
			finish_dealing(gs, now);
		}
	}
	if (gs->deck_size > 0) {
		deal_card(as);
		return;
	}
	if (gs->game_over || gs->current_player == USER ||
	    now < gs->bot_ready_ticks) {
		return;
	}
	bot_step(as);
}

static void pass_clicked(void *userdata) {
	struct app_state *as = userdata;
	if (user_must_pass(as->siset)) {
		end_turn(as->siset, as->current_frame_ticks);
	}
}

static void hold_card(struct app_state *as, unsigned j, SDL_FPoint point) {
	struct siset_state *gs = as->siset;
	struct held_data *held = &gs->held;
	const struct card_placement placement =
	    hand_card(as, gs->card_size, USER, j, gs->player[USER].num_cards);
	held->index = j;
	held->was_selected = gs->selected == j;
	held->dragging = false;
	held->press_point = point;
	held->pointer = point;
	held->grab_offset = (SDL_FPoint){
	    point.x - placement.rect.x,
	    point.y - placement.rect.y,
	};
	gs->selected = j;
}

static bool try_play_on_slot(struct app_state *as, SDL_FPoint point) {
	const struct siset_state *gs = as->siset;
	if (gs->selected == NO_CARD_INDEX) {
		return false;
	}
	const card_t card = gs->player[USER].cards[gs->selected];
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		const SDL_FRect rect = column_slot_rect(as, i, card_slot(card));
		if (card_fits_column(gs, card, i) &&
		    SDL_PointInRectFloat(&point, &rect)) {
			play_hand_card(as, USER, gs->selected, i);
			return true;
		}
	}
	return false;
}

static bool play_mouse_down(struct app_state *as, const SDL_Event *event) {
	struct siset_state *gs = as->siset;
	if (gs->game_over) {
		return button_event(gs->new_game_button, event);
	}
	if (!user_can_act(gs) || event->button.button != SDL_BUTTON_LEFT) {
		return false;
	}
	if (user_must_pass(gs) && button_event(gs->pass_button, event)) {
		return true;
	}
	const SDL_FPoint point = {event->button.x, event->button.y};
	const unsigned j =
	    hand_card_at(as, gs->card_size, USER, gs->player[USER].num_cards,
			 gs->selected, point);
	if (j != NO_CARD_INDEX) {
		hold_card(as, j, point);
		return true;
	}
	if (try_play_on_slot(as, point)) {
		return true;
	}
	gs->selected = NO_CARD_INDEX;
	return false;
}

static void play_mouse_motion(struct app_state *as, const SDL_Event *event) {
	struct held_data *held = &as->siset->held;
	if (held->index == NO_CARD_INDEX) {
		return;
	}
	held->pointer = (SDL_FPoint){event->motion.x, event->motion.y};
	const float distance = hypotf(held->pointer.x - held->press_point.x,
				      held->pointer.y - held->press_point.y);
	if (distance > DRAG_START_DISTANCE * as->siset->card_size.x) {
		held->dragging = true;
	}
}

static SDL_FPoint held_card_position(const struct held_data *held) {
	return (SDL_FPoint){
	    held->pointer.x - held->grab_offset.x,
	    held->pointer.y - held->grab_offset.y,
	};
}

static void play_mouse_up(struct app_state *as, const SDL_Event *event) {
	struct siset_state *gs = as->siset;
	struct held_data *held = &gs->held;
	if (held->index == NO_CARD_INDEX ||
	    event->button.button != SDL_BUTTON_LEFT) {
		return;
	}
	const unsigned j = held->index;
	held->index = NO_CARD_INDEX;
	if (!user_can_act(gs)) {
		return;
	}
	const unsigned column = target_column(gs, gs->player[USER].cards[j]);
	if (held->dragging) {
		const float hand_top = hand_card(as, gs->card_size, USER, 0,
						 gs->player[USER].num_cards)
					   .rect.y;
		const float release_y = event->button.y;
		if (column != NO_COLUMN &&
		    release_y <
			hand_top - (DRAG_PLAY_DISTANCE * gs->card_size.y)) {
			play_card(as, USER, j, column, held_card_position(held),
				  0.0);
		}
	} else if (held->was_selected) {
		if (column != NO_COLUMN) {
			play_hand_card(as, USER, j, column);
		} else {
			gs->selected = NO_CARD_INDEX;
		}
	}
}

void siset_event(struct app_state *appstate, SDL_Event *event) {
	struct siset_state *gs = appstate->siset;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (gs->choosing_options) {
			players_options_mouse_down(appstate, &gs->options,
						   event);
		} else {
			play_mouse_down(appstate, event);
		}
		break;
	case SDL_EVENT_MOUSE_MOTION:
		play_mouse_motion(appstate, event);
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		button_event(gs->options.start_button, event);
		button_event(gs->pass_button, event);
		button_event(gs->new_game_button, event);
		play_mouse_up(appstate, event);
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		siset_resize(appstate);
		break;
	default:
		if (gs->choosing_options) {
			players_options_event(appstate, &gs->options, event);
		}
		break;
	}
	players_options_update_start(&gs->options);
}

static void draw_column(struct app_state *as, unsigned i) {
	const struct column_data *column = &as->siset->columns[i];
	const SDL_FRect six_rect = column_slot_rect(as, i, SIX_SLOT);
	card_texture_draw(as->card_texture, (card_t)(column->suit | SIX),
			  &six_rect);
	if (column->high > SIX) {
		const SDL_FRect rect = column_slot_rect(as, i, HIGH_SLOT);
		card_texture_draw(as->card_texture,
				  (card_t)(column->suit | column->high), &rect);
	}
	if (column->low < SIX) {
		const SDL_FRect rect = column_slot_rect(as, i, LOW_SLOT);
		card_texture_draw(as->card_texture,
				  (card_t)(column->suit | column->low), &rect);
	}
}

static const SDL_Color target_color = {255, 255, 255, 90};

static void draw_targets(struct app_state *as) {
	const struct siset_state *gs = as->siset;
	if (gs->selected == NO_CARD_INDEX || !user_can_act(gs)) {
		return;
	}
	const card_t card = gs->player[USER].cards[gs->selected];
	SDL_SetRenderDrawColor(as->renderer, target_color.r, target_color.g,
			       target_color.b, target_color.a);
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		if (card_fits_column(gs, card, i)) {
			const SDL_FRect rect =
			    column_slot_rect(as, i, card_slot(card));
			SDL_RenderFillRect(as->renderer, &rect);
		}
	}
}

static void draw_play_flying(struct app_state *as) {
	const struct siset_state *gs = as->siset;
	if (!card_motion_is_moving(&gs->play_motion)) {
		return;
	}
	const Uint64 now = as->current_frame_ticks;
	const SDL_FRect end = column_slot_rect(as, gs->playing_column,
					       card_slot(gs->playing_card));
	const SDL_FPoint position = card_motion_position(
	    &gs->play_motion, (SDL_FPoint){end.x, end.y}, now);
	const struct card_placement placement = {
	    .rect = {.x = position.x, .y = position.y, .w = end.w, .h = end.h},
	    .angle = card_motion_angle(&gs->play_motion, 0.0, now),
	};
	draw_card(as, gs->playing_card, &placement);
}

static bool shows_face(const struct siset_state *gs, enum player_id id) {
	return id == USER || gs->game_over;
}

static void draw_dragged_hand(struct app_state *as, SDL_FPoint mouse) {
	const struct siset_state *gs = as->siset;
	const struct player_data *user = &gs->player[USER];
	card_t rest[SPANISH_DECK_SIZE];
	unsigned num_rest = 0;
	for (unsigned j = 0; j < user->num_cards; ++j) {
		if (j != gs->held.index) {
			rest[num_rest++] = user->cards[j];
		}
	}
	draw_hand(as, gs->card_size, USER, rest, num_rest, num_rest, true,
		  NO_CARD_INDEX, mouse);
	const SDL_FPoint position = held_card_position(&gs->held);
	const struct card_placement placement = {
	    .rect =
		{
		    .x = position.x,
		    .y = position.y,
		    .w = gs->card_size.x,
		    .h = gs->card_size.y,
		},
	    .angle = 0.0,
	};
	draw_card(as, user->cards[gs->held.index], &placement);
}

static void draw_player_hand(struct app_state *as, enum player_id id,
			     SDL_FPoint mouse) {
	const struct siset_state *gs = as->siset;
	const struct player_data *player = &gs->player[id];
	if (id == USER && gs->held.index != NO_CARD_INDEX &&
	    gs->held.dragging) {
		draw_dragged_hand(as, mouse);
		return;
	}
	draw_hand(as, gs->card_size, id, player->cards,
		  player->num_cards - player->num_receiving, player->num_cards,
		  shows_face(gs, id), id == USER ? gs->selected : NO_CARD_INDEX,
		  mouse);
}

static void draw_flying(struct app_state *as) {
	const struct siset_state *gs = as->siset;
	const enum player_id id = gs->flying.player;
	const struct player_data *player = &gs->player[id];
	draw_flying_card(as, gs->card_size, &gs->flying, player->cards,
			 player->num_cards, shows_face(gs, id));
}

static void draw_actions(struct app_state *as) {
	struct siset_state *gs = as->siset;
	const bool dragging =
	    gs->held.index != NO_CARD_INDEX && gs->held.dragging;
	if (gs->game_over) {
		button_draw_choice(as, gs->new_game_button);
	} else if (user_must_pass(gs)) {
		button_draw_choice(as, gs->pass_button);
	} else if (user_can_act(gs) && !dragging) {
		label_draw(as, gs->turn_hint_label, &table_label_color);
	}
}

void siset_iterate(struct app_state *appstate) {
	struct siset_state *gs = appstate->siset;
	if (gs->choosing_options) {
		players_options_draw(appstate, &gs->options);
		return;
	}
	siset_update(appstate);
	SDL_FPoint mouse;
	SDL_GetMouseState(&mouse.x, &mouse.y);
	for (unsigned i = 0; i < NUM_COLUMNS; ++i) {
		draw_column(appstate, i);
	}
	if (gs->deck_size > 0) {
		const SDL_FRect rect = deck_rect(appstate);
		card_texture_draw(appstate->card_texture, CARD_BACK, &rect);
	}
	draw_targets(appstate);
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		draw_player_hand(appstate, (enum player_id)i, mouse);
		draw_place(appstate, gs->place_labels, gs->card_size,
			   (enum player_id)i, gs->player[i].place);
	}
	if (!gs->game_over && !is_dealing(gs) && gs->current_player != USER) {
		draw_turn_arrow(appstate, gs->card_size, gs->current_player);
	}
	draw_flying(appstate);
	draw_play_flying(appstate);
	draw_actions(appstate);
}

void siset_leave(struct app_state *appstate) {
	input_unfocus(appstate, appstate->siset->options.input);
}

void siset_quit(struct app_state *appstate) {
	quit_gui(appstate, appstate->siset);
	SDL_free(appstate->siset);
	appstate->siset = NULL;
}
