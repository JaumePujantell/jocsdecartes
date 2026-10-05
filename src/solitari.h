#ifndef SOLITARI_H
#define SOLITARI_H

#include <SDL3/SDL_events.h>

#include <stdbool.h>

#include "app.h"

struct solitari_state;

bool solitari_init(struct app_state *appstate);
void solitari_event(struct app_state *appstate, SDL_Event *event);
void solitari_iterate(struct app_state *appstate);
void solitari_resize(struct app_state *appstate);
void solitari_restart(struct solitari_state *gamestate);
void solitari_quit(struct solitari_state *gamestate);

#endif // !SOLITARI_H