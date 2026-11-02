#include "tuti.h"

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

#define HAND_SIZE 3U
#define SEVEN 7U
#define TWO 2U
#define HORSE 11U
#define KING 12U

#define PAIR_POINTS 20U
#define TRUMP_PAIR_POINTS 40U
#define LAST_TRICK_POINTS 10U

#define CARD_DEAL_MS 150U
#define CARD_PLAY_MS 400U
#define CARD_COLLECT_MS 500U
#define BOT_STEP_MS 700U
#define TRICK_SHOW_MS 1000U

#define TRUMP_ANGLE 90.0
#define TRICK_SIDE_OFFSET 1.1F
#define TRICK_VERTICAL_OFFSET 0.3F
#define WON_PILE_GAP 0.2F
#define ACTION_GAP 0.3F
#define BUTTON_WIDTH 1.6F
#define TURN_HINT_WIDTH 2.F
#define RESULT_WIDTH 2.F
#define SCORE_WIDTH 1.F

enum phase { DEALING, PLAYING, SHOWING_TRICK, COLLECTING, GAME_OVER };

struct player_data {
	card_t cards[HAND_SIZE];
	unsigned num_cards;
	unsigned num_receiving;
	card_t won[SPANISH_DECK_SIZE];
	unsigned num_won;
};

struct trick_data {
	card_t cards[NUM_PLAYERS];
	enum player_id players[NUM_PLAYERS];
	unsigned num_cards;
};

struct tuti_state {
	card_t deck[SPANISH_DECK_SIZE];
	unsigned deck_size;
	unsigned trump_suit;
	struct player_data player[NUM_PLAYERS];
	struct trick_data trick;
	enum phase phase;
	enum player_id leader;
	enum player_id current_player;
	enum player_id trick_winner;
	unsigned num_to_deal;
	unsigned selected;
	bool user_won;
	bool showing_won;
	Uint64 ready_ticks;
	struct flying_data flying;
	struct card_motion play_motion;
	struct card_motion collect_motions[NUM_PLAYERS];
	bool choosing_options;
	unsigned num_players;
	SDL_FPoint card_size;
	struct players_options options;
	struct label *turn_hint_label;
	struct button *exchange_button;
	struct button *new_game_button;
	struct label *won_label;
	struct label *lost_label;
	struct label *score_labels[NUM_PLAYERS];
};

static const unsigned value_ranks[NUM_SUIT_CARDS + 1] = {
    0, 11, 0, 10, 1, 2, 3, 4, 5, 6, 7, 8, 9,
};

static const unsigned value_points[NUM_SUIT_CARDS + 1] = {
    0, 11, 0, 10, 0, 0, 0, 0, 0, 0, 2, 3, 4,
};

static unsigned card_rank(card_t card) { return value_ranks[card & VAL_MASK]; }

static unsigned card_points(card_t card) {
	return value_points[card & VAL_MASK];
}

static bool is_trump(const struct tuti_state *gs, card_t card) {
	return (card & SUIT_MASK) == gs->trump_suit;
}

static void quit_score_labels(struct tuti_state *gs) {
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		if (gs->score_labels[i]) {
			label_quit(gs->score_labels[i]);
			gs->score_labels[i] = NULL;
		}
	}
}

static void start_game(struct tuti_state *gamestate) {
	gamestate->choosing_options = false;
	const unsigned players = players_in_input(gamestate->options.input);
	if (players != 0) {
		gamestate->num_players = players;
	}
	fill_spanish_deck(gamestate->deck);
	shuffle(gamestate->deck, SPANISH_DECK_SIZE);
	gamestate->deck_size = SPANISH_DECK_SIZE;
	gamestate->trump_suit = gamestate->deck[0] & SUIT_MASK;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		gamestate->player[i].num_cards = 0;
		gamestate->player[i].num_receiving = 0;
		gamestate->player[i].num_won = 0;
	}
	gamestate->trick.num_cards = 0;
	card_motion_stop(&gamestate->flying.motion);
	card_motion_stop(&gamestate->play_motion);
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		card_motion_stop(&gamestate->collect_motions[i]);
	}
	quit_score_labels(gamestate);
	const enum player_id first =
	    random_active_player(gamestate->num_players);
	gamestate->leader = first;
	gamestate->current_player = first;
	gamestate->num_to_deal = HAND_SIZE * gamestate->num_players;
	gamestate->phase = DEALING;
	gamestate->selected = NO_CARD_INDEX;
	gamestate->showing_won = false;
}

