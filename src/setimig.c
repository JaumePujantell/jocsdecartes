#include "setimig.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>
#include <stddef.h>

#include "app.h"
#include "card_motion.h"
#include "card_texture.h"
#include "cards.h"
#include "gui.h"
#include "table.h"

struct deck_data {
	card_t cards[SMALL_SPANISH_DECK_SIZE];
	unsigned num_cards;
};

struct discard_data {
	card_t cards[SMALL_SPANISH_DECK_SIZE];
	unsigned num_cards;
};

struct player_data {
	card_t cards[SMALL_SPANISH_DECK_SIZE];
	unsigned num_cards;
	unsigned num_receiving;
	bool passed;
	unsigned wins;
};

#define CARD_FLIGHT_MS 300U

#define DEFAULT_WINS_TEXT "5"

struct setimig_state {
	struct deck_data deck;
	struct discard_data discard;
	struct player_data player[NUM_PLAYERS];
	struct flying_data flying;
	enum player_id current_player;
	bool revealed;
	bool showing_discard;
	bool choosing_options;
	bool wins_limited;
	unsigned num_players;
	SDL_FPoint card_size;
	struct button *draw_button;
	struct button *pass_button;
	struct button *next_round_button;
	struct label *players_caption;
	struct input *players_input;
	struct label *wins_caption;
	struct input *wins_input;
	struct button *no_limit_button;
	struct button *start_button;
	struct label *won_label;
	struct label *lost_label;
	struct button *new_game_button;
};

static unsigned wins_needed(const struct setimig_state *gs) {
	if (!gs->wins_limited) {
		return 0;
	}
	const unsigned long wins =
	    SDL_strtoul(input_get_text(gs->wins_input), NULL, 10);
	return (unsigned)SDL_min(wins, SDL_MAX_UINT32);
}

static void start_round(struct setimig_state *gamestate) {
	struct discard_data *discard = &gamestate->discard;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		struct player_data *player = &gamestate->player[i];
		SDL_memcpy(&discard->cards[discard->num_cards], player->cards,
			   player->num_cards * sizeof(player->cards[0]));
		discard->num_cards += player->num_cards;
		player->num_cards = 0;
		player->num_receiving = 0;
		player->passed = !player_is_active(gamestate->num_players,
						   (enum player_id)i);
	}
	card_motion_stop(&gamestate->flying.motion);
	gamestate->current_player = USER;
	gamestate->revealed = false;
}

static void start_game(struct setimig_state *gamestate) {
	gamestate->choosing_options = false;
	gamestate->showing_discard = false;
	const unsigned players = players_in_input(gamestate->players_input);
	if (players != 0) {
		gamestate->num_players = players;
	}
	if (wins_needed(gamestate) == 0) {
		gamestate->wins_limited = false;
	}
	fill_small_spanish_deck(gamestate->deck.cards);
	gamestate->deck.num_cards = SMALL_SPANISH_DECK_SIZE;
	shuffle(gamestate->deck.cards, SMALL_SPANISH_DECK_SIZE);
	gamestate->discard.num_cards = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		gamestate->player[i].num_cards = 0;
		gamestate->player[i].wins = 0;
	}
	start_round(gamestate);
}

void setimig_restart(struct setimig_state *gamestate) { start_game(gamestate); }

void setimig_choose_options(struct setimig_state *gamestate) {
	gamestate->choosing_options = true;
	gamestate->showing_discard = false;
}

bool setimig_is_choosing_options(const struct setimig_state *gamestate) {
	return gamestate->choosing_options;
}

static void next_round_clicked(void *userdata) {
	start_round((struct setimig_state *)userdata);
}

static void start_game_clicked(void *userdata) {
	start_game((struct setimig_state *)userdata);
}

static void no_limit_clicked(void *userdata) {
	((struct setimig_state *)userdata)->wins_limited = false;
}

#define BUTTON_WIDTH_FRACTION 0.8F
#define BUTTON_GAP 0.2F
#define STAR_SIZE 0.2F
#define STAR_GAP 0.1F

