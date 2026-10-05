#ifndef GUI_H
#define GUI_H

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>

#include <stdbool.h>

#include "app.h"

struct gui_state;
struct button;
typedef void (*button_callback)(void *userdata);

extern const SDL_Color green_button_color;
extern const SDL_Color green_button_pressed_color;

bool gui_init(struct app_state *appstate);
bool gui_event(struct app_state *appstate, SDL_Event *event);
void gui_iterate(struct app_state *appstate);
void gui_quit(struct gui_state *guistate);

struct button *button_init(struct gui_state *guistate, const char *label,
			   button_callback on_click, void *userdata);
bool button_event(struct button *button, const SDL_Event *event);
void button_draw(struct app_state *appstate, struct button *button,
		 SDL_Color fill_color);
void button_quit(struct button *button);

SDL_FPoint button_get_size(const struct button *button);

void button_fit_width(struct button *button, float target_width);
void button_set_position(struct button *button, float x, float y);

bool button_is_hovered(const struct button *button, SDL_FPoint point);
bool button_is_pressing(const struct button *button);

#endif // !GUI_H
