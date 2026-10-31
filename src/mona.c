#include "mona.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>

#include "app.h"
#include "card_motion.h"
#include "card_texture.h"
#include "cards.h"
#include "gui.h"
#include "table.h"

struct player_data {
	card_t cards[SPANISH_DECK_SIZE];
	unsigned num_cards;
	unsigned num_receiving;
	unsigned place;
};

enum turn_phase { BEFORE_TAKING, AFTER_TAKING };

#define CARD_DEAL_MS 120U
#define CARD_TAKE_MS 400U
#define CARD_DISCARD_MS 300U
#define BOT_STEP_MS 700U
struct mona_state {
	card_t deck[SPANISH_DECK_SIZE];
	unsigned deck_size;
	card_t mona;
	card_t discard[SPANISH_DECK_SIZE];
	unsigned discard_size;
	struct player_data player[NUM_PLAYERS];
	enum player_id dealing_to;
	enum player_id current_player;
	enum turn_phase phase;
	unsigned selected;
	unsigned num_out;
	bool game_over;
	Uint64 bot_ready_ticks;
	struct flying_data flying;
	struct card_motion discard_motion;
	bool showing_discard;
	bool choosing_options;
	unsigned num_players;
	SDL_FPoint card_size;
	struct players_options options;
	struct label *take_hint_label;
	struct button *end_turn_button;
	struct button *new_game_button;
	struct label *place_labels[NUM_PLACES];
};

static void start_game(struct mona_state *gamestate) {
	gamestate->choosing_options = false;
	gamestate->showing_discard = false;
	const unsigned players = players_in_input(gamestate->options.input);
	if (players != 0) {
		gamestate->num_players = players;
	}
	fill_spanish_deck(gamestate->deck);
	shuffle(gamestate->deck, SPANISH_DECK_SIZE);
	gamestate->deck_size = SPANISH_DECK_SIZE - 1;
	gamestate->mona = gamestate->deck[gamestate->deck_size];
	gamestate->discard_size = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		gamestate->player[i].num_cards = 0;
		gamestate->player[i].num_receiving = 0;
		gamestate->player[i].place = 0;
	}
	card_motion_stop(&gamestate->flying.motion);
	card_motion_stop(&gamestate->discard_motion);
	const enum player_id first =
	    random_active_player(gamestate->num_players);
	gamestate->dealing_to = first;
	gamestate->current_player = first;
	gamestate->phase = BEFORE_TAKING;
	gamestate->selected = NO_CARD_INDEX;
	gamestate->num_out = 0;
	gamestate->game_over = false;
}

void mona_restart(struct mona_state *gamestate) { start_game(gamestate); }

void mona_choose_options(struct mona_state *gamestate) {
	gamestate->choosing_options = true;
	gamestate->showing_discard = false;
}

bool mona_is_choosing_options(const struct mona_state *gamestate) {
	return gamestate->choosing_options;
}

static void start_game_clicked(void *userdata) {
	start_game((struct mona_state *)userdata);
}

#define ACTION_GAP 0.3F
#define BUTTON_WIDTH 1.6F
#define HINT_WIDTH 3.F

static void position_actions(struct app_state *appstate) {
	struct mona_state *gs = appstate->mona;
	const float window_width = (float)appstate->window_size.x;
	const float bottom =
	    (float)appstate->window_size.y -
	    ((1.F + OUTER_MARGIN + ACTION_GAP) * gs->card_size.y);
	label_fit_width(gs->take_hint_label, HINT_WIDTH * gs->card_size.x);
	const SDL_FPoint hint_size = label_get_size(gs->take_hint_label);
	label_set_position(gs->take_hint_label,
			   (window_width - hint_size.x) / 2.F,
			   bottom - hint_size.y);
	struct button *buttons[] = {gs->end_turn_button, gs->new_game_button};
	button_group_fit_width(buttons, SDL_arraysize(buttons),
			       BUTTON_WIDTH * gs->card_size.x);
	for (unsigned i = 0; i < SDL_arraysize(buttons); ++i) {
		const SDL_FPoint size = button_get_size(buttons[i]);
		button_set_position(buttons[i], (window_width - size.x) / 2.F,
				    bottom - size.y);
	}
}