static void position_options(struct app_state *appstate) {
	struct setimig_state *gs = appstate->setimig;
	const float window_width = (float)appstate->window_size.x;
	const float window_height = (float)appstate->window_size.y;
	const float gap = option_gap(appstate);
	struct label *cells[] = {gs->players_caption, gs->wins_caption,
				 button_get_label(gs->no_limit_button)};
	label_group_fit_width(cells, SDL_arraysize(cells),
			      option_column_width(appstate));
	const SDL_FPoint choice_size = button_get_size(gs->no_limit_button);
	const float font_size = button_get_font_size(gs->no_limit_button);
	struct input *inputs[] = {gs->players_input, gs->wins_input};
	for (unsigned i = 0; i < SDL_arraysize(inputs); ++i) {
		input_set_font_size(appstate, inputs[i], font_size);
		input_set_size(appstate, inputs[i], choice_size.x,
			       choice_size.y);
	}
	button_fit_width(gs->start_button, choice_size.x);
	const SDL_FPoint start_size = button_get_size(gs->start_button);

	const float total_height =
	    (2.F * choice_size.y) + gap + (2.F * gap) + start_size.y;
	float y = (window_height - total_height) / 2.F;
	const float caption_x =
	    (window_width - ((float)NUM_OPTION_COLUMNS * choice_size.x) -
	     ((float)(NUM_OPTION_COLUMNS - 1) * gap)) /
	    2.F;
	const float input_x = caption_x + choice_size.x + gap;
	label_set_position(gs->players_caption, caption_x, y);
	input_set_position(appstate, gs->players_input, input_x, y);
	y += choice_size.y + gap;
	label_set_position(gs->wins_caption, caption_x, y);
	input_set_position(appstate, gs->wins_input, input_x, y);
	button_set_position(gs->no_limit_button, input_x + choice_size.x + gap,
			    y);
	y += choice_size.y + (2.F * gap);
	button_set_position(gs->start_button,
			    (window_width - start_size.x) / 2.F, y);
}

static void position_game_over(struct app_state *appstate, float width,
			       float gap) {
	struct setimig_state *gs = appstate->setimig;
	const float window_width = (float)appstate->window_size.x;
	const float middle = (float)appstate->window_size.y / 2.F;
	struct label *labels[] = {gs->won_label, gs->lost_label};
	label_group_fit_width(labels, SDL_arraysize(labels), width);
	button_fit_width(gs->new_game_button, width);
	const SDL_FPoint label_size = label_get_size(gs->won_label);
	const SDL_FPoint new_game_size = button_get_size(gs->new_game_button);
	for (unsigned i = 0; i < SDL_arraysize(labels); ++i) {
		label_set_position(labels[i],
				   (window_width - label_size.x) / 2.F,
				   middle - label_size.y - (gap / 2.F));
	}
	button_set_position(gs->new_game_button,
			    (window_width - new_game_size.x) / 2.F,
			    middle + (gap / 2.F));
}

void setimig_resize(struct app_state *appstate) {
	struct setimig_state *gs = appstate->setimig;
	gs->card_size = table_card_size(appstate);

	const float button_width = BUTTON_WIDTH_FRACTION * gs->card_size.x;
	const float gap = BUTTON_GAP * gs->card_size.x;
	button_fit_width(gs->draw_button, button_width);
	button_fit_width(gs->pass_button, button_width);
	const SDL_FPoint draw_size = button_get_size(gs->draw_button);
	const SDL_FPoint pass_size = button_get_size(gs->pass_button);
	const float left =
	    ((float)appstate->window_size.x - draw_size.x - gap - pass_size.x) /
	    2.F;
	const float bottom = (float)appstate->window_size.y - gs->card_size.y -
			     (OUTER_MARGIN * gs->card_size.y) - gap;
	button_set_position(gs->draw_button, left, bottom - draw_size.y);
	button_set_position(gs->pass_button, left + draw_size.x + gap,
			    bottom - pass_size.y);

	button_fit_width(gs->next_round_button, 2.F * button_width);
	const SDL_FPoint next_size = button_get_size(gs->next_round_button);
	button_set_position(
	    gs->next_round_button,
	    ((float)appstate->window_size.x - next_size.x) / 2.F,
	    ((float)appstate->window_size.y - next_size.y) / 2.F);

	position_game_over(appstate, 2.F * button_width, gap);
	position_options(appstate);
}

#define NUM_BUTTONS 6U

static void all_buttons(struct setimig_state *gs,
			struct button *buttons[NUM_BUTTONS]) {
	struct button *const list[NUM_BUTTONS] = {
	    gs->draw_button,  gs->pass_button,	   gs->next_round_button,
	    gs->start_button, gs->new_game_button, gs->no_limit_button};
	for (unsigned i = 0; i < NUM_BUTTONS; ++i) {
		buttons[i] = list[i];
	}
}

