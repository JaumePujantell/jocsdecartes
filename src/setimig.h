#ifndef SETIMIG_H
#define SETIMIG_H

#include <SDL3/SDL_events.h>

#include <stdbool.h>

#include "app.h"

struct setimig_state;

bool setimig_init(struct app_state *appstate);
void setimig_event(struct app_state *appstate, SDL_Event *event);
void setimig_iterate(struct app_state *appstate);
void setimig_resize(struct app_state *appstate);
void setimig_restart(struct setimig_state *gamestate);
void setimig_choose_options(struct setimig_state *gamestate);
void setimig_leave(struct app_state *appstate);
void setimig_quit(struct app_state *appstate);

#endif // !SETIMIG_H
