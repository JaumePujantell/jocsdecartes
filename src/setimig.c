#include "setimig.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "app.h"
#include "card_motion.h"
#include "card_texture.h"
#include "cards.h"
#include "gui.h"

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

enum player_id { USER, LEFT_BOT, TOP_BOT, RIGHT_BOT, NUM_PLAYERS };

struct flying_data {
	enum player_id player;
	struct card_motion motion;
};

#define CARD_FLIGHT_MS 300U

#define MIN_PLAYERS 2U
#define DEFAULT_PLAYERS_TEXT "4"

#define NUM_OPTION_COLUMNS 3U
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

static unsigned players_in_input(const struct setimig_state *gs) {
	const unsigned long players =
	    SDL_strtoul(input_get_text(gs->players_input), NULL, 10);
	return players >= MIN_PLAYERS && players <= NUM_PLAYERS
		   ? (unsigned)players
		   : 0;
}

static unsigned wins_needed(const struct setimig_state *gs) {
	if (!gs->wins_limited) {
		return 0;
	}
	const unsigned long wins =
	    SDL_strtoul(input_get_text(gs->wins_input), NULL, 10);
	return (unsigned)SDL_min(wins, SDL_MAX_UINT32);
}

static bool player_is_active(const struct setimig_state *gs,
			     enum player_id id) {
	switch (gs->num_players) {
	case 2:
		return id == USER || id == TOP_BOT;
	case 3:
		return id != TOP_BOT;
	default:
		return true;
	}
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
		player->passed =
		    !player_is_active(gamestate, (enum player_id)i);
	}
	card_motion_stop(&gamestate->flying.motion);
	gamestate->current_player = USER;
	gamestate->revealed = false;
}

static void start_game(struct setimig_state *gamestate) {
	gamestate->choosing_options = false;
	const unsigned players = players_in_input(gamestate);
	if (players != 0) {
		gamestate->num_players = players;
	}
	if (wins_needed(gamestate) == 0) {
		gamestate->wins_limited = false;
	}
	SDL_memcpy(gamestate->deck.cards, SMALL_SPANISH_DECK,
		   sizeof(SMALL_SPANISH_DECK));
	gamestate->deck.num_cards = SMALL_SPANISH_DECK_SIZE;
	shuffle(gamestate->deck.cards, SMALL_SPANISH_DECK_SIZE);
	gamestate->discard.num_cards = 0;
	for (unsigned i = 0; i < NUM_PLAYERS; ++i) {
		gamestate->player[i].num_cards = 0;
		gamestate->player[i].wins = 0;
	}
	start_round(gamestate);
}

void setimig_restart(struct setimig_state *gamestate) {
	if (!gamestate->choosing_options) {
		start_game(gamestate);
	}
}