static void quit_gui(struct app_state *appstate, struct setimig_state *gs) {
	struct button *buttons[NUM_BUTTONS];
	all_buttons(gs, buttons);
	for (unsigned i = 0; i < NUM_BUTTONS; ++i) {
		if (buttons[i]) {
			button_quit(buttons[i]);
		}
	}
	struct label *labels[] = {gs->won_label, gs->lost_label,
				  gs->players_caption, gs->wins_caption};
	for (unsigned i = 0; i < SDL_arraysize(labels); ++i) {
		if (labels[i]) {
			label_quit(labels[i]);
		}
	}
	struct input *inputs[] = {gs->players_input, gs->wins_input};
	for (unsigned i = 0; i < SDL_arraysize(inputs); ++i) {
		if (inputs[i]) {
			input_quit(appstate, inputs[i]);
		}
	}
}

static void draw_clicked(void *userdata);
static void pass_clicked(void *userdata);

static bool init_option_buttons(struct gui_state *gui,
				struct setimig_state *gs) {
	gs->players_caption = label_init(gui, "Jugadors");
	gs->wins_caption = label_init(gui, "Victòries");
	gs->players_input = input_init(gui, DEFAULT_PLAYERS_TEXT);
	gs->wins_input = input_init(gui, DEFAULT_WINS_TEXT);
	gs->no_limit_button =
	    button_init(gui, "Sense límit", no_limit_clicked, gs);
	gs->start_button = button_init(gui, "Comença", start_game_clicked, gs);
	return gs->players_caption && gs->wins_caption && gs->players_input &&
	       gs->wins_input && gs->no_limit_button && gs->start_button;
}

static bool init_buttons_and_labels(struct app_state *appstate,
				    struct setimig_state *gs) {
	struct gui_state *gui = appstate->gui;
	gs->draw_button = button_init(gui, "Roba", draw_clicked, appstate);
	gs->pass_button = button_init(gui, "Passa", pass_clicked, appstate);
	gs->next_round_button =
	    button_init(gui, "Següent ronda", next_round_clicked, gs);
	gs->won_label = label_init(gui, "Has guanyat!");
	gs->lost_label = label_init(gui, "Has perdut");
	gs->new_game_button =
	    button_init(gui, "Nova partida", start_game_clicked, gs);
	if (!gs->draw_button || !gs->pass_button || !gs->next_round_button ||
	    !gs->won_label || !gs->lost_label || !gs->new_game_button) {
		return false;
	}
	return init_option_buttons(gui, gs);
}

bool setimig_init(struct app_state *appstate) {
	struct setimig_state *gs = SDL_calloc(1, sizeof(*gs));
	if (!gs) {
		return false;
	}
	gs->num_players = NUM_PLAYERS;
	gs->wins_limited = false;
	if (!init_buttons_and_labels(appstate, gs)) {
		quit_gui(appstate, gs);
		SDL_free(gs);
		return false;
	}
	appstate->setimig = gs;

	setimig_resize(appstate);
	setimig_choose_options(gs);
	return true;
}

static SDL_FRect deck_rect(const struct app_state *as) {
	return pile_rect(as, as->setimig->card_size, LEFT_PILE);
}

static SDL_FRect discard_rect(const struct app_state *as) {
	return pile_rect(as, as->setimig->card_size, RIGHT_PILE);
}

#define MAX_HALF_POINTS 15U

static unsigned hand_half_points(const struct player_data *player) {
	unsigned half_points = 0;
	for (unsigned j = 0; j < player->num_cards; ++j) {
		const unsigned value = player->cards[j] & VAL_MASK;
		half_points += value > NUM_VALUES ? 1 : 2 * value;
	}
	return half_points;
}

static void score_round(struct setimig_state *gs) {
	unsigned scores[NUM_PLAYERS];
	unsigned best = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		scores[i] = hand_half_points(&gs->player[i]);
		if (scores[i] <= MAX_HALF_POINTS && scores[i] > best) {
			best = scores[i];
		}
	}
	if (best == 0) {
		return;
	}
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		if (scores[i] == best) {
			++gs->player[i].wins;
		}
	}
}

static void end_turn(struct setimig_state *gs) {
	for (unsigned i = 1; i <= NUM_PLAYERS; ++i) {
		const enum player_id id = (enum player_id)(
		    (gs->current_player + NUM_PLAYERS - i) % NUM_PLAYERS);
		if (!gs->player[id].passed) {
			gs->current_player = id;
			return;
		}
	}
	gs->revealed = true;
	score_round(gs);
}

