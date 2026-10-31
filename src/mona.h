#ifndef MONA_H
#define MONA_H

#include <SDL3/SDL_events.h>

#include <stdbool.h>

#include "app.h"

struct mona_state;

bool mona_init(struct app_state *appstate);
void mona_event(struct app_state *appstate, SDL_Event *event);
void mona_iterate(struct app_state *appstate);
void mona_resize(struct app_state *appstate);
void mona_restart(struct mona_state *gamestate);
void mona_choose_options(struct mona_state *gamestate);
bool mona_is_choosing_options(const struct mona_state *gamestate);
void mona_leave(struct app_state *appstate);
void mona_quit(struct app_state *appstate);

#endif // !MONA_H