void tuti_restart(struct tuti_state *gamestate) { start_game(gamestate); }

void tuti_choose_options(struct tuti_state *gamestate) {
	gamestate->choosing_options = true;
	gamestate->showing_won = false;
}

bool tuti_is_choosing_options(const struct tuti_state *gamestate) {
	return gamestate->choosing_options;
}

static void start_game_clicked(void *userdata) {
	start_game((struct tuti_state *)userdata);
}

static void position_actions(struct app_state *appstate) {
	struct tuti_state *gs = appstate->tuti;
	const float window_width = (float)appstate->window_size.x;
	const float window_height = (float)appstate->window_size.y;
	const float bottom =
	    window_height -
	    ((1.F + OUTER_MARGIN + ACTION_GAP) * gs->card_size.y);
	label_fit_width(gs->turn_hint_label, TURN_HINT_WIDTH * gs->card_size.x);
	const SDL_FPoint hint_size = label_get_size(gs->turn_hint_label);
	label_set_position(gs->turn_hint_label,
			   (window_width - hint_size.x) / 2.F,
			   bottom - hint_size.y);
	struct button *buttons[] = {gs->exchange_button, gs->new_game_button};
	button_group_fit_width(buttons, SDL_arraysize(buttons),
			       BUTTON_WIDTH * gs->card_size.x);
	for (unsigned i = 0; i < SDL_arraysize(buttons); ++i) {
		const SDL_FPoint size = button_get_size(buttons[i]);
		button_set_position(buttons[i], (window_width - size.x) / 2.F,
				    bottom - size.y);
	}
	struct label *results[] = {gs->won_label, gs->lost_label};
	label_group_fit_width(results, SDL_arraysize(results),
			      RESULT_WIDTH * gs->card_size.x);
	for (unsigned i = 0; i < SDL_arraysize(results); ++i) {
		const SDL_FPoint size = label_get_size(results[i]);
		label_set_position(results[i], (window_width - size.x) / 2.F,
				   (window_height - size.y) / 2.F);
	}
}

static void fit_score_labels(struct tuti_state *gs) {
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		if (gs->score_labels[i]) {
			label_fit_width(gs->score_labels[i],
					SCORE_WIDTH * gs->card_size.x);
		}
	}
}

void tuti_resize(struct app_state *appstate) {
	struct tuti_state *gs = appstate->tuti;
	gs->card_size = table_card_size(appstate);
	players_options_position(appstate, &gs->options);
	position_actions(appstate);
	fit_score_labels(gs);
}

static void quit_gui(struct app_state *appstate, struct tuti_state *gs) {
	players_options_quit(appstate, &gs->options);
	quit_score_labels(gs);
	struct label *labels[] = {
	    gs->turn_hint_label,
	    gs->won_label,
	    gs->lost_label,
	};
	for (unsigned i = 0; i < SDL_arraysize(labels); ++i) {
		if (labels[i]) {
			label_quit(labels[i]);
		}
	}
	struct button *buttons[] = {gs->exchange_button, gs->new_game_button};
	for (unsigned i = 0; i < SDL_arraysize(buttons); ++i) {
		if (buttons[i]) {
			button_quit(buttons[i]);
		}
	}
}

static void exchange_clicked(void *userdata);

static bool init_gui(struct app_state *appstate, struct tuti_state *gs) {
	struct gui_state *gui = appstate->gui;
	const bool options_ok =
	    players_options_init(gui, &gs->options, start_game_clicked, gs);
	gs->turn_hint_label = label_init(gui, "És el teu torn");
	gs->exchange_button =
	    button_init(gui, "Canvia el trumfo", exchange_clicked, appstate);
	gs->new_game_button =
	    button_init(gui, "Nova partida", start_game_clicked, gs);
	gs->won_label = label_init(gui, "Has guanyat!");
	gs->lost_label = label_init(gui, "Has perdut");
	return options_ok && gs->turn_hint_label && gs->exchange_button &&
	       gs->new_game_button && gs->won_label && gs->lost_label;
}

