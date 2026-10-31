#include "table.h"

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

#define CARD_WINDOW_HEIGHT_FRACTION 0.25F
#define CARD_WINDOW_WIDTH_FRACTION 0.12F

#define LIFTED_CARD_RISE 0.2F
#define DISCARD_SPREAD_WIDTH_FRACTION 0.9F
#define DISCARD_ROW_GAP 0.1F

#define QUARTER_TURN_ANGLE 90.0
#define LEFT_BOT_CARD_ANGLE QUARTER_TURN_ANGLE
#define RIGHT_BOT_CARD_ANGLE (-QUARTER_TURN_ANGLE)

#define TURN_ARROW_SIZE 0.25F
#define TURN_ARROW_GAP 0.1F

#define PILE_GAP 0.2F
#define OPTION_ROW_WIDTH_FRACTION 0.8F
#define OPTION_GAP_FRACTION 0.03F

const SDL_Color table_label_color = {255, 255, 255, 220};
const SDL_Color input_color = {255, 255, 255, SDL_ALPHA_OPAQUE};
static const SDL_Color invalid_input_color = {240, 150, 150, SDL_ALPHA_OPAQUE};

unsigned players_in_input(const struct input *input) {
	const unsigned long players =
	    SDL_strtoul(input_get_text(input), NULL, 10);
	return players >= MIN_PLAYERS && players <= NUM_PLAYERS
		   ? (unsigned)players
		   : 0;
}

bool player_is_active(unsigned num_players, enum player_id id) {
	switch (num_players) {
	case 2:
		return id == USER || id == TOP_BOT;
	case 3:
		return id != TOP_BOT;
	default:
		return true;
	}
}

bool is_left_click_in(const SDL_Event *event, const SDL_FRect *rect) {
	const SDL_FPoint point = {event->button.x, event->button.y};
	return event->button.button == SDL_BUTTON_LEFT &&
	       SDL_PointInRectFloat(&point, rect);
}

float option_gap(const struct app_state *as) {
	return OPTION_GAP_FRACTION * (float)as->window_size.x;
}

float option_column_width(const struct app_state *as) {
	const float row_width =
	    OPTION_ROW_WIDTH_FRACTION * (float)as->window_size.x;
	return (row_width -
		((float)(NUM_OPTION_COLUMNS - 1) * option_gap(as))) /
	       (float)NUM_OPTION_COLUMNS;
}

SDL_FPoint table_card_size(const struct app_state *as) {
	const float card_ratio = card_texture_aspect_ratio(as->card_texture);
	SDL_FPoint size;
	size.x = fminf((float)as->window_size.x * CARD_WINDOW_WIDTH_FRACTION,
		       (float)as->window_size.y * CARD_WINDOW_HEIGHT_FRACTION *
			   card_ratio);
	size.y = size.x / card_ratio;
	return size;
}

SDL_FRect pile_rect(const struct app_state *as, SDL_FPoint card_size,
		    enum pile_id pile) {
	const float gap = PILE_GAP * card_size.x;
	const float left =
	    ((float)as->window_size.x - (2.F * card_size.x) - gap) / 2.F;
	return (SDL_FRect){.x = left +
				(pile == RIGHT_PILE ? card_size.x + gap : 0.F),
			   .y = ((float)as->window_size.y - card_size.y) / 2.F,
			   .w = card_size.x,
			   .h = card_size.y};
}

float hand_length(SDL_FPoint card_size, unsigned n) {
	return card_size.x + ((float)(n - 1) * CARD_SHOW_MARGIN * card_size.x);
}

static struct card_placement turned_card(SDL_FPoint card_size, float left,
					 float top, double angle) {
	return (struct card_placement){
	    .rect = {.x = left + ((card_size.y - card_size.x) / 2.F),
		     .y = top + ((card_size.x - card_size.y) / 2.F),
		     .w = card_size.x,
		     .h = card_size.y},
	    .angle = angle};
}

struct card_placement hand_card(const struct app_state *as,
				SDL_FPoint card_size, enum player_id id,
				unsigned j, unsigned n) {
	const float window_width = (float)as->window_size.x;
	const float window_height = (float)as->window_size.y;
	const float offset = (float)j * CARD_SHOW_MARGIN * card_size.x;
	const float outer_margin = OUTER_MARGIN * card_size.y;
	const float row_left =
	    ((window_width - hand_length(card_size, n)) / 2.F);
	const float column_top =
	    ((window_height - hand_length(card_size, n)) / 2.F);
	struct card_placement placement = {.rect = {.x = row_left + offset,
						    .w = card_size.x,
						    .h = card_size.y}};
	switch (id) {
	case USER:
		placement.rect.y = window_height - card_size.y - outer_margin;
		break;
	case TOP_BOT:
		placement.rect.y = outer_margin;
		break;
	case LEFT_BOT:
		placement =
		    turned_card(card_size, outer_margin, column_top + offset,
				LEFT_BOT_CARD_ANGLE);
		break;
	case RIGHT_BOT:
	default:
		placement = turned_card(
		    card_size, window_width - card_size.y - outer_margin,
		    column_top + offset, RIGHT_BOT_CARD_ANGLE);
		break;
	}
	return placement;
}