void mona_resize(struct app_state *appstate) {
	appstate->mona->card_size = table_card_size(appstate);
	players_options_position(appstate, &appstate->mona->options);
	position_actions(appstate);
	place_labels_fit(appstate->mona->place_labels,
			 appstate->mona->card_size);
}

static void quit_gui(struct app_state *appstate, struct mona_state *gs) {
	players_options_quit(appstate, &gs->options);
	if (gs->take_hint_label) {
		label_quit(gs->take_hint_label);
	}
	place_labels_quit(gs->place_labels);
	struct button *buttons[] = {gs->end_turn_button, gs->new_game_button};
	for (unsigned i = 0; i < SDL_arraysize(buttons); ++i) {
		if (buttons[i]) {
			button_quit(buttons[i]);
		}
	}
}

static void end_turn_clicked(void *userdata);

static bool init_gui(struct app_state *appstate, struct mona_state *gs) {
	struct gui_state *gui = appstate->gui;
	const bool options_ok =
	    players_options_init(gui, &gs->options, start_game_clicked, gs);
	gs->take_hint_label =
	    label_init(gui, "Agafa una carta d'un altre jugador");
	gs->end_turn_button =
	    button_init(gui, "Acaba el torn", end_turn_clicked, appstate);
	gs->new_game_button =
	    button_init(gui, "Nova partida", start_game_clicked, gs);
	const bool places_ok = place_labels_init(gui, gs->place_labels);
	return options_ok && places_ok && gs->take_hint_label &&
	       gs->end_turn_button && gs->new_game_button;
}

bool mona_init(struct app_state *appstate) {
	struct mona_state *gs = SDL_calloc(1, sizeof(*gs));
	if (!gs) {
		return false;
	}
	gs->num_players = NUM_PLAYERS;
	if (!init_gui(appstate, gs)) {
		quit_gui(appstate, gs);
		SDL_free(gs);
		return false;
	}
	appstate->mona = gs;

	mona_resize(appstate);
	mona_choose_options(gs);
	return true;
}

static SDL_FRect removed_card_rect(const struct app_state *as) {
	return pile_rect(as, as->mona->card_size, LEFT_PILE);
}

static SDL_FRect deck_rect(const struct app_state *as) {
	return pile_rect(as, as->mona->card_size, RIGHT_PILE);
}

static bool in_game(const struct mona_state *gs, enum player_id id) {
	return player_is_active(gs->num_players, id) &&
	       gs->player[id].place == 0;
}

static unsigned num_in_game(const struct mona_state *gs) {
	unsigned count = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		count += in_game(gs, (enum player_id)i) ? 1 : 0;
	}
	return count;
}

static enum player_id next_player(const struct mona_state *gs,
				  enum player_id id) {
	do {
		id = (enum player_id)((id + 1) % NUM_PLAYERS);
	} while (!in_game(gs, id));
	return id;
}