bool tuti_init(struct app_state *appstate) {
	struct tuti_state *gs = SDL_calloc(1, sizeof(*gs));
	if (!gs) {
		return false;
	}
	gs->num_players = NUM_PLAYERS;
	if (!init_gui(appstate, gs)) {
		quit_gui(appstate, gs);
		SDL_free(gs);
		return false;
	}
	appstate->tuti = gs;

	tuti_resize(appstate);
	tuti_choose_options(gs);
	return true;
}

static SDL_FRect deck_rect(const struct app_state *as) {
	const SDL_FPoint card_size = as->tuti->card_size;
	const float margin = OUTER_MARGIN * card_size.y;
	return (SDL_FRect){
	    .x = margin,
	    .y = margin,
	    .w = card_size.x,
	    .h = card_size.y,
	};
}

static struct card_placement trump_placement(const struct app_state *as) {
	const SDL_FPoint card_size = as->tuti->card_size;
	const SDL_FRect deck = deck_rect(as);
	return (struct card_placement){
	    .rect =
		{
		    .x = deck.x + (card_size.x / 2.F),
		    .y = deck.y,
		    .w = card_size.x,
		    .h = card_size.y,
		},
	    .angle = TRUMP_ANGLE,
	};
}

static SDL_FRect trick_slot_rect(const struct app_state *as,
				 enum player_id id) {
	const SDL_FPoint card_size = as->tuti->card_size;
	const SDL_FPoint inward = seat_inward(id);
	return (SDL_FRect){
	    .x = (((float)as->window_size.x - card_size.x) / 2.F) -
		 (inward.x * TRICK_SIDE_OFFSET * card_size.x),
	    .y = (((float)as->window_size.y - card_size.y) / 2.F) -
		 (inward.y * TRICK_VERTICAL_OFFSET * card_size.y),
	    .w = card_size.x,
	    .h = card_size.y,
	};
}

static struct card_placement won_pile_placement(const struct app_state *as,
						enum player_id id) {
	const SDL_FPoint card_size = as->tuti->card_size;
	struct card_placement placement =
	    hand_card(as, card_size, id, 0, HAND_SIZE);
	const SDL_FPoint inward = seat_inward(id);
	const float shift =
	    hand_length(card_size, HAND_SIZE) + (WON_PILE_GAP * card_size.x);
	placement.rect.x += SDL_fabsf(inward.y) * shift;
	placement.rect.y += SDL_fabsf(inward.x) * shift;
	return placement;
}

static enum player_id next_player(const struct tuti_state *gs,
				  enum player_id id) {
	return next_active_player(gs->num_players, id);
}

static card_t exchange_card(const struct tuti_state *gs) {
	if (gs->deck_size == 0) {
		return 0;
	}
	const card_t trump = gs->deck[0];
	const unsigned value =
	    card_rank(trump) > value_ranks[SEVEN] ? SEVEN : TWO;
	const card_t card = (card_t)(gs->trump_suit | value);
	return card == trump ? 0 : card;
}

static unsigned exchange_index(const struct tuti_state *gs, enum player_id id) {
	const card_t card = exchange_card(gs);
	const struct player_data *player = &gs->player[id];
	if (card == 0) {
		return NO_CARD_INDEX;
	}
	for (unsigned j = 0; j < player->num_cards; ++j) {
		if (player->cards[j] == card) {
			return j;
		}
	}
	return NO_CARD_INDEX;
}

static bool can_play(const struct tuti_state *gs, enum player_id id) {
	return gs->phase == PLAYING && gs->current_player == id &&
	       !card_motion_is_moving(&gs->play_motion);
}

static bool can_exchange(const struct tuti_state *gs, enum player_id id) {
	return can_play(gs, id) && exchange_index(gs, id) != NO_CARD_INDEX;
}

static void exchange_trump(struct tuti_state *gs, enum player_id id) {
	card_t *hand_card_slot = &gs->player[id].cards[exchange_index(gs, id)];
	const card_t trump = gs->deck[0];
	gs->deck[0] = *hand_card_slot;
	*hand_card_slot = trump;
}

