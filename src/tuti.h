#ifndef TUTI_H
#define TUTI_H

#include <SDL3/SDL_events.h>

#include <stdbool.h>

#include "app.h"

struct tuti_state;

bool tuti_init(struct app_state *appstate);
void tuti_event(struct app_state *appstate, SDL_Event *event);
void tuti_iterate(struct app_state *appstate);
void tuti_resize(struct app_state *appstate);
void tuti_restart(struct tuti_state *gamestate);
void tuti_choose_options(struct tuti_state *gamestate);
bool tuti_is_choosing_options(const struct tuti_state *gamestate);
void tuti_leave(struct app_state *appstate);
void tuti_quit(struct app_state *appstate);

#endif // !TUTI_H