static void deal_card(struct app_state *as) {
	struct mona_state *gs = as->mona;
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

static bool user_can_act(const struct mona_state *gs) {
	return !gs->game_over && gs->deck_size == 0 &&
	       !card_motion_is_moving(&gs->flying.motion) &&
	       gs->current_player == USER;
}

static card_t remove_card(struct mona_state *gs, enum player_id id,
			  unsigned j) {
	struct player_data *player = &gs->player[id];
	const card_t card = player->cards[j];
	--player->num_cards;
	SDL_memmove(&player->cards[j], &player->cards[j + 1],
		    (player->num_cards - j) * sizeof(player->cards[0]));
	if (id == USER) {
		gs->selected = NO_CARD_INDEX;
	}
	return card;
}

static void leave_if_empty(struct mona_state *gs, enum player_id id) {
	struct player_data *player = &gs->player[id];
	if (player->num_cards > 0 || player->place != 0) {
		return;
	}
	player->place = ++gs->num_out;
	if (num_in_game(gs) <= 1) {
		gs->game_over = true;
	}
}

static void end_turn(struct mona_state *gs, Uint64 now) {
	if (gs->game_over) {
		return;
	}
	gs->current_player = next_player(gs, gs->current_player);
	gs->phase = BEFORE_TAKING;
	gs->selected = NO_CARD_INDEX;
	gs->bot_ready_ticks = now + BOT_STEP_MS;
}

static void discard_pair(struct app_state *as, enum player_id id, unsigned a,
			 unsigned b) {
	struct mona_state *gs = as->mona;
	const unsigned high = SDL_max(a, b);
	const struct card_placement from =
	    hand_card(as, gs->card_size, id, high, gs->player[id].num_cards);
	const card_t flying_card = remove_card(gs, id, high);
	gs->discard[gs->discard_size++] = remove_card(gs, id, SDL_min(a, b));
	gs->discard[gs->discard_size++] = flying_card;
	const SDL_FRect end = deck_rect(as);
	card_motion_start(&gs->discard_motion,
			  (SDL_FPoint){from.rect.x, from.rect.y}, from.angle,
			  (SDL_FPoint){end.x, end.y}, gs->card_size.x,
			  CARD_DISCARD_MS);
	leave_if_empty(gs, id);
	if (gs->player[id].place != 0) {
		end_turn(gs, as->current_frame_ticks);
	}
}

static bool find_pair(const struct player_data *player, unsigned *a,
		      unsigned *b) {
	for (unsigned i = 0; i < player->num_cards; ++i) {
		for (unsigned j = i + 1; j < player->num_cards; ++j) {
			if ((player->cards[i] & VAL_MASK) ==
			    (player->cards[j] & VAL_MASK)) {
				*a = i;
				*b = j;
				return true;
			}
		}
	}
	return false;
}

static void take_card(struct app_state *as, enum player_id victim, unsigned j) {
	struct mona_state *gs = as->mona;
	const enum player_id taker = gs->current_player;
	const struct card_placement from = hand_card(
	    as, gs->card_size, victim, j, gs->player[victim].num_cards);
	struct player_data *player = &gs->player[taker];
	player->cards[player->num_cards] = remove_card(gs, victim, j);
	++player->num_cards;
	player->num_receiving = 1;
	start_flight_to_hand(
	    as, gs->card_size, &gs->flying, taker, player->num_cards,
	    (SDL_FPoint){from.rect.x, from.rect.y}, from.angle, CARD_TAKE_MS);
	gs->phase = AFTER_TAKING;
	leave_if_empty(gs, victim);
}

static void bot_take(struct app_state *as) {
	const struct mona_state *gs = as->mona;
	enum player_id victims[NUM_PLAYERS];
	unsigned num_victims = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		const enum player_id id = (enum player_id)i;
		if (id != gs->current_player && in_game(gs, id)) {
			victims[num_victims++] = id;
		}
	}
	const enum player_id victim = victims[SDL_rand((Sint32)num_victims)];
	take_card(as, victim,
		  (unsigned)SDL_rand((Sint32)gs->player[victim].num_cards));
}

static void bot_step(struct app_state *as) {
	struct mona_state *gs = as->mona;
	const Uint64 now = as->current_frame_ticks;
	unsigned a;
	unsigned b;
	if (find_pair(&gs->player[gs->current_player], &a, &b)) {
		discard_pair(as, gs->current_player, a, b);
	} else if (gs->phase == BEFORE_TAKING) {
		bot_take(as);
	} else {
		end_turn(gs, now);
	}
}