SDL_FPoint seat_center(const struct app_state *as, SDL_FPoint card_size,
		       enum player_id id) {
	const float window_width = (float)as->window_size.x;
	const float window_height = (float)as->window_size.y;
	const float from_edge =
	    (OUTER_MARGIN * card_size.y) + (card_size.y / 2.F);
	switch (id) {
	case USER:
		return (SDL_FPoint){window_width / 2.F,
				    window_height - from_edge};
	case TOP_BOT:
		return (SDL_FPoint){window_width / 2.F, from_edge};
	case LEFT_BOT:
		return (SDL_FPoint){from_edge, window_height / 2.F};
	case RIGHT_BOT:
	default:
		return (SDL_FPoint){window_width - from_edge,
				    window_height / 2.F};
	}
}

SDL_FPoint seat_inward(enum player_id id) {
	switch (id) {
	case USER:
		return (SDL_FPoint){0.F, -1.F};
	case TOP_BOT:
		return (SDL_FPoint){0.F, 1.F};
	case LEFT_BOT:
		return (SDL_FPoint){1.F, 0.F};
	case RIGHT_BOT:
	default:
		return (SDL_FPoint){-1.F, 0.F};
	}
}

void draw_turn_arrow(struct app_state *as, SDL_FPoint card_size,
		     enum player_id id) {
	const SDL_FColor color = {(float)table_label_color.r / 255.F,
				  (float)table_label_color.g / 255.F,
				  (float)table_label_color.b / 255.F,
				  (float)table_label_color.a / 255.F};
	const float size = TURN_ARROW_SIZE * card_size.x;
	const SDL_FPoint center = seat_center(as, card_size, id);
	const SDL_FPoint inward = seat_inward(id);
	const float tip_distance =
	    (card_size.y / 2.F) + (TURN_ARROW_GAP * card_size.y);
	const float base_distance = tip_distance + size;
	const SDL_FPoint tip = {center.x + (inward.x * tip_distance),
				center.y + (inward.y * tip_distance)};
	const SDL_FPoint base = {center.x + (inward.x * base_distance),
				 center.y + (inward.y * base_distance)};
	const SDL_FPoint across = {-inward.y * size / 2.F,
				   inward.x * size / 2.F};
	const SDL_Vertex vertices[] = {
	    {.position = tip, .color = color},
	    {.position = {base.x + across.x, base.y + across.y},
	     .color = color},
	    {.position = {base.x - across.x, base.y - across.y},
	     .color = color}};
	SDL_RenderGeometry(as->renderer, NULL, vertices,
			   (int)SDL_arraysize(vertices), NULL, 0);
}

static struct card_placement shown_hand_card(const struct app_state *as,
					     SDL_FPoint card_size,
					     enum player_id id, unsigned j,
					     unsigned n, unsigned lifted) {
	struct card_placement placement = hand_card(as, card_size, id, j, n);
	if (j == lifted) {
		placement.rect.y -= LIFTED_CARD_RISE * card_size.y;
	}
	return placement;
}

static SDL_FRect screen_rect(const struct card_placement *placement) {
	if (SDL_fabs(placement->angle) < QUARTER_TURN_ANGLE / 2.0) {
		return placement->rect;
	}
	const SDL_FRect *rect = &placement->rect;
	return (SDL_FRect){.x = rect->x + ((rect->w - rect->h) / 2.F),
			   .y = rect->y + ((rect->h - rect->w) / 2.F),
			   .w = rect->h,
			   .h = rect->w};
}

unsigned hand_card_at(const struct app_state *as, SDL_FPoint card_size,
		      enum player_id id, unsigned n, unsigned lifted,
		      SDL_FPoint point) {
	for (unsigned j = n; j-- > 0;) {
		const struct card_placement placement =
		    shown_hand_card(as, card_size, id, j, n, lifted);
		const SDL_FRect rect = screen_rect(&placement);
		if (SDL_PointInRectFloat(&point, &rect)) {
			return j;
		}
	}
	return NO_CARD_INDEX;
}

void draw_card(struct app_state *as, card_t card,
	       const struct card_placement *placement) {
	card_texture_draw_rotated(as->card_texture, card, &placement->rect,
				  placement->angle);
}