static void deal_card(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
	struct player_data *player = &gs->player[gs->current_player];
	--gs->deck_size;
	player->cards[player->num_cards] = gs->deck[gs->deck_size];
	++player->num_cards;
	player->num_receiving = 1;
	struct card_placement from = {.rect = deck_rect(as), .angle = 0.0};
	if (gs->deck_size == 0) {
		from = trump_placement(as);
	}
	start_flight_to_hand(as, gs->card_size, &gs->flying, gs->current_player,
			     player->num_cards,
			     (SDL_FPoint){from.rect.x, from.rect.y}, from.angle,
			     CARD_DEAL_MS);
	--gs->num_to_deal;
	gs->current_player = next_player(gs, gs->current_player);
}

static void start_playing(struct tuti_state *gs, Uint64 now) {
	gs->phase = PLAYING;
	gs->current_player = gs->leader;
	gs->ready_ticks = now + BOT_STEP_MS;
}

static void play_card(struct app_state *as, enum player_id id, unsigned j) {
	struct tuti_state *gs = as->tuti;
	struct player_data *player = &gs->player[id];
	const struct card_placement from =
	    hand_card(as, gs->card_size, id, j, player->num_cards);
	struct trick_data *trick = &gs->trick;
	trick->cards[trick->num_cards] = player->cards[j];
	trick->players[trick->num_cards] = id;
	++trick->num_cards;
	--player->num_cards;
	SDL_memmove(&player->cards[j], &player->cards[j + 1],
		    (player->num_cards - j) * sizeof(player->cards[0]));
	gs->selected = NO_CARD_INDEX;
	const SDL_FRect end = trick_slot_rect(as, id);
	card_motion_start(&gs->play_motion,
			  (SDL_FPoint){from.rect.x, from.rect.y}, from.angle,
			  (SDL_FPoint){end.x, end.y}, gs->card_size.x,
			  CARD_PLAY_MS);
}

static void land_played_card(struct tuti_state *gs, Uint64 now) {
	const enum player_id id = next_player(gs, gs->current_player);
	if (id == gs->leader) {
		gs->phase = SHOWING_TRICK;
		gs->ready_ticks = now + TRICK_SHOW_MS;
		return;
	}
	gs->current_player = id;
	gs->ready_ticks = now + BOT_STEP_MS;
}

static bool beats(const struct tuti_state *gs, card_t card, card_t best) {
	if ((card & SUIT_MASK) == (best & SUIT_MASK)) {
		return card_rank(card) > card_rank(best);
	}
	return is_trump(gs, card);
}

static unsigned best_trick_index(const struct tuti_state *gs) {
	const struct trick_data *trick = &gs->trick;
	unsigned best = 0;
	for (unsigned i = 1; i < trick->num_cards; ++i) {
		if (beats(gs, trick->cards[i], trick->cards[best])) {
			best = i;
		}
	}
	return best;
}

static void start_collecting(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
	const struct trick_data *trick = &gs->trick;
	gs->trick_winner = trick->players[best_trick_index(gs)];
	const SDL_FRect pile = won_pile_placement(as, gs->trick_winner).rect;
	const SDL_FPoint to = {pile.x, pile.y};
	for (unsigned i = 0; i < trick->num_cards; ++i) {
		const SDL_FRect from = trick_slot_rect(as, trick->players[i]);
		card_motion_start(&gs->collect_motions[i],
				  (SDL_FPoint){from.x, from.y}, 0.0, to,
				  gs->card_size.x, CARD_COLLECT_MS);
	}
	gs->phase = COLLECTING;
}

static bool collect_arrived(struct tuti_state *gs, Uint64 now) {
	bool moving = false;
	for (unsigned i = 0; i < gs->trick.num_cards; ++i) {
		card_motion_arrived(&gs->collect_motions[i], now);
		moving =
		    moving || card_motion_is_moving(&gs->collect_motions[i]);
	}
	return !moving;
}

static bool won_all_suits_of(const struct player_data *player, unsigned value) {
	unsigned count = 0;
	for (unsigned i = 0; i < player->num_won; ++i) {
		count += (player->won[i] & VAL_MASK) == value ? 1 : 0;
	}
	return count == NUM_SUITS;
}