static void mona_update(struct app_state *as) {
	struct mona_state *gs = as->mona;
	const Uint64 now = as->current_frame_ticks;
	card_motion_arrived(&gs->discard_motion, now);
	if (card_motion_is_moving(&gs->flying.motion)) {
		if (!card_motion_arrived(&gs->flying.motion, now)) {
			return;
		}
		gs->player[gs->flying.player].num_receiving = 0;
		gs->bot_ready_ticks = now + BOT_STEP_MS;
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
	gs->bot_ready_ticks = now + BOT_STEP_MS;
}

static void end_turn_clicked(void *userdata) {
	struct app_state *as = userdata;
	if (user_can_act(as->mona) && as->mona->phase == AFTER_TAKING) {
		end_turn(as->mona, as->current_frame_ticks);
	}
}

static void select_card(struct app_state *as, unsigned j) {
	struct mona_state *gs = as->mona;
	const struct player_data *user = &gs->player[USER];
	if (gs->selected == j) {
		gs->selected = NO_CARD_INDEX;
	} else if (gs->selected != NO_CARD_INDEX &&
		   (user->cards[gs->selected] & VAL_MASK) ==
		       (user->cards[j] & VAL_MASK)) {
		discard_pair(as, USER, gs->selected, j);
	} else {
		gs->selected = j;
	}
}

static bool try_take(struct app_state *as, SDL_FPoint point) {
	const struct mona_state *gs = as->mona;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		const enum player_id id = (enum player_id)i;
		if (id == USER || !in_game(gs, id)) {
			continue;
		}
		const unsigned j = hand_card_at(as, gs->card_size, id,
						gs->player[id].num_cards,
						NO_CARD_INDEX, point);
		if (j != NO_CARD_INDEX) {
			take_card(as, id, j);
			return true;
		}
	}
	return false;
}

static bool play_mouse_down(struct app_state *as, const SDL_Event *event) {
	struct mona_state *gs = as->mona;
	if (gs->game_over) {
		return button_event(gs->new_game_button, event);
	}
	if (!user_can_act(gs) || event->button.button != SDL_BUTTON_LEFT) {
		return false;
	}
	if (gs->phase == AFTER_TAKING &&
	    button_event(gs->end_turn_button, event)) {
		return true;
	}
	const SDL_FPoint point = {event->button.x, event->button.y};
	const unsigned j =
	    hand_card_at(as, gs->card_size, USER, gs->player[USER].num_cards,
			 gs->selected, point);
	if (j != NO_CARD_INDEX) {
		select_card(as, j);
		return true;
	}
	return gs->phase == BEFORE_TAKING && try_take(as, point);
}

static bool is_discard_click(const struct app_state *as,
			     const SDL_Event *event) {
	const SDL_FRect rect = deck_rect(as);
	return as->mona->deck_size == 0 && as->mona->discard_size > 0 &&
	       is_left_click_in(event, &rect);
}

void mona_event(struct app_state *appstate, SDL_Event *event) {
	struct mona_state *gs = appstate->mona;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (gs->choosing_options) {
			players_options_mouse_down(appstate, &gs->options,
						   event);
		} else if (gs->showing_discard) {
			gs->showing_discard = false;
		} else if (!play_mouse_down(appstate, event) &&
			   is_discard_click(appstate, event)) {
			gs->showing_discard = true;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		button_event(gs->options.start_button, event);
		button_event(gs->end_turn_button, event);
		button_event(gs->new_game_button, event);
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		mona_resize(appstate);
		break;
	default:
		if (gs->choosing_options) {
			players_options_event(appstate, &gs->options, event);
		}
		break;
	}
	players_options_update_start(&gs->options);
}

