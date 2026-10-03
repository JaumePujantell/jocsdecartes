#ifndef GUI_H
#define GUI_H

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "app.h"

struct gui_state;
struct button;
typedef void (*button_callback)(void *userdata);

bool gui_init(struct app_state *appstate);
/* Returns true when the event was handled by the GUI and should not reach
 * the current screen. */
bool gui_event(struct app_state *appstate, SDL_Event *event);
void gui_iterate(struct app_state *appstate);
void gui_quit(struct gui_state *guistate);

struct button *button_init(struct gui_state *guistate, const char *label,
			   button_callback on_click, void *userdata);
void button_fit_width(struct button *button, float target_width);
/* Sets the label font size and sizes the button to fit the padded label. */
void button_set_font_size(struct button *button, float font_size);
float button_font_size(const struct button *button);
void button_set_position(struct button *button, float x, float y);
void button_set_size(struct button *button, float w, float h);
SDL_FPoint button_size(const struct button *button);
void button_quit(struct button *button);
bool button_event(struct button *button, const SDL_Event *event);
bool button_is_hovered(const struct button *button, SDL_FPoint point);
bool button_is_pressing(const struct button *button);
void button_draw(struct app_state *appstate, struct button *button,
		 SDL_Color fill_color);

#endif // !GUI_H