static bool has_won_card(const struct player_data *player, card_t card) {
	for (unsigned i = 0; i < player->num_won; ++i) {
		if (player->won[i] == card) {
			return true;
		}
	}
	return false;
}

static unsigned pile_score(const struct tuti_state *gs,
			   const struct player_data *player) {
	unsigned score = 0;
	for (unsigned i = 0; i < player->num_won; ++i) {
		score += card_points(player->won[i]);
	}
	for (unsigned i = 0; i < NUM_SUITS; ++i) {
		if (has_won_card(player, (card_t)(suits[i] | KING)) &&
		    has_won_card(player, (card_t)(suits[i] | HORSE))) {
			score += suits[i] == gs->trump_suit ? TRUMP_PAIR_POINTS
							    : PAIR_POINTS;
		}
	}
	return score;
}

static struct label *score_label(struct gui_state *gui, unsigned score) {
	char *text = NULL;
	SDL_asprintf(&text, "%u punts", score);
	if (!text) {
		return NULL;
	}
	struct label *label = label_init(gui, text);
	SDL_free(text);
	return label;
}

static void end_game_with_tuti(struct app_state *as, enum player_id winner) {
	struct tuti_state *gs = as->tuti;
	gs->score_labels[winner] = label_init(as->gui, "Tuti!");
	gs->user_won = winner == USER;
	gs->phase = GAME_OVER;
	fit_score_labels(gs);
}

static void end_game_with_scores(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
	unsigned scores[NUM_PLAYERS] = {0};
	unsigned best = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		const enum player_id id = (enum player_id)i;
		if (!player_is_active(gs->num_players, id)) {
			continue;
		}
		scores[i] = pile_score(gs, &gs->player[i]);
		if (id == gs->trick_winner) {
			scores[i] += LAST_TRICK_POINTS;
		}
		best = SDL_max(best, scores[i]);
		gs->score_labels[i] = score_label(as->gui, scores[i]);
	}
	gs->user_won = scores[USER] == best;
	gs->phase = GAME_OVER;
	fit_score_labels(gs);
}

static void finish_trick(struct app_state *as, Uint64 now) {
	struct tuti_state *gs = as->tuti;
	struct trick_data *trick = &gs->trick;
	struct player_data *winner = &gs->player[gs->trick_winner];
	SDL_memcpy(&winner->won[winner->num_won], trick->cards,
		   trick->num_cards * sizeof(trick->cards[0]));
	winner->num_won += trick->num_cards;
	trick->num_cards = 0;
	if (won_all_suits_of(winner, KING) || won_all_suits_of(winner, HORSE)) {
		end_game_with_tuti(as, gs->trick_winner);
		return;
	}
	if (winner->num_cards == 0) {
		end_game_with_scores(as);
		return;
	}
	gs->leader = gs->trick_winner;
	if (gs->deck_size == 0) {
		start_playing(gs, now);
		return;
	}
	gs->phase = DEALING;
	gs->current_player = gs->trick_winner;
	gs->num_to_deal = gs->num_players;
}

static unsigned card_cost(const struct tuti_state *gs, card_t card) {
	return card_rank(card) + (is_trump(gs, card) ? NUM_SUIT_CARDS : 0);
}

static unsigned trick_points(const struct tuti_state *gs) {
	unsigned points = 0;
	for (unsigned i = 0; i < gs->trick.num_cards; ++i) {
		points += card_points(gs->trick.cards[i]);
	}
	return points;
}

static unsigned bot_choice(const struct tuti_state *gs, enum player_id id) {
	const struct player_data *player = &gs->player[id];
	unsigned cheapest = 0;
	unsigned cheapest_winner = NO_CARD_INDEX;
	const bool following = gs->trick.num_cards > 0;
	const card_t best =
	    following ? gs->trick.cards[best_trick_index(gs)] : 0;
	for (unsigned j = 0; j < player->num_cards; ++j) {
		const unsigned cost = card_cost(gs, player->cards[j]);
		if (cost < card_cost(gs, player->cards[cheapest])) {
			cheapest = j;
		}
		if (following && beats(gs, player->cards[j], best) &&
		    (cheapest_winner == NO_CARD_INDEX ||
		     cost < card_cost(gs, player->cards[cheapest_winner]))) {
			cheapest_winner = j;
		}
	}
	if (cheapest_winner != NO_CARD_INDEX &&
	    (trick_points(gs) > 0 ||
	     !is_trump(gs, player->cards[cheapest_winner]))) {
		return cheapest_winner;
	}
	return cheapest;
}