void draw_hand(struct app_state *as, SDL_FPoint card_size, enum player_id id,
	       const card_t *cards, unsigned num_settled, unsigned n,
	       bool face_up, unsigned lifted, SDL_FPoint mouse) {
	SDL_FRect visible_rect = {.w = CARD_SHOW_MARGIN * card_size.x,
				  .h = card_size.y};
	bool draw_hovered = false;
	card_t hovered_card;
	struct card_placement hovered_placement;
	for (unsigned j = 0; j < num_settled; ++j) {
		const card_t card = face_up ? cards[j] : CARD_BACK;
		const struct card_placement placement =
		    shown_hand_card(as, card_size, id, j, n, lifted);
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

void start_flight_to_hand(const struct app_state *as, SDL_FPoint card_size,
			  struct flying_data *flying, enum player_id id,
			  unsigned n, SDL_FPoint from, double from_angle,
			  Uint64 duration_ms) {
	const SDL_FRect end = hand_card(as, card_size, id, n - 1, n).rect;
	flying->player = id;
	card_motion_start(&flying->motion, from, from_angle,
			  (SDL_FPoint){end.x, end.y}, card_size.x, duration_ms);
}

void draw_flying_card(struct app_state *as, SDL_FPoint card_size,
		      const struct flying_data *flying, const card_t *cards,
		      unsigned n, bool face_up) {
	const struct card_motion *motion = &flying->motion;
	if (!card_motion_is_moving(motion)) {
		return;
	}
	const Uint64 now = as->current_frame_ticks;
	struct card_placement placement =
	    hand_card(as, card_size, flying->player, n - 1, n);
	const SDL_FPoint position = card_motion_position(
	    motion, (SDL_FPoint){placement.rect.x, placement.rect.y}, now);
	placement.rect.x = position.x;
	placement.rect.y = position.y;
	placement.angle = card_motion_angle(motion, placement.angle, now);
	draw_card(as, face_up ? cards[n - 1] : CARD_BACK, &placement);
}

struct discard_spread {
	SDL_FPoint first;
	SDL_FPoint step;
	unsigned per_row;
};

static struct discard_spread spread_discard(const struct app_state *as,
					    SDL_FPoint card_size,
					    unsigned num_cards) {
	const float window_width = (float)as->window_size.x;
	const float window_height = (float)as->window_size.y;
	struct discard_spread spread = {
	    .step = {CARD_SHOW_MARGIN * card_size.x,
		     card_size.y * (1.F + DISCARD_ROW_GAP)}};
	const float row_room =
	    (DISCARD_SPREAD_WIDTH_FRACTION * window_width) - card_size.x;
	spread.per_row =
	    (unsigned)SDL_max(1.F, 1.F + (row_room / spread.step.x));
	const unsigned num_rows =
	    SDL_max(1U, (num_cards + spread.per_row - 1) / spread.per_row);
	const unsigned row_cards = SDL_min(num_cards, spread.per_row);
	if (num_rows > 1) {
		spread.step.y =
		    SDL_min(spread.step.y, (window_height - card_size.y) /
					       (float)(num_rows - 1));
	}
	spread.first.x =
	    (window_width - hand_length(card_size, row_cards)) / 2.F;
	spread.first.y = (window_height - card_size.y -
			  ((float)(num_rows - 1) * spread.step.y)) /
			 2.F;
	return spread;
}

static SDL_FRect spread_card_rect(SDL_FPoint card_size,
				  const struct discard_spread *spread,
				  unsigned i) {
	const unsigned row = i / spread->per_row;
	const unsigned column = i % spread->per_row;
	return (SDL_FRect){.x = spread->first.x +
				((float)column * spread->step.x),
			   .y = spread->first.y + ((float)row * spread->step.y),
			   .w = card_size.x,
			   .h = card_size.y};
}

static const SDL_Color discard_backdrop_color = {0, 0, 0, 160};

void draw_discard_spread(struct app_state *as, SDL_FPoint card_size,
			 const card_t *cards, unsigned num_cards,
			 SDL_FPoint mouse) {
	const struct discard_spread spread =
	    spread_discard(as, card_size, num_cards);
	SDL_SetRenderDrawColor(
	    as->renderer, discard_backdrop_color.r, discard_backdrop_color.g,
	    discard_backdrop_color.b, discard_backdrop_color.a);
	SDL_RenderFillRect(as->renderer, NULL);
	for (unsigned i = 0; i < num_cards; ++i) {
		const SDL_FRect dest_rect =
		    spread_card_rect(card_size, &spread, i);
		card_texture_draw(as->card_texture, cards[i], &dest_rect);
	}
	for (unsigned i = num_cards; i-- > 0;) {
		const SDL_FRect dest_rect =
		    spread_card_rect(card_size, &spread, i);
		if (SDL_PointInRectFloat(&mouse, &dest_rect)) {
			card_texture_draw(as->card_texture, cards[i],
					  &dest_rect);
			break;
		}
	}
}

void draw_players_input(struct app_state *as, struct input *input) {
	input_draw(as, input,
		   players_in_input(input) != 0 ? input_color
						: invalid_input_color);
}
