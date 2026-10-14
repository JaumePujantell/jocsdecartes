#ifndef CARD_MOTION_H
#define CARD_MOTION_H

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include <stdbool.h>

struct card_motion {
	SDL_FPoint from;
	double from_angle;
	Uint64 start_ticks;
	Uint64 duration_ms;
	bool moving;
};

void card_motion_start(struct card_motion *motion, SDL_FPoint from,
		       double from_angle, SDL_FPoint to, float card_width,
		       Uint64 max_duration_ms);
void card_motion_stop(struct card_motion *motion);
bool card_motion_is_moving(const struct card_motion *motion);
bool card_motion_arrived(struct card_motion *motion, Uint64 now);

SDL_FPoint card_motion_position(const struct card_motion *motion, SDL_FPoint to,
				Uint64 now);
double card_motion_angle(const struct card_motion *motion, double to_angle,
			 Uint64 now);

#endif // !CARD_MOTION_H