static void pass_turn(struct setimig_state *gs) {
	gs->player[gs->current_player].passed = true;
	end_turn(gs);
}

static void reshuffle_discard_into_deck(struct setimig_state *gs) {
	SDL_memcpy(gs->deck.cards, gs->discard.cards,
		   gs->discard.num_cards * sizeof(gs->discard.cards[0]));
	gs->deck.num_cards = gs->discard.num_cards;
	gs->discard.num_cards = 0;
	shuffle(gs->deck.cards, gs->deck.num_cards);
}

static void draw_turn(struct app_state *as) {
	struct setimig_state *gs = as->setimig;
	if (gs->deck.num_cards == 0) {
		reshuffle_discard_into_deck(gs);
	}
	if (gs->deck.num_cards == 0) {
		pass_turn(gs);
		return;
	}
	struct player_data *player = &gs->player[gs->current_player];
	--gs->deck.num_cards;
	player->cards[player->num_cards] = gs->deck.cards[gs->deck.num_cards];
	++player->num_cards;
	player->num_receiving = 1;
	const SDL_FRect start = deck_rect(as);
	start_flight_to_hand(as, gs->card_size, &gs->flying, gs->current_player,
			     player->num_cards, (SDL_FPoint){start.x, start.y},
			     0.0, CARD_FLIGHT_MS);
	end_turn(gs);
}

#define TOP_BOT_STOP_HALF_POINTS 12U

static bool bot_wants_card(const struct setimig_state *gs, enum player_id id) {
	const struct player_data *player = &gs->player[id];
	if (id == TOP_BOT) {
		return hand_half_points(player) < TOP_BOT_STOP_HALF_POINTS;
	}
	return player->num_cards < 2;
}

static bool user_is_choosing(const struct setimig_state *gs) {
	return !gs->revealed && !card_motion_is_moving(&gs->flying.motion) &&
	       gs->current_player == USER && gs->player[USER].num_cards > 0;
}

static bool round_is_over(const struct setimig_state *gs) {
	return gs->revealed && !card_motion_is_moving(&gs->flying.motion);
}

static bool reached_wins_needed(const struct setimig_state *gs,
				enum player_id id) {
	return wins_needed(gs) != 0 && gs->player[id].wins >= wins_needed(gs);
}

static bool game_is_over(const struct setimig_state *gs) {
	if (!round_is_over(gs)) {
		return false;
	}
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		if (reached_wins_needed(gs, (enum player_id)i)) {
			return true;
		}
	}
	return false;
}

static void draw_clicked(void *userdata) {
	struct app_state *as = userdata;
	if (user_is_choosing(as->setimig)) {
		draw_turn(as);
	}
}

static void pass_clicked(void *userdata) {
	struct app_state *as = userdata;
	if (user_is_choosing(as->setimig)) {
		pass_turn(as->setimig);
	}
}

static void options_mouse_down(struct app_state *appstate,
			       const SDL_Event *event) {
	struct setimig_state *gs = appstate->setimig;
	if (input_event(appstate, gs->players_input, event)) {
		return;
	}
	if (input_event(appstate, gs->wins_input, event)) {
		gs->wins_limited = true;
		return;
	}
	if (!button_event(gs->no_limit_button, event)) {
		button_event(gs->start_button, event);
	}
}

static bool is_deck_click(const struct app_state *as, const SDL_Event *event) {
	const SDL_FRect rect = deck_rect(as);
	return is_left_click_in(event, &rect);
}

static bool play_mouse_down(struct app_state *as, const SDL_Event *event) {
	struct setimig_state *gs = as->setimig;
	if (user_is_choosing(gs)) {
		if (is_deck_click(as, event)) {
			draw_clicked(as);
			return true;
		}
		return button_event(gs->draw_button, event) ||
		       button_event(gs->pass_button, event);
	}
	if (game_is_over(gs)) {
		return button_event(gs->new_game_button, event);
	}
	if (round_is_over(gs)) {
		return button_event(gs->next_round_button, event);
	}
	return false;
}

static bool is_discard_click(const struct app_state *as,
			     const SDL_Event *event) {
	const SDL_FRect rect = discard_rect(as);
	return as->setimig->discard.num_cards > 0 &&
	       is_left_click_in(event, &rect);
}