static void draw_piles(struct app_state *as) {
	const struct mona_state *gs = as->mona;
	const SDL_FRect removed_rect = removed_card_rect(as);
	card_texture_draw(as->card_texture,
			  gs->game_over ? gs->mona : CARD_BACK, &removed_rect);
	const SDL_FRect rect = deck_rect(as);
	if (gs->deck_size > 0) {
		card_texture_draw(as->card_texture, CARD_BACK, &rect);
		return;
	}
	const unsigned landed =
	    gs->discard_size -
	    (card_motion_is_moving(&gs->discard_motion) ? 2 : 0);
	card_texture_draw(as->card_texture,
			  landed > 0 ? gs->discard[landed - 1] : CARD_OUTLINE,
			  &rect);
}

static void draw_discard_flying(struct app_state *as) {
	const struct mona_state *gs = as->mona;
	if (!card_motion_is_moving(&gs->discard_motion)) {
		return;
	}
	const Uint64 now = as->current_frame_ticks;
	const SDL_FRect end = deck_rect(as);
	const SDL_FPoint position = card_motion_position(
	    &gs->discard_motion, (SDL_FPoint){end.x, end.y}, now);
	const struct card_placement placement = {
	    .rect = {.x = position.x, .y = position.y, .w = end.w, .h = end.h},
	    .angle = card_motion_angle(&gs->discard_motion, 0.0, now),
	};
	draw_card(as, gs->discard[gs->discard_size - 1], &placement);
}

static bool shows_face(const struct mona_state *gs, enum player_id id) {
	return id == USER || gs->game_over;
}

static void draw_player_hand(struct app_state *as, enum player_id id,
			     SDL_FPoint mouse) {
	const struct mona_state *gs = as->mona;
	const struct player_data *player = &gs->player[id];
	draw_hand(as, gs->card_size, id, player->cards,
		  player->num_cards - player->num_receiving, player->num_cards,
		  shows_face(gs, id), id == USER ? gs->selected : NO_CARD_INDEX,
		  mouse);
}

static void draw_flying(struct app_state *as) {
	const struct mona_state *gs = as->mona;
	const enum player_id id = gs->flying.player;
	const struct player_data *player = &gs->player[id];
	draw_flying_card(as, gs->card_size, &gs->flying, player->cards,
			 player->num_cards, shows_face(gs, id));
}

static void draw_actions(struct app_state *as) {
	struct mona_state *gs = as->mona;
	if (gs->game_over) {
		button_draw_choice(as, gs->new_game_button);
	} else if (!user_can_act(gs)) {
		return;
	} else if (gs->phase == BEFORE_TAKING) {
		label_draw(as, gs->take_hint_label, &table_label_color);
	} else {
		button_draw_choice(as, gs->end_turn_button);
	}
}

void mona_iterate(struct app_state *appstate) {
	if (appstate->mona->choosing_options) {
		players_options_draw(appstate, &appstate->mona->options);
		return;
	}
	mona_update(appstate);
	SDL_FPoint mouse;
	SDL_GetMouseState(&mouse.x, &mouse.y);
	draw_piles(appstate);
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		draw_player_hand(appstate, (enum player_id)i, mouse);
		draw_place(appstate, appstate->mona->place_labels,
			   appstate->mona->card_size, (enum player_id)i,
			   appstate->mona->player[i].place);
	}
	const struct mona_state *gs = appstate->mona;
	if (!gs->game_over && gs->deck_size == 0 &&
	    gs->current_player != USER) {
		draw_turn_arrow(appstate, gs->card_size, gs->current_player);
	}
	draw_flying(appstate);
	draw_discard_flying(appstate);
	draw_actions(appstate);
	if (gs->showing_discard) {
		draw_discard_spread(appstate, gs->card_size, gs->discard,
				    gs->discard_size, mouse);
	}
}

void mona_leave(struct app_state *appstate) {
	input_unfocus(appstate, appstate->mona->options.input);
}

void mona_quit(struct app_state *appstate) {
	quit_gui(appstate, appstate->mona);
	SDL_free(appstate->mona);
	appstate->mona = NULL;
}
