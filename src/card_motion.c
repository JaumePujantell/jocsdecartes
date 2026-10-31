#include "card_motion.h"

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <math.h>
#include <stdbool.h>

#define FULL_FLIGHT_CARD_WIDTHS 6.F

void card_motion_start(struct card_motion *motion, SDL_FPoint from,
		       double from_angle, SDL_FPoint to, float card_width,
		       Uint64 max_duration_ms) {
	motion->from = from;
	motion->from_angle = from_angle;
	motion->start_ticks = SDL_GetTicks();
	const float distance = hypotf(to.x - from.x, to.y - from.y);
	const float full_distance = FULL_FLIGHT_CARD_WIDTHS * card_width;
	const float fraction =
	    full_distance > 0.F ? SDL_min(distance / full_distance, 1.F) : 1.F;
	motion->duration_ms =
	    (Uint64)SDL_lroundf((float)max_duration_ms * sqrtf(fraction));
	motion->moving = true;
}

void card_motion_stop(struct card_motion *motion) { motion->moving = false; }

bool card_motion_is_moving(const struct card_motion *motion) {
	return motion->moving;
}

static Uint64 elapsed_ms(const struct card_motion *motion, Uint64 now) {
	return now > motion->start_ticks ? now - motion->start_ticks : 0;
}

bool card_motion_arrived(struct card_motion *motion, Uint64 now) {
	if (!motion->moving || elapsed_ms(motion, now) < motion->duration_ms) {
		return false;
	}
	motion->moving = false;
	return true;
}

static float eased_progress(const struct card_motion *motion, Uint64 now) {
	if (!motion->moving || motion->duration_ms == 0) {
		return 1.F;
	}
	const float t = SDL_min(
	    (float)elapsed_ms(motion, now) / (float)motion->duration_ms, 1.F);
	return t * t * (3 - (2 * t));
}

SDL_FPoint card_motion_position(const struct card_motion *motion, SDL_FPoint to,
				Uint64 now) {
	const float progress = eased_progress(motion, now);
	return (SDL_FPoint){
	    motion->from.x + ((to.x - motion->from.x) * progress),
	    motion->from.y + ((to.y - motion->from.y) * progress),
	};
}

double card_motion_angle(const struct card_motion *motion, double to_angle,
			 Uint64 now) {
	const float progress = eased_progress(motion, now);
	return motion->from_angle +
	       ((to_angle - motion->from_angle) * (double)progress);
}