void setimig_choose_options(struct setimig_state *gamestate) {
	gamestate->choosing_options = true;
	gamestate->showing_discard = false;
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

#define CARD_WINDOW_HEIGHT_FRACTION 0.25F
#define CARD_WINDOW_WIDTH_FRACTION 0.12F
#define CARD_SHOW_MARGIN 0.25F
#define OUTER_MARGIN 0.1F
#define PILE_GAP 0.2F
#define BUTTON_WIDTH_FRACTION 0.8F
#define BUTTON_GAP 0.2F
#define STAR_SIZE 0.2F
#define STAR_GAP 0.1F
#define OPTION_ROW_WIDTH_FRACTION 0.8F
#define OPTION_GAP_FRACTION 0.03F
#define DISCARD_SPREAD_WIDTH_FRACTION 0.9F
#define DISCARD_ROW_GAP 0.1F

#define LEFT_BOT_CARD_ANGLE 90.0
#define RIGHT_BOT_CARD_ANGLE (-90.0)

static void position_options(struct app_state *appstate) {
	struct setimig_state *gs = appstate->setimig;
	const float window_width = (float)appstate->window_size.x;
	const float window_height = (float)appstate->window_size.y;
	const float gap = OPTION_GAP_FRACTION * window_width;
	struct label *cells[] = {gs->players_caption, gs->wins_caption,
				 button_get_label(gs->no_limit_button)};
	label_group_fit_width(cells, SDL_arraysize(cells),
			      ((OPTION_ROW_WIDTH_FRACTION * window_width) -
			       ((float)(NUM_OPTION_COLUMNS - 1) * gap)) /
				  (float)NUM_OPTION_COLUMNS);
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
	const float card_ratio =
	    card_texture_aspect_ratio(appstate->card_texture);
	gs->card_size.x =
	    fminf((float)appstate->window_size.x * CARD_WINDOW_WIDTH_FRACTION,
		  (float)appstate->window_size.y * CARD_WINDOW_HEIGHT_FRACTION *
		      card_ratio);
	gs->card_size.y = gs->card_size.x / card_ratio;

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

static unsigned all_buttons(struct setimig_state *gs,
			    struct button *buttons[NUM_BUTTONS]) {
	unsigned n = 0;
	buttons[n++] = gs->draw_button;
	buttons[n++] = gs->pass_button;
	buttons[n++] = gs->next_round_button;
	buttons[n++] = gs->start_button;
	buttons[n++] = gs->new_game_button;
	buttons[n++] = gs->no_limit_button;
	return n;
}

static void quit_gui(struct app_state *appstate, struct setimig_state *gs) {
	struct button *buttons[NUM_BUTTONS];
	const unsigned count = all_buttons(gs, buttons);
	for (unsigned i = 0; i < count; ++i) {
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
	struct setimig_state *gs = SDL_malloc(sizeof(*gs));
	if (!gs) {
		return false;
	}
	SDL_memset(gs, 0, sizeof(*gs));
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

struct card_placement {
	SDL_FRect rect;
	double angle;
};

static float hand_length(const struct setimig_state *gs, unsigned n) {
	return gs->card_size.x +
	       ((float)(n - 1) * CARD_SHOW_MARGIN * gs->card_size.x);
}

static struct card_placement turned_card(const struct setimig_state *gs,
					 float left, float top, double angle) {
	return (struct card_placement){
	    .rect = {.x = left + ((gs->card_size.y - gs->card_size.x) / 2.F),
		     .y = top + ((gs->card_size.x - gs->card_size.y) / 2.F),
		     .w = gs->card_size.x,
		     .h = gs->card_size.y},
	    .angle = angle};
}

static struct card_placement hand_card(const struct app_state *as,
				       enum player_id id, unsigned j,
				       unsigned n) {
	const struct setimig_state *gs = as->setimig;
	const float window_width = (float)as->window_size.x;
	const float window_height = (float)as->window_size.y;
	const float offset = (float)j * CARD_SHOW_MARGIN * gs->card_size.x;
	const float outer_margin = OUTER_MARGIN * gs->card_size.y;
	const float row_left = ((window_width - hand_length(gs, n)) / 2.F);
	const float column_top = ((window_height - hand_length(gs, n)) / 2.F);
	struct card_placement placement = {.rect = {.x = row_left + offset,
						    .w = gs->card_size.x,
						    .h = gs->card_size.y}};
	switch (id) {
	case USER:
		placement.rect.y =
		    window_height - gs->card_size.y - outer_margin;
		break;
	case TOP_BOT:
		placement.rect.y = outer_margin;
		break;
	case LEFT_BOT:
		placement = turned_card(gs, outer_margin, column_top + offset,
					LEFT_BOT_CARD_ANGLE);
		break;
	case RIGHT_BOT:
	default:
		placement = turned_card(
		    gs, window_width - gs->card_size.y - outer_margin,
		    column_top + offset, RIGHT_BOT_CARD_ANGLE);
		break;
	}
	return placement;
}

static SDL_FRect deck_rect(const struct app_state *as) {
	const struct setimig_state *gs = as->setimig;
	const float gap = PILE_GAP * gs->card_size.x;
	return (SDL_FRect){
	    .x = ((float)as->window_size.x - (2.F * gs->card_size.x) - gap) /
		 2.F,
	    .y = ((float)as->window_size.y - gs->card_size.y) / 2.F,
	    .w = gs->card_size.x,
	    .h = gs->card_size.y};
}

static SDL_FRect discard_rect(const struct app_state *as) {
	const struct setimig_state *gs = as->setimig;
	SDL_FRect rect = deck_rect(as);
	rect.x += gs->card_size.x + (PILE_GAP * gs->card_size.x);
	return rect;
}

struct discard_spread {
	SDL_FPoint first;
	SDL_FPoint step;
	unsigned per_row;
};

/* Lays the discard pile out in rows of overlapping cards, centered in the
 * window. Rows overlap vertically too when the window is too short to fit
 * them apart. */
static struct discard_spread spread_discard(const struct app_state *as) {
	const struct setimig_state *gs = as->setimig;
	const float window_width = (float)as->window_size.x;
	const float window_height = (float)as->window_size.y;
	const unsigned num_cards = gs->discard.num_cards;
	struct discard_spread spread = {
	    .step = {CARD_SHOW_MARGIN * gs->card_size.x,
		     gs->card_size.y * (1.F + DISCARD_ROW_GAP)}};
	const float row_room =
	    (DISCARD_SPREAD_WIDTH_FRACTION * window_width) - gs->card_size.x;
	spread.per_row =
	    (unsigned)SDL_max(1.F, 1.F + (row_room / spread.step.x));
	const unsigned num_rows =
	    SDL_max(1U, (num_cards + spread.per_row - 1) / spread.per_row);
	const unsigned row_cards = SDL_min(num_cards, spread.per_row);
	if (num_rows > 1) {
		spread.step.y =
		    SDL_min(spread.step.y, (window_height - gs->card_size.y) /
					       (float)(num_rows - 1));
	}
	spread.first.x = (window_width - hand_length(gs, row_cards)) / 2.F;
	spread.first.y = (window_height - gs->card_size.y -
			  ((float)(num_rows - 1) * spread.step.y)) /
			 2.F;
	return spread;
}

static card_t shown_side(const struct app_state *as, enum player_id id,
			 card_t card) {
	return id == USER || as->setimig->revealed ? card : CARD_BACK;
}

static void draw_card(struct app_state *as, card_t card,
		      const struct card_placement *placement) {
	card_texture_draw_rotated(as->card_texture, card, &placement->rect,
				  placement->angle);
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
	const SDL_FRect end =
	    hand_card(as, gs->current_player, player->num_cards - 1,
		      player->num_cards)
		.rect;
	gs->flying.player = gs->current_player;
	card_motion_start(&gs->flying.motion, (SDL_FPoint){start.x, start.y},
			  0.0, (SDL_FPoint){end.x, end.y}, gs->card_size.x,
			  CARD_FLIGHT_MS);
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

static bool play_mouse_down(struct setimig_state *gs, const SDL_Event *event) {
	if (user_is_choosing(gs)) {
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
	const SDL_FPoint p = {event->button.x, event->button.y};
	const SDL_FRect rect = discard_rect(as);
	return event->button.button == SDL_BUTTON_LEFT &&
	       as->setimig->discard.num_cards > 0 &&
	       SDL_PointInRectFloat(&p, &rect);
}

void setimig_event(struct app_state *appstate, SDL_Event *event) {
	struct setimig_state *gs = appstate->setimig;
	struct button *buttons[NUM_BUTTONS];
	unsigned count;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (gs->choosing_options) {
			options_mouse_down(appstate, event);
		} else if (gs->showing_discard) {
			gs->showing_discard = false;
		} else if (!play_mouse_down(gs, event) &&
			   is_discard_click(appstate, event)) {
			gs->showing_discard = true;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		count = all_buttons(gs, buttons);
		for (unsigned i = 0; i < count; ++i) {
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
	button_set_enabled(gs->start_button, players_in_input(gs) != 0);
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

static const SDL_Color discard_backdrop_color = {0, 0, 0, 160};

static SDL_FRect spread_card_rect(const struct app_state *as,
				  const struct discard_spread *spread,
				  unsigned i) {
	const unsigned row = i / spread->per_row;
	const unsigned column = i % spread->per_row;
	return (SDL_FRect){.x = spread->first.x +
				((float)column * spread->step.x),
			   .y = spread->first.y + ((float)row * spread->step.y),
			   .w = as->setimig->card_size.x,
			   .h = as->setimig->card_size.y};
}

static void draw_discard_spread(struct app_state *as, SDL_FPoint mouse) {
	const struct discard_data *discard = &as->setimig->discard;
	const struct discard_spread spread = spread_discard(as);
	SDL_SetRenderDrawColor(
	    as->renderer, discard_backdrop_color.r, discard_backdrop_color.g,
	    discard_backdrop_color.b, discard_backdrop_color.a);
	SDL_RenderFillRect(as->renderer, NULL);
	for (unsigned i = 0; i < discard->num_cards; ++i) {
		const SDL_FRect dest_rect = spread_card_rect(as, &spread, i);
		card_texture_draw(as->card_texture, discard->cards[i],
				  &dest_rect);
	}
	for (unsigned i = discard->num_cards; i-- > 0;) {
		const SDL_FRect dest_rect = spread_card_rect(as, &spread, i);
		if (SDL_PointInRectFloat(&mouse, &dest_rect)) {
			card_texture_draw(as->card_texture, discard->cards[i],
					  &dest_rect);
			break;
		}
	}
}

static void draw_hand(struct app_state *as, enum player_id id,
		      SDL_FPoint mouse) {
	const struct setimig_state *gs = as->setimig;
	const struct player_data *player = &gs->player[id];
	const unsigned num_settled = player->num_cards - player->num_receiving;
	SDL_FRect visible_rect = {.w = CARD_SHOW_MARGIN * gs->card_size.x,
				  .h = gs->card_size.y};
	bool draw_hovered = false;
	card_t hovered_card;
	struct card_placement hovered_placement;
	for (unsigned j = 0; j < num_settled; ++j) {
		const card_t card = shown_side(as, id, player->cards[j]);
		const struct card_placement placement =
		    hand_card(as, id, j, player->num_cards);
		draw_card(as, card, &placement);
		visible_rect.x = placement.rect.x;
		visible_rect.y = placement.rect.y;
		if (id == USER && j < num_settled - 1 &&
		    SDL_PointInRectFloat(&mouse, &visible_rect)) {
			draw_hovered = true;
			hovered_card = card;
			hovered_placement = placement;
		}
	}
	if (draw_hovered) {
		draw_card(as, hovered_card, &hovered_placement);
	}
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
	const float window_width = (float)as->window_size.x;
	const float window_height = (float)as->window_size.y;
	const float outer_margin = OUTER_MARGIN * gs->card_size.y;
	const float star_size = STAR_SIZE * gs->card_size.x;
	const float step = star_size * (1.F + STAR_GAP);
	const float half_hand = player->num_cards == 0
				    ? 0.F
				    : hand_length(gs, player->num_cards) / 2.F;
	const float first =
	    half_hand + (STAR_GAP * gs->card_size.x) + (star_size / 2.F);
	SDL_FPoint center;
	SDL_FPoint direction;
	SDL_FPoint inward;
	switch (id) {
	case USER:
		center = (SDL_FPoint){window_width / 2.F,
				      window_height - outer_margin -
					  (gs->card_size.y / 2.F)};
		direction = (SDL_FPoint){1.F, 0.F};
		inward = (SDL_FPoint){0.F, -1.F};
		break;
	case TOP_BOT:
		center = (SDL_FPoint){window_width / 2.F,
				      outer_margin + (gs->card_size.y / 2.F)};
		direction = (SDL_FPoint){1.F, 0.F};
		inward = (SDL_FPoint){0.F, 1.F};
		break;
	case LEFT_BOT:
		center = (SDL_FPoint){outer_margin + (gs->card_size.y / 2.F),
				      window_height / 2.F};
		direction = (SDL_FPoint){0.F, 1.F};
		inward = (SDL_FPoint){1.F, 0.F};
		break;
	case RIGHT_BOT:
	default:
		center = (SDL_FPoint){window_width - outer_margin -
					  (gs->card_size.y / 2.F),
				      window_height / 2.F};
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

static const SDL_Color disabled_button_color = {170, 170, 170, 200};

static void draw_choice_button(struct app_state *as, struct button *button) {
	SDL_Color color = button_is_pressing(button)
			      ? green_button_pressed_color
			      : green_button_color;
	if (!button_is_enabled(button)) {
		color = disabled_button_color;
	}
	button_draw(as, button, color);
}

static void draw_flying(struct app_state *as) {
	const struct setimig_state *gs = as->setimig;
	const struct card_motion *motion = &gs->flying.motion;
	if (!card_motion_is_moving(motion)) {
		return;
	}
	const enum player_id id = gs->flying.player;
	const struct player_data *player = &gs->player[id];
	const Uint64 now = as->current_frame_ticks;
	struct card_placement placement =
	    hand_card(as, id, player->num_cards - 1, player->num_cards);
	const SDL_FPoint position = card_motion_position(
	    motion, (SDL_FPoint){placement.rect.x, placement.rect.y}, now);
	placement.rect.x = position.x;
	placement.rect.y = position.y;
	placement.angle = card_motion_angle(motion, placement.angle, now);
	draw_card(as, shown_side(as, id, player->cards[player->num_cards - 1]),
		  &placement);
}

static const SDL_Color option_color = {235, 235, 235, SDL_ALPHA_OPAQUE};
static const SDL_Color option_pressed_color = {190, 190, 190, SDL_ALPHA_OPAQUE};
static const SDL_Color selected_option_color = {250, 215, 90, SDL_ALPHA_OPAQUE};
static const SDL_Color label_color = {255, 255, 255, 220};
static const SDL_Color input_color = {255, 255, 255, SDL_ALPHA_OPAQUE};
static const SDL_Color invalid_input_color = {240, 150, 150, SDL_ALPHA_OPAQUE};

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
	input_draw(as, gs->players_input,
		   players_in_input(gs) != 0 ? input_color
					     : invalid_input_color);
	input_draw(as, gs->wins_input,
		   gs->wins_limited ? selected_option_color : input_color);
	draw_option_button(as, gs->no_limit_button, !gs->wins_limited);
	draw_choice_button(as, gs->start_button);
}

static void draw_game_over(struct app_state *as) {
	struct setimig_state *gs = as->setimig;
	label_draw(
	    as, reached_wins_needed(gs, USER) ? gs->won_label : gs->lost_label,
	    &label_color);
	draw_choice_button(as, gs->new_game_button);
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
		draw_hand(appstate, (enum player_id)i, mouse);
		draw_wins(appstate, (enum player_id)i);
	}
	draw_flying(appstate);
	if (user_is_choosing(appstate->setimig)) {
		draw_choice_button(appstate, appstate->setimig->draw_button);
		draw_choice_button(appstate, appstate->setimig->pass_button);
	}
	if (game_is_over(appstate->setimig)) {
		draw_game_over(appstate);
	} else if (round_is_over(appstate->setimig)) {
		draw_choice_button(appstate,
				   appstate->setimig->next_round_button);
	}
	if (appstate->setimig->showing_discard) {
		draw_discard_spread(appstate, mouse);
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