static void bot_step(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
	const enum player_id id = gs->current_player;
	if (can_exchange(gs, id)) {
		exchange_trump(gs, id);
		gs->ready_ticks = as->current_frame_ticks + BOT_STEP_MS;
		return;
	}
	play_card(as, id, bot_choice(gs, id));
}

static void tuti_update(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
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
		if (gs->num_to_deal == 0) {
			start_playing(gs, now);
		}
	}
	switch (gs->phase) {
	case DEALING:
		deal_card(as);
		break;
	case PLAYING:
		if (gs->current_player != USER && now >= gs->ready_ticks) {
			bot_step(as);
		}
		break;
	case SHOWING_TRICK:
		if (now >= gs->ready_ticks) {
			start_collecting(as);
		}
		break;
	case COLLECTING:
		if (collect_arrived(gs, now)) {
			finish_trick(as, now);
		}
		break;
	case GAME_OVER:
		break;
	}
}

static void exchange_clicked(void *userdata) {
	struct app_state *as = userdata;
	if (can_exchange(as->tuti, USER)) {
		exchange_trump(as->tuti, USER);
	}
}

static bool play_mouse_down(struct app_state *as, const SDL_Event *event) {
	struct tuti_state *gs = as->tuti;
	if (gs->phase == GAME_OVER) {
		return button_event(gs->new_game_button, event);
	}
	if (!can_play(gs, USER) || event->button.button != SDL_BUTTON_LEFT) {
		return false;
	}
	if (can_exchange(gs, USER) &&
	    button_event(gs->exchange_button, event)) {
		return true;
	}
	const SDL_FPoint point = {event->button.x, event->button.y};
	const unsigned j =
	    hand_card_at(as, gs->card_size, USER, gs->player[USER].num_cards,
			 gs->selected, point);
	if (j != NO_CARD_INDEX && j == gs->selected) {
		play_card(as, USER, j);
	} else {
		gs->selected = j;
	}
	return j != NO_CARD_INDEX;
}

static bool is_won_pile_click(const struct app_state *as,
			      const SDL_Event *event) {
	const SDL_FRect rect = won_pile_placement(as, USER).rect;
	return as->tuti->player[USER].num_won > 0 &&
	       is_left_click_in(event, &rect);
}

void tuti_event(struct app_state *appstate, SDL_Event *event) {
	struct tuti_state *gs = appstate->tuti;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (gs->choosing_options) {
			players_options_mouse_down(appstate, &gs->options,
						   event);
		} else if (gs->showing_won) {
			gs->showing_won = false;
		} else if (!play_mouse_down(appstate, event) &&
			   is_won_pile_click(appstate, event)) {
			gs->showing_won = true;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		button_event(gs->options.start_button, event);
		button_event(gs->exchange_button, event);
		button_event(gs->new_game_button, event);
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		tuti_resize(appstate);
		break;
	default:
		if (gs->choosing_options) {
			players_options_event(appstate, &gs->options, event);
		}
		break;
	}
	players_options_update_start(&gs->options);
}

static void draw_deck(struct app_state *as) {
	const struct tuti_state *gs = as->tuti;
	if (gs->deck_size == 0) {
		return;
	}
	const struct card_placement trump = trump_placement(as);
	draw_card(as, gs->deck[0], &trump);
	if (gs->deck_size > 1) {
		const SDL_FRect rect = deck_rect(as);
		card_texture_draw(as->card_texture, CARD_BACK, &rect);
	}
}

static void draw_won_piles(struct app_state *as) {
	const struct tuti_state *gs = as->tuti;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		const enum player_id id = (enum player_id)i;
		if (!player_is_active(gs->num_players, id)) {
			continue;
		}
		const struct card_placement placement =
		    won_pile_placement(as, id);
		draw_card(as,
			  gs->player[i].num_won > 0 ? CARD_BACK : CARD_OUTLINE,
			  &placement);
	}
}

