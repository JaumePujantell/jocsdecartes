#ifndef SISET_H
#define SISET_H

#include <SDL3/SDL_events.h>

#include <stdbool.h>

#include "app.h"

struct siset_state;

bool siset_init(struct app_state *appstate);
void siset_event(struct app_state *appstate, SDL_Event *event);
void siset_iterate(struct app_state *appstate);
void siset_resize(struct app_state *appstate);
void siset_restart(struct siset_state *gamestate);
void siset_choose_options(struct siset_state *gamestate);
bool siset_is_choosing_options(const struct siset_state *gamestate);
void siset_leave(struct app_state *appstate);
void siset_quit(struct app_state *appstate);

#endif // !SISET_H