void setimig_event(struct app_state *appstate, SDL_Event *event) {
	struct setimig_state *gs = appstate->setimig;
	struct button *buttons[NUM_BUTTONS];
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (gs->choosing_options) {
			options_mouse_down(appstate, event);
		} else if (gs->showing_discard) {
			gs->showing_discard = false;
		} else if (!play_mouse_down(appstate, event) &&
			   is_discard_click(appstate, event)) {
			gs->showing_discard = true;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		all_buttons(gs, buttons);
		for (unsigned i = 0; i < NUM_BUTTONS; ++i) {
			button_event(buttons[i], event);
		}
		break;
	case SDL_EVENT_WINDOW_RESIZED:
		setimig_resize(appstate);
		break;
	default:
		if (gs->choosing_options &&
		    !input_event(appstate, gs->players_input, event)) {
			input_event(appstate, gs->wins_input, event);
		}
		break;
	}
	button_set_enabled(gs->start_button,
			   players_in_input(gs->players_input) != 0);
}

static void setimig_update(struct app_state *as) {
	struct setimig_state *gs = as->setimig;
	if (card_motion_is_moving(&gs->flying.motion)) {
		if (!card_motion_arrived(&gs->flying.motion,
					 as->current_frame_ticks)) {
			return;
		}
		gs->player[gs->flying.player].num_receiving = 0;
	}
	if (gs->revealed) {
		return;
	}
	const struct player_data *player = &gs->player[gs->current_player];
	if (player->num_cards == 0) {
		draw_turn(as);
	} else if (gs->current_player != USER) {
		if (bot_wants_card(gs, gs->current_player)) {
			draw_turn(as);
		} else {
			pass_turn(gs);
		}
	}
}

static void draw_deck(struct app_state *as) {
	const struct setimig_state *gs = as->setimig;
	const SDL_FRect dest_rect = deck_rect(as);
	card_texture_draw(as->card_texture,
			  gs->deck.num_cards == 0 ? CARD_OUTLINE : CARD_BACK,
			  &dest_rect);
}

static void draw_discard(struct app_state *as) {
	const struct discard_data *discard = &as->setimig->discard;
	const SDL_FRect dest_rect = discard_rect(as);
	card_texture_draw(as->card_texture,
			  discard->num_cards == 0
			      ? CARD_OUTLINE
			      : discard->cards[discard->num_cards - 1],
			  &dest_rect);
}

static void draw_player_hand(struct app_state *as, enum player_id id,
			     SDL_FPoint mouse) {
	const struct setimig_state *gs = as->setimig;
	const struct player_data *player = &gs->player[id];
	draw_hand(as, gs->card_size, id, player->cards,
		  player->num_cards - player->num_receiving, player->num_cards,
		  id == USER || gs->revealed, NO_CARD_INDEX, mouse);
}

#define STAR_CORNERS 10U
#define STAR_INNER_RATIO 0.4F

static void draw_star(struct app_state *as, SDL_FPoint center, float radius) {
	static const SDL_FColor gold = {1.F, 0.84F, 0.F, 1.F};
	SDL_Vertex vertices[STAR_CORNERS + 1];
	int indices[3 * STAR_CORNERS];
	vertices[0] = (SDL_Vertex){.position = center, .color = gold};
	for (size_t k = 0; k < STAR_CORNERS; ++k) {
		const float r = k % 2 == 0 ? radius : radius * STAR_INNER_RATIO;
		const float angle = (2.F * SDL_PI_F * (float)k / STAR_CORNERS) -
				    (SDL_PI_F / 2.F);
		vertices[k + 1] =
		    (SDL_Vertex){.position = {center.x + (r * SDL_cosf(angle)),
					      center.y + (r * SDL_sinf(angle))},
				 .color = gold};
		indices[3 * k] = 0;
		indices[(3 * k) + 1] = (int)(k + 1);
		indices[(3 * k) + 2] = (int)(((k + 1) % STAR_CORNERS) + 1);
	}
	SDL_RenderGeometry(as->renderer, NULL, vertices,
			   (int)SDL_arraysize(vertices), indices,
			   (int)SDL_arraysize(indices));
}