static void draw_trick(struct app_state *as) {
	const struct tuti_state *gs = as->tuti;
	const struct trick_data *trick = &gs->trick;
	const Uint64 now = as->current_frame_ticks;
	for (unsigned i = 0; i < trick->num_cards; ++i) {
		const SDL_FRect slot = trick_slot_rect(as, trick->players[i]);
		struct card_placement placement = {.rect = slot, .angle = 0.0};
		const struct card_motion *motion = NULL;
		struct card_placement to = placement;
		if (gs->phase == COLLECTING) {
			motion = &gs->collect_motions[i];
			to = won_pile_placement(as, gs->trick_winner);
		} else if (i == trick->num_cards - 1) {
			motion = &gs->play_motion;
		}
		if (motion && card_motion_is_moving(motion)) {
			const SDL_FPoint position = card_motion_position(
			    motion, (SDL_FPoint){to.rect.x, to.rect.y}, now);
			placement.rect.x = position.x;
			placement.rect.y = position.y;
			placement.angle =
			    card_motion_angle(motion, to.angle, now);
		} else if (gs->phase == COLLECTING) {
			continue;
		}
		draw_card(as, trick->cards[i], &placement);
	}
}

static bool shows_face(const struct tuti_state *gs, enum player_id id) {
	return id == USER || gs->phase == GAME_OVER;
}

static void draw_hands(struct app_state *as, SDL_FPoint mouse) {
	const struct tuti_state *gs = as->tuti;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		const enum player_id id = (enum player_id)i;
		const struct player_data *player = &gs->player[i];
		draw_hand(as, gs->card_size, id, player->cards,
			  player->num_cards - player->num_receiving,
			  player->num_cards, shows_face(gs, id),
			  id == USER ? gs->selected : NO_CARD_INDEX, mouse);
	}
	const struct player_data *receiver = &gs->player[gs->flying.player];
	draw_flying_card(as, gs->card_size, &gs->flying, receiver->cards,
			 receiver->num_cards,
			 shows_face(gs, gs->flying.player));
}

static void draw_scores(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		struct label *label = gs->score_labels[i];
		if (!label) {
			continue;
		}
		const SDL_FPoint center =
		    seat_center(as, gs->card_size, (enum player_id)i);
		const SDL_FPoint size = label_get_size(label);
		label_set_position(label, center.x - (size.x / 2.F),
				   center.y - (size.y / 2.F));
		label_draw(as, label, &table_label_color);
	}
}

static void draw_actions(struct app_state *as) {
	struct tuti_state *gs = as->tuti;
	if (gs->phase == GAME_OVER) {
		draw_scores(as);
		label_draw(as, gs->user_won ? gs->won_label : gs->lost_label,
			   &table_label_color);
		button_draw_choice(as, gs->new_game_button);
	} else if (can_exchange(gs, USER)) {
		button_draw_choice(as, gs->exchange_button);
	} else if (can_play(gs, USER)) {
		label_draw(as, gs->turn_hint_label, &table_label_color);
	}
}

void tuti_iterate(struct app_state *appstate) {
	struct tuti_state *gs = appstate->tuti;
	if (gs->choosing_options) {
		players_options_draw(appstate, &gs->options);
		return;
	}
	tuti_update(appstate);
	SDL_FPoint mouse;
	SDL_GetMouseState(&mouse.x, &mouse.y);
	draw_deck(appstate);
	draw_won_piles(appstate);
	draw_hands(appstate, mouse);
	if (gs->phase == PLAYING && gs->current_player != USER) {
		draw_turn_arrow(appstate, gs->card_size, gs->current_player);
	}
	draw_trick(appstate);
	draw_actions(appstate);
	if (gs->showing_won) {
		const struct player_data *user = &gs->player[USER];
		draw_discard_spread(appstate, gs->card_size, user->won,
				    user->num_won, mouse);
	}
}

void tuti_leave(struct app_state *appstate) {
	input_unfocus(appstate, appstate->tuti->options.input);
}

void tuti_quit(struct app_state *appstate) {
	quit_gui(appstate, appstate->tuti);
	SDL_free(appstate->tuti);
	appstate->tuti = NULL;
}
