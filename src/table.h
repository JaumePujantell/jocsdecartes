#ifndef TABLE_H
#define TABLE_H

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include <limits.h>
#include <stdbool.h>

#include "app.h"
#include "card_motion.h"
#include "cards.h"
#include "gui.h"

enum player_id { USER, LEFT_BOT, TOP_BOT, RIGHT_BOT, NUM_PLAYERS };

#define NUM_PLACES (NUM_PLAYERS - 1)

#define MIN_PLAYERS 2U
#define DEFAULT_PLAYERS_TEXT "4"

#define CARD_SHOW_MARGIN 0.25F
#define OUTER_MARGIN 0.1F

#define NO_CARD_INDEX UINT_MAX

#define NUM_OPTION_COLUMNS 3U

enum pile_id { LEFT_PILE, RIGHT_PILE };

struct card_placement {
	SDL_FRect rect;
	double angle;
};

struct flying_data {
	enum player_id player;
	struct card_motion motion;
};

struct players_options {
	struct label *caption;
	struct input *input;
	struct button *start_button;
	button_callback on_start;
	void *start_userdata;
};

extern const SDL_Color table_label_color;
extern const SDL_Color input_color;

unsigned players_in_input(const struct input *input);
bool player_is_active(unsigned num_players, enum player_id id);
enum player_id next_active_player(unsigned num_players, enum player_id id);
enum player_id random_active_player(unsigned num_players);
bool is_left_click_in(const SDL_Event *event, const SDL_FRect *rect);

float option_gap(const struct app_state *as);
float option_column_width(const struct app_state *as);

bool players_options_init(struct gui_state *gui,
			  struct players_options *options,
			  button_callback on_start, void *userdata);
void players_options_quit(struct app_state *as,
			  struct players_options *options);
void players_options_position(struct app_state *as,
			      struct players_options *options);
void players_options_mouse_down(struct app_state *as,
				struct players_options *options,
				const SDL_Event *event);
void players_options_event(struct app_state *as,
			   struct players_options *options,
			   const SDL_Event *event);
void players_options_update_start(struct players_options *options);
void players_options_draw(struct app_state *as,
			  struct players_options *options);

bool place_labels_init(struct gui_state *gui, struct label *labels[NUM_PLACES]);
void place_labels_quit(struct label *labels[NUM_PLACES]);
void place_labels_fit(struct label *labels[NUM_PLACES], SDL_FPoint card_size);
void draw_place(struct app_state *as, struct label *labels[NUM_PLACES],
		SDL_FPoint card_size, enum player_id id, unsigned place);

SDL_FPoint table_card_size(const struct app_state *as);
SDL_FRect pile_rect(const struct app_state *as, SDL_FPoint card_size,
		    enum pile_id pile);
float hand_length(SDL_FPoint card_size, unsigned n);
struct card_placement hand_card(const struct app_state *as,
				SDL_FPoint card_size, enum player_id id,
				unsigned j, unsigned n);
SDL_FPoint seat_center(const struct app_state *as, SDL_FPoint card_size,
		       enum player_id id);
SDL_FPoint seat_inward(enum player_id id);
unsigned hand_card_at(const struct app_state *as, SDL_FPoint card_size,
		      enum player_id id, unsigned n, unsigned lifted,
		      SDL_FPoint point);
void start_flight_to_hand(const struct app_state *as, SDL_FPoint card_size,
			  struct flying_data *flying, enum player_id id,
			  unsigned n, SDL_FPoint from, double from_angle,
			  Uint64 duration_ms);

void draw_card(struct app_state *as, card_t card,
	       const struct card_placement *placement);
void draw_hand(struct app_state *as, SDL_FPoint card_size, enum player_id id,
	       const card_t *cards, unsigned num_settled, unsigned n,
	       bool face_up, unsigned lifted, SDL_FPoint mouse);
void draw_flying_card(struct app_state *as, SDL_FPoint card_size,
		      const struct flying_data *flying, const card_t *cards,
		      unsigned n, bool face_up);
void draw_turn_arrow(struct app_state *as, SDL_FPoint card_size,
		     enum player_id id);
void draw_discard_spread(struct app_state *as, SDL_FPoint card_size,
			 const card_t *cards, unsigned num_cards,
			 SDL_FPoint mouse);
void draw_players_input(struct app_state *as, struct input *input);

#endif // !TABLE_H
