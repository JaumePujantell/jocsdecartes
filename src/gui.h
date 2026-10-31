#ifndef GUI_H
#define GUI_H

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>

#include <stdbool.h>

#include "app.h"

struct gui_state;
struct button;
struct label;
struct input;
typedef void (*button_callback)(void *userdata);

extern const SDL_Color green_button_color;
extern const SDL_Color green_button_pressed_color;

bool gui_init(struct app_state *appstate);
bool gui_event(struct app_state *appstate, SDL_Event *event);
void gui_iterate(struct app_state *appstate);
void gui_quit(struct gui_state *guistate);

struct button *button_init(struct gui_state *guistate, const char *text,
			   button_callback on_click, void *userdata);
struct button *button_init_icon(SDL_Texture *icon, float w, float h,
				button_callback on_click, void *userdata);
bool button_event(struct button *button, const SDL_Event *event);
void button_draw(struct app_state *appstate, struct button *button,
		 SDL_Color color);
void button_draw_choice(struct app_state *appstate, struct button *button);
void button_quit(struct button *button);

SDL_FPoint button_get_size(const struct button *button);
struct label *button_get_label(struct button *button);
float button_get_font_size(const struct button *button);

void button_fit_width(struct button *button, float target_width);
void button_group_fit_width(struct button *buttons[], unsigned count,
			    float target_width);
void button_set_position(struct button *button, float x, float y);

bool button_is_hovered(const struct button *button, SDL_FPoint point);
bool button_is_pressing(const struct button *button);
void button_set_enabled(struct button *button, bool enabled);
bool button_is_enabled(const struct button *button);

struct label *label_init(struct gui_state *guistate, const char *text);
void label_draw(struct app_state *appstate, struct label *label,
		const SDL_Color *background_color);
void label_quit(struct label *label);
void label_fit_width(struct label *label, float target_width);
void label_group_fit_width(struct label *labels[], unsigned count,
			   float target_width);
void label_set_position(struct label *label, float x, float y);
SDL_FPoint label_get_size(const struct label *label);

struct input *input_init(struct gui_state *guistate, const char *text);
bool input_event(struct app_state *appstate, struct input *input,
		 const SDL_Event *event);
void input_draw(struct app_state *appstate, struct input *input,
		SDL_Color background_color);
void input_quit(struct app_state *appstate, struct input *input);
void input_unfocus(struct app_state *appstate, struct input *input);
bool input_has_focus(const struct app_state *appstate,
		     const struct input *input);
void input_set_position(struct app_state *appstate, struct input *input,
			float x, float y);
void input_set_size(struct app_state *appstate, struct input *input, float w,
		    float h);
void input_set_font_size(struct app_state *appstate, struct input *input,
			 float font_size);
const char *input_get_text(const struct input *input);

#endif // !GUI_H