static void draw_wins(struct app_state *as, enum player_id id) {
	const struct setimig_state *gs = as->setimig;
	const struct player_data *player = &gs->player[id];
	const float star_size = STAR_SIZE * gs->card_size.x;
	const float step = star_size * (1.F + STAR_GAP);
	const float half_hand =
	    player->num_cards == 0
		? 0.F
		: hand_length(gs->card_size, player->num_cards) / 2.F;
	const float first =
	    half_hand + (STAR_GAP * gs->card_size.x) + (star_size / 2.F);
	const SDL_FPoint center = seat_center(as, gs->card_size, id);
	SDL_FPoint direction;
	SDL_FPoint inward;
	switch (id) {
	case USER:
		direction = (SDL_FPoint){1.F, 0.F};
		inward = (SDL_FPoint){0.F, -1.F};
		break;
	case TOP_BOT:
		direction = (SDL_FPoint){1.F, 0.F};
		inward = (SDL_FPoint){0.F, 1.F};
		break;
	case LEFT_BOT:
		direction = (SDL_FPoint){0.F, 1.F};
		inward = (SDL_FPoint){1.F, 0.F};
		break;
	case RIGHT_BOT:
	default:
		direction = (SDL_FPoint){0.F, 1.F};
		inward = (SDL_FPoint){-1.F, 0.F};
		break;
	}
	static const float column_offsets[] = {0.F, 1.F, -1.F};
	const unsigned stars_per_column = SDL_arraysize(column_offsets);
	for (unsigned k = 0; k < player->wins; ++k) {
		const unsigned column = k / stars_per_column;
		const float along = first + ((float)column * step);
		const float across =
		    column_offsets[k % stars_per_column] * step;
		const SDL_FPoint star = {
		    center.x + (direction.x * along) + (inward.x * across),
		    center.y + (direction.y * along) + (inward.y * across)};
		draw_star(as, star, star_size / 2.F);
	}
}

static void draw_flying(struct app_state *as) {
	const struct setimig_state *gs = as->setimig;
	const enum player_id id = gs->flying.player;
	const struct player_data *player = &gs->player[id];
	draw_flying_card(as, gs->card_size, &gs->flying, player->cards,
			 player->num_cards, id == USER || gs->revealed);
}

static const SDL_Color option_color = {235, 235, 235, SDL_ALPHA_OPAQUE};
static const SDL_Color option_pressed_color = {190, 190, 190, SDL_ALPHA_OPAQUE};
static const SDL_Color selected_option_color = {250, 215, 90, SDL_ALPHA_OPAQUE};

static void draw_option_button(struct app_state *as, struct button *button,
			       bool selected) {
	SDL_Color color = selected ? selected_option_color : option_color;
	if (button_is_pressing(button)) {
		color = option_pressed_color;
	}
	button_draw(as, button, color);
}

static void draw_options(struct app_state *as) {
	struct setimig_state *gs = as->setimig;
	label_draw(as, gs->players_caption, NULL);
	label_draw(as, gs->wins_caption, NULL);
	draw_players_input(as, gs->players_input);
	input_draw(as, gs->wins_input,
		   gs->wins_limited ? selected_option_color : input_color);
	draw_option_button(as, gs->no_limit_button, !gs->wins_limited);
	button_draw_choice(as, gs->start_button);
}

static void draw_game_over(struct app_state *as) {
	struct setimig_state *gs = as->setimig;
	label_draw(
	    as, reached_wins_needed(gs, USER) ? gs->won_label : gs->lost_label,
	    &table_label_color);
	button_draw_choice(as, gs->new_game_button);
}

void setimig_iterate(struct app_state *appstate) {
	if (appstate->setimig->choosing_options) {
		draw_options(appstate);
		return;
	}
	setimig_update(appstate);
	SDL_FPoint mouse;
	SDL_GetMouseState(&mouse.x, &mouse.y);
	draw_deck(appstate);
	draw_discard(appstate);
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		draw_player_hand(appstate, (enum player_id)i, mouse);
		draw_wins(appstate, (enum player_id)i);
	}
	draw_flying(appstate);
	if (user_is_choosing(appstate->setimig)) {
		button_draw_choice(appstate, appstate->setimig->draw_button);
		button_draw_choice(appstate, appstate->setimig->pass_button);
	}
	if (game_is_over(appstate->setimig)) {
		draw_game_over(appstate);
	} else if (round_is_over(appstate->setimig)) {
		button_draw_choice(appstate,
				   appstate->setimig->next_round_button);
	}
	if (appstate->setimig->showing_discard) {
		const struct discard_data *discard =
		    &appstate->setimig->discard;
		draw_discard_spread(appstate, appstate->setimig->card_size,
				    discard->cards, discard->num_cards, mouse);
	}
}

void setimig_leave(struct app_state *appstate) {
	input_unfocus(appstate, appstate->setimig->players_input);
	input_unfocus(appstate, appstate->setimig->wins_input);
}

void setimig_quit(struct app_state *appstate) {
	quit_gui(appstate, appstate->setimig);
	SDL_free(appstate->setimig);
	appstate->setimig = NULL;
}
