#include "gui.h"

#include <SDL3/SDL_assert.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <math.h>

#include "app.h"

struct label {
	SDL_FRect rect;
	SDL_Texture *background;
	SDL_Point background_size;
	TTF_Text *text;
	TTF_Font *font;
};

struct button {
	struct label *label;
	SDL_Texture *icon;
	SDL_FRect icon_rect;
	bool pressing;
	bool disabled;
	button_callback on_click;
	void *userdata;
};

struct input {
	struct label *label;
};

struct gui_state {
	struct input *focused_input;
	TTF_TextEngine *text_engine;
	TTF_Font *font;
	struct button *burger_button;
	struct button *new_game_button;
	struct button *main_menu_button;
	struct button *quit_button;
	bool app_menu_open;
	int app_menu_window_side;
	int app_menu_icon_pixels;
};

#define MAX_APP_MENU_OPTIONS 3
#define APP_MENU_MARGIN_FRACTION 0.02F
#define APP_MENU_BUTTON_WIDTH_FRACTION 0.32F
#define MENU_ICON_SIZE_FRACTION 0.1F
#define DEFAULT_FONT_SIZE 14.F
#define LABEL_CORNER_RADIUS 0.3F

const SDL_Color green_button_color = {120, 205, 120, SDL_ALPHA_OPAQUE};
const SDL_Color green_button_pressed_color = {90, 170, 90, SDL_ALPHA_OPAQUE};

static const SDL_Color menu_button_color = {240, 240, 240, SDL_ALPHA_OPAQUE};
static const SDL_Color menu_button_pressed_color = {
    190,
    190,
    190,
    SDL_ALPHA_OPAQUE,
};

static const SDL_Color input_cursor_color = {0, 0, 0, SDL_ALPHA_OPAQUE};
#define INPUT_CURSOR_WIDTH 2.F

static const SDL_Color icon_color = {255, 255, 255, SDL_ALPHA_OPAQUE};
static const SDL_Color icon_pressed_color = {170, 170, 170, SDL_ALPHA_OPAQUE};

static void
get_visible_app_menu_buttons(struct app_state *as,
			     struct button *buttons[MAX_APP_MENU_OPTIONS],
			     unsigned *count) {
	unsigned n = 0;
	struct gui_state *gs = as->gui;
	if (current_game_can_restart(as)) {
		buttons[n++] = gs->new_game_button;
	}
	if (as->current_game != NO_GAME) {
		buttons[n++] = gs->main_menu_button;
	}
	buttons[n++] = gs->quit_button;
	*count = n;
}

static void position_app_menu(struct app_state *as) {
	SDL_Rect safe_area;
	SDL_GetWindowSafeArea(as->window, &safe_area);
	const float margin = SDL_roundf((float)as->gui->app_menu_window_side *
					APP_MENU_MARGIN_FRACTION);
	const float right = (float)(safe_area.x + safe_area.w);
	const float top = (float)safe_area.y;
	struct gui_state *gs = as->gui;
	SDL_FPoint burger_size = button_get_size(gs->burger_button);
	button_set_position(gs->burger_button, right - burger_size.x - margin,
			    top + margin);

	struct button *visible[MAX_APP_MENU_OPTIONS];
	unsigned count;
	get_visible_app_menu_buttons(as, visible, &count);

	float y = top + margin + burger_size.y + margin;
	for (unsigned i = 0; i < count; ++i) {
		SDL_FPoint size = button_get_size(visible[i]);
		button_set_position(visible[i], right - size.x - margin, y);
		y += size.y + margin;
	}
}

static void toggle_app_menu(void *userdata) {
	struct app_state *as = (struct app_state *)userdata;
	as->gui->app_menu_open = !as->gui->app_menu_open;
	if (as->gui->app_menu_open && as->gui->focused_input) {
		input_unfocus(as, as->gui->focused_input);
	}
}

static void restart_current_game_callback(void *userdata) {
	struct app_state *as = (struct app_state *)userdata;
	restart_current_game(as);
	as->gui->app_menu_open = false;
}

static void go_to_main_menu(void *userdata) {
	struct app_state *as = (struct app_state *)userdata;
	select_game(as, NO_GAME);
	as->gui->app_menu_open = false;
}

static void quit_app(void *userdata) {
	(void)userdata;
	SDL_Event quit_event = {.type = SDL_EVENT_QUIT};
	SDL_PushEvent(&quit_event);
}

static SDL_Point size_in_pixels(struct app_state *as, float w, float h) {
	const float density = window_pixel_density(as);
	return (SDL_Point){
	    (int)SDL_ceilf(w * density),
	    (int)SDL_ceilf(h * density),
	};
}

static SDL_Texture *svg_texture_from_stream(struct app_state *as,
					    SDL_IOStream *stream, float w,
					    float h) {
	SDL_Point pixels = size_in_pixels(as, w, h);
	SDL_Surface *surface = IMG_LoadSizedSVG_IO(stream, pixels.x, pixels.y);
	SDL_CloseIO(stream);
	if (!surface) {
		return NULL;
	}
	SDL_Texture *texture =
	    SDL_CreateTextureFromSurface(as->renderer, surface);
	SDL_DestroySurface(surface);
	if (texture) {
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	}
	return texture;
}

static SDL_Texture *load_svg_texture(struct app_state *as, const char *name,
				     float w, float h) {
	char *svg_path = NULL;
	if (!get_path_to_resource(&svg_path, "icons", name)) {
		return NULL;
	}
	SDL_IOStream *stream = SDL_IOFromFile(svg_path, "rb");
	SDL_free(svg_path);
	if (!stream) {
		return NULL;
	}
	return svg_texture_from_stream(as, stream, w, h);
}

static SDL_Texture *create_label_background(struct app_state *as, float w,
					    float h) {
	float radius = fminf(h * LABEL_CORNER_RADIUS, w / 2.F);
	char *svg = NULL;
	SDL_asprintf(
	    &svg,
	    "<svg xmlns='http://www.w3.org/2000/svg' width='%g' height='%g'>"
	    "<defs><linearGradient id='shade' x1='0' y1='0' x2='0' y2='1'>"
	    "<stop offset='0' stop-color='#ffffff'/>"
	    "<stop offset='1' stop-color='#dcdcdc'/>"
	    "</linearGradient></defs>"
	    "<rect x='0.5' y='0.5' width='%g' height='%g' rx='%g' ry='%g' "
	    "fill='url(#shade)' stroke='#000000' stroke-opacity='0.3' "
	    "stroke-width='1'/></svg>",
	    (double)w, (double)h, (double)(w - 1.F), (double)(h - 1.F),
	    (double)radius, (double)radius);
	if (!svg) {
		return NULL;
	}
	SDL_IOStream *stream = SDL_IOFromConstMem(svg, SDL_strlen(svg));
	SDL_Texture *texture =
	    stream ? svg_texture_from_stream(as, stream, w, h) : NULL;
	SDL_free(svg);
	return texture;
}

#define LABEL_PADDING_X 0.6F
#define LABEL_PADDING_Y 0.2F

static SDL_FPoint padded_text_size(const struct label *label) {
	int w;
	int h;
	TTF_GetTextSize(label->text, &w, &h);
	const float text_w = (float)w;
	const float text_h = (float)h;
	return (SDL_FPoint){
	    text_w + (2.F * LABEL_PADDING_X * text_h),
	    text_h + (2.F * LABEL_PADDING_Y * text_h),
	};
}

#define LABEL_MIN_FONT_SIZE 4.F

static void label_set_font_size(struct label *label, float font_size) {
	float clamped_size = fmaxf(font_size, LABEL_MIN_FONT_SIZE);
	TTF_SetFontSize(label->font, clamped_size);
	SDL_FPoint size = padded_text_size(label);
	label->rect.w = SDL_roundf(size.x);
	label->rect.h = SDL_roundf(size.y);
}

static void label_set_size(struct label *label, float w, float h) {
	label->rect.w = w;
	label->rect.h = h;
}

static SDL_FRect *button_rect(struct button *button) {
	return button->label ? &button->label->rect : &button->icon_rect;
}

static const SDL_FRect *button_const_rect(const struct button *button) {
	return button->label ? &button->label->rect : &button->icon_rect;
}

static bool fit_app_menu(struct app_state *as) {
	struct gui_state *gs = as->gui;
	const int window_side = SDL_min(as->window_size.x, as->window_size.y);
	const float icon_size =
	    SDL_roundf((float)window_side * MENU_ICON_SIZE_FRACTION);
	const int icon_pixels = size_in_pixels(as, icon_size, icon_size).x;
	if (window_side == gs->app_menu_window_side &&
	    icon_pixels == gs->app_menu_icon_pixels) {
		return true;
	}

	struct button *options[] = {
	    gs->new_game_button,
	    gs->main_menu_button,
	    gs->quit_button,
	};
	button_group_fit_width(options, SDL_arraysize(options),
			       (float)window_side *
				   APP_MENU_BUTTON_WIDTH_FRACTION);

	SDL_Texture *icon =
	    load_svg_texture(as, "menu.svg", icon_size, icon_size);
	if (!icon) {
		return false;
	}
	struct button *burger = gs->burger_button;
	if (burger->icon) {
		SDL_DestroyTexture(burger->icon);
	}
	burger->icon = icon;
	burger->icon_rect.w = icon_size;
	burger->icon_rect.h = icon_size;

	gs->app_menu_window_side = window_side;
	gs->app_menu_icon_pixels = icon_pixels;
	return true;
}

bool gui_init(struct app_state *appstate) {
	struct gui_state *gs = SDL_calloc(1, sizeof(*gs));
	if (!gs) {
		return false;
	}
	appstate->gui = gs;

	gs->text_engine = TTF_CreateRendererTextEngine(appstate->renderer);
	if (!gs->text_engine) {
		return false;
	}

	char *font_path = NULL;
	if (!get_path_to_resource(&font_path, "fonts",
				  "OpenSans-Regular.ttf")) {
		return false;
	}
	gs->font = TTF_OpenFont(font_path, DEFAULT_FONT_SIZE);
	SDL_free(font_path);
	if (!gs->font) {
		return false;
	}

	gs->burger_button =
	    button_init_icon(NULL, 0.F, 0.F, toggle_app_menu, appstate);
	if (!gs->burger_button) {
		return false;
	}

	gs->new_game_button = button_init(
	    gs, "Comença de nou", restart_current_game_callback, appstate);
	if (!gs->new_game_button) {
		return false;
	}

	gs->main_menu_button =
	    button_init(gs, "Menú principal", go_to_main_menu, appstate);
	if (!gs->main_menu_button) {
		return false;
	}

	gs->quit_button = button_init(gs, "Surt", quit_app, appstate);
	if (!gs->quit_button) {
		return false;
	}

	if (!fit_app_menu(appstate)) {
		return false;
	}
	position_app_menu(appstate);

	return true;
}

bool gui_event(struct app_state *appstate, SDL_Event *event) {
	if (button_event(appstate->gui->burger_button, event)) {
		return true;
	}

	if (appstate->gui->app_menu_open) {
		struct button *visible[MAX_APP_MENU_OPTIONS];
		unsigned count;
		get_visible_app_menu_buttons(appstate, visible, &count);
		for (unsigned i = 0; i < count; ++i) {
			if (button_event(visible[i], event)) {
				return true;
			}
		}

		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			SDL_FPoint p = {event->button.x, event->button.y};
			bool on_menu =
			    button_is_hovered(appstate->gui->burger_button, p);
			for (unsigned i = 0; i < count; ++i) {
				on_menu |= button_is_hovered(visible[i], p);
			}
			if (!on_menu) {
				appstate->gui->app_menu_open = false;
				return true;
			}
		}
	}
	return false;
}

void gui_iterate(struct app_state *appstate) {
	fit_app_menu(appstate);
	position_app_menu(appstate);
	struct button *burger = appstate->gui->burger_button;
	button_draw(appstate, burger,
		    button_is_pressing(burger) ? icon_pressed_color
					       : icon_color);
	if (appstate->gui->app_menu_open) {
		struct button *visible[MAX_APP_MENU_OPTIONS];
		unsigned count;
		get_visible_app_menu_buttons(appstate, visible, &count);
		for (unsigned i = 0; i < count; ++i) {
			button_draw(appstate, visible[i],
				    button_is_pressing(visible[i])
					? menu_button_pressed_color
					: menu_button_color);
		}
	}
}

void gui_quit(struct gui_state *guistate) {
	if (guistate->burger_button) {
		button_quit(guistate->burger_button);
	}
	if (guistate->new_game_button) {
		button_quit(guistate->new_game_button);
	}
	if (guistate->main_menu_button) {
		button_quit(guistate->main_menu_button);
	}
	if (guistate->quit_button) {
		button_quit(guistate->quit_button);
	}
	if (guistate->font) {
		TTF_CloseFont(guistate->font);
	}
	if (guistate->text_engine) {
		TTF_DestroyRendererTextEngine(guistate->text_engine);
	}
	SDL_free(guistate);
}

struct label *label_init(struct gui_state *guistate, const char *text) {
	struct label *label = SDL_calloc(1, sizeof(*label));
	if (!label) {
		return NULL;
	}

	label->font = TTF_CopyFont(guistate->font);
	if (!label->font) {
		SDL_free(label);
		return NULL;
	}
	label->text = TTF_CreateText(guistate->text_engine, label->font, text,
				     SDL_strlen(text));
	if (!label->text) {
		TTF_CloseFont(label->font);
		SDL_free(label);
		return NULL;
	}
	TTF_SetTextColor(label->text, 0, 0, 0, SDL_ALPHA_OPAQUE);
	return label;
}

static SDL_FRect label_text_rect(const struct label *label) {
	int w;
	int h;
	TTF_GetTextSize(label->text, &w, &h);
	return (SDL_FRect){
	    SDL_roundf(label->rect.x + ((label->rect.w - (float)w) / 2.F)),
	    SDL_roundf(label->rect.y + ((label->rect.h - (float)h) / 2.F)),
	    (float)w,
	    (float)h,
	};
}

static void draw_label_background(struct app_state *appstate,
				  struct label *label, SDL_Color color) {
	SDL_Point pixels =
	    size_in_pixels(appstate, label->rect.w, label->rect.h);
	if (!label->background || pixels.x != label->background_size.x ||
	    pixels.y != label->background_size.y) {
		if (label->background) {
			SDL_DestroyTexture(label->background);
		}
		label->background = create_label_background(
		    appstate, label->rect.w, label->rect.h);
		label->background_size = pixels;
	}

	if (label->background) {
		SDL_SetTextureColorMod(label->background, color.r, color.g,
				       color.b);
		SDL_SetTextureAlphaMod(label->background, color.a);
		SDL_RenderTexture(appstate->renderer, label->background, NULL,
				  &label->rect);
	} else {
		SDL_SetRenderDrawColor(appstate->renderer, color.r, color.g,
				       color.b, color.a);
		SDL_RenderFillRect(appstate->renderer, &label->rect);
	}
}

void label_draw(struct app_state *appstate, struct label *label,
		const SDL_Color *background_color) {
	if (background_color) {
		draw_label_background(appstate, label, *background_color);
	}

	const SDL_FRect text_rect = label_text_rect(label);
	TTF_DrawRendererText(label->text, text_rect.x, text_rect.y);
}

void label_quit(struct label *label) {
	if (label->background) {
		SDL_DestroyTexture(label->background);
	}
	if (label->text) {
		TTF_DestroyText(label->text);
	}
	if (label->font) {
		TTF_CloseFont(label->font);
	}
	SDL_free(label);
}

#define LABEL_FIT_MAX_ROUNDS 8

void label_fit_width(struct label *label, float target_width) {
	SDL_FPoint size = padded_text_size(label);
	float font_size = TTF_GetFontSize(label->font);
	long previous_width = 0;
	for (int round = 0;
	     round < LABEL_FIT_MAX_ROUNDS && target_width > 0 && size.x > 0 &&
	     SDL_fabsf(size.x - target_width) > 1.F;
	     ++round) {
		font_size *= target_width / size.x;
		label_set_font_size(label, font_size);
		size = padded_text_size(label);
		const long width = SDL_lroundf(size.x);
		if (width == previous_width) {
			break;
		}
		previous_width = width;
	}
	label_set_font_size(label, font_size);
}

void label_group_fit_width(struct label *labels[], unsigned count,
			   float target_width) {
	float font_size = 0.F;
	for (unsigned i = 0; i < count; ++i) {
		label_fit_width(labels[i], target_width);
		const float size = TTF_GetFontSize(labels[i]->font);
		font_size = i == 0 ? size : fminf(font_size, size);
	}
	SDL_FPoint largest = {0.F, 0.F};
	for (unsigned i = 0; i < count; ++i) {
		label_set_font_size(labels[i], font_size);
		const SDL_FPoint size = label_get_size(labels[i]);
		largest.x = SDL_max(largest.x, size.x);
		largest.y = SDL_max(largest.y, size.y);
	}
	for (unsigned i = 0; i < count; ++i) {
		label_set_size(labels[i], largest.x, largest.y);
	}
}

void label_set_position(struct label *label, float x, float y) {
	label->rect.x = x;
	label->rect.y = y;
}

SDL_FPoint label_get_size(const struct label *label) {
	return (SDL_FPoint){label->rect.w, label->rect.h};
}

#if SDL_ASSERT_LEVEL >= 2
static bool is_whole_number(const char *text) {
	for (const char *c = text; *c; ++c) {
		if (!SDL_isdigit((unsigned char)*c)) {
			return false;
		}
	}
	return true;
}
#endif

struct input *input_init(struct gui_state *guistate, const char *text) {
#if SDL_ASSERT_LEVEL >= 2
	SDL_assert(is_whole_number(text) &&
		   "the text of an input must contain only digits");
#endif
	struct input *input = SDL_calloc(1, sizeof(*input));
	if (!input) {
		return NULL;
	}
	input->label = label_init(guistate, text);
	if (!input->label) {
		SDL_free(input);
		return NULL;
	}
	return input;
}

static SDL_FRect input_cursor_rect(const struct input *input) {
	const struct label *label = input->label;
	const SDL_FRect text_rect = label_text_rect(label);
	const float line_height = (float)TTF_GetFontHeight(label->font);
	return (SDL_FRect){
	    text_rect.x + text_rect.w,
	    SDL_roundf(label->rect.y + ((label->rect.h - line_height) / 2.F)),
	    INPUT_CURSOR_WIDTH,
	    line_height,
	};
}

static void input_update_text_input_area(struct app_state *appstate,
					 const struct input *input) {
	const SDL_FRect *rect = &input->label->rect;
	const SDL_Rect area = {
	    (int)SDL_floorf(rect->x),
	    (int)SDL_floorf(rect->y),
	    (int)SDL_ceilf(rect->w),
	    (int)SDL_ceilf(rect->h),
	};
	const SDL_FRect cursor = input_cursor_rect(input);
	SDL_SetTextInputArea(appstate->window, &area,
			     (int)SDL_roundf(cursor.x) - area.x);
}

bool input_has_focus(const struct app_state *appstate,
		     const struct input *input) {
	return appstate->gui->focused_input == input;
}

static void input_focus(struct app_state *appstate, struct input *input) {
	if (appstate->gui->focused_input) {
		input_unfocus(appstate, appstate->gui->focused_input);
	}
	appstate->gui->focused_input = input;
	input_update_text_input_area(appstate, input);
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
			      SDL_TEXTINPUT_TYPE_NUMBER);
	SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER,
			      SDL_CAPITALIZE_NONE);
	SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN,
			       false);
	SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN,
			       false);
	SDL_StartTextInputWithProperties(appstate->window, props);
	SDL_DestroyProperties(props);
}

void input_unfocus(struct app_state *appstate, struct input *input) {
	if (!input_has_focus(appstate, input)) {
		return;
	}
	appstate->gui->focused_input = NULL;
	SDL_StopTextInput(appstate->window);
	SDL_SetTextInputArea(appstate->window, NULL, 0);
}

static void input_append_text(struct app_state *appstate, struct input *input,
			      const char *text) {
	for (const char *c = text; *c; ++c) {
		if (SDL_isdigit((unsigned char)*c)) {
			TTF_AppendTextString(input->label->text, c, 1);
		}
	}
	input_update_text_input_area(appstate, input);
}

#define UTF8_CONTINUATION_MASK 0xC0
#define UTF8_CONTINUATION_BITS 0x80

static void input_delete_last_character(struct app_state *appstate,
					struct input *input) {
	const char *text = input_get_text(input);
	int start = (int)SDL_strlen(text);
	if (start == 0) {
		return;
	}
	do {
		--start;
	} while (start > 0 && (text[start] & UTF8_CONTINUATION_MASK) ==
				  UTF8_CONTINUATION_BITS);
	TTF_DeleteTextString(input->label->text, start, -1);
	input_update_text_input_area(appstate, input);
}

static void input_key_down(struct app_state *appstate, struct input *input,
			   const SDL_KeyboardEvent *key) {
	switch (key->key) {
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
	case SDLK_ESCAPE:
		input_unfocus(appstate, input);
		break;
	case SDLK_BACKSPACE:
		input_delete_last_character(appstate, input);
		break;
	default:
		break;
	}
}

static bool input_mouse_down(struct app_state *appstate, struct input *input,
			     const SDL_MouseButtonEvent *button) {
	const SDL_FPoint p = {button->x, button->y};
	if (!SDL_PointInRectFloat(&p, &input->label->rect)) {
		input_unfocus(appstate, input);
		return false;
	}
	if (button->button == SDL_BUTTON_LEFT &&
	    !input_has_focus(appstate, input)) {
		input_focus(appstate, input);
	}
	return true;
}

bool input_event(struct app_state *appstate, struct input *input,
		 const SDL_Event *event) {
	const bool focused = input_has_focus(appstate, input);
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		return input_mouse_down(appstate, input, &event->button);
	case SDL_EVENT_TEXT_INPUT:
		if (focused) {
			input_append_text(appstate, input, event->text.text);
		}
		return focused;
	case SDL_EVENT_KEY_DOWN:
		if (focused) {
			input_key_down(appstate, input, &event->key);
		}
		return focused;
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_TEXT_EDITING:
		return focused;
	default:
		return false;
	}
}

static void draw_input_cursor(struct app_state *appstate,
			      const struct input *input) {
	const SDL_FRect cursor = input_cursor_rect(input);
	SDL_SetRenderDrawColor(appstate->renderer, input_cursor_color.r,
			       input_cursor_color.g, input_cursor_color.b,
			       input_cursor_color.a);
	SDL_RenderFillRect(appstate->renderer, &cursor);
}

void input_draw(struct app_state *appstate, struct input *input,
		SDL_Color background_color) {
	label_draw(appstate, input->label, &background_color);
	if (input_has_focus(appstate, input)) {
		draw_input_cursor(appstate, input);
	}
}

void input_quit(struct app_state *appstate, struct input *input) {
	input_unfocus(appstate, input);
	label_quit(input->label);
	SDL_free(input);
}

void input_set_position(struct app_state *appstate, struct input *input,
			float x, float y) {
	label_set_position(input->label, x, y);
	if (input_has_focus(appstate, input)) {
		input_update_text_input_area(appstate, input);
	}
}

void input_set_size(struct app_state *appstate, struct input *input, float w,
		    float h) {
	label_set_size(input->label, w, h);
	if (input_has_focus(appstate, input)) {
		input_update_text_input_area(appstate, input);
	}
}

void input_set_font_size(struct app_state *appstate, struct input *input,
			 float font_size) {
	TTF_SetFontSize(input->label->font, font_size);
	if (input_has_focus(appstate, input)) {
		input_update_text_input_area(appstate, input);
	}
}

const char *input_get_text(const struct input *input) {
	const char *text = input->label->text->text;
	return text ? text : "";
}

static struct button *button_alloc(button_callback on_click, void *userdata) {
	struct button *button = SDL_calloc(1, sizeof(*button));
	if (!button) {
		return NULL;
	}
	button->on_click = on_click;
	button->userdata = userdata;
	return button;
}

struct button *button_init(struct gui_state *guistate, const char *text,
			   button_callback on_click, void *userdata) {
	struct button *button = button_alloc(on_click, userdata);
	if (!button) {
		return NULL;
	}
	button->label = label_init(guistate, text);
	if (!button->label) {
		SDL_free(button);
		return NULL;
	}
	return button;
}

struct button *button_init_icon(SDL_Texture *icon, float w, float h,
				button_callback on_click, void *userdata) {
	struct button *button = button_alloc(on_click, userdata);
	if (!button) {
		return NULL;
	}
	button->icon = icon;
	button->icon_rect.w = w;
	button->icon_rect.h = h;
	return button;
}

bool button_event(struct button *button, const SDL_Event *event) {
	if (button->disabled) {
		return false;
	}
	const SDL_FRect *rect = button_const_rect(button);
	SDL_FPoint p;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button != SDL_BUTTON_LEFT) {
			return false;
		}
		p.x = event->button.x;
		p.y = event->button.y;
		if (!SDL_PointInRectFloat(&p, rect)) {
			return false;
		}
		button->pressing = true;
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button != SDL_BUTTON_LEFT ||
		    !button->pressing) {
			return false;
		}
		button->pressing = false;
		p.x = event->button.x;
		p.y = event->button.y;
		if (SDL_PointInRectFloat(&p, rect) && button->on_click) {
			button->on_click(button->userdata);
		}
		return true;
	default:
		return false;
	}
}

void button_draw(struct app_state *appstate, struct button *button,
		 SDL_Color color) {
	if (button->label) {
		label_draw(appstate, button->label, &color);
		return;
	}
	SDL_SetTextureColorMod(button->icon, color.r, color.g, color.b);
	SDL_SetTextureAlphaMod(button->icon, color.a);
	SDL_RenderTexture(appstate->renderer, button->icon, NULL,
			  &button->icon_rect);
}

static const SDL_Color disabled_button_color = {170, 170, 170, 200};

void button_draw_choice(struct app_state *appstate, struct button *button) {
	SDL_Color color = button_is_pressing(button)
			      ? green_button_pressed_color
			      : green_button_color;
	if (!button_is_enabled(button)) {
		color = disabled_button_color;
	}
	button_draw(appstate, button, color);
}

void button_quit(struct button *button) {
	if (button->label) {
		label_quit(button->label);
	}
	if (button->icon) {
		SDL_DestroyTexture(button->icon);
	}
	SDL_free(button);
}

void button_fit_width(struct button *button, float target_width) {
	if (button->label) {
		label_fit_width(button->label, target_width);
	}
}

static struct label *text_button_label(const struct button *button) {
	SDL_assert(button->label && "Only text buttons can be group fitted.");
	return button->label;
}

void button_group_fit_width(struct button *buttons[], unsigned count,
			    float target_width) {
	struct label **labels = SDL_stack_alloc(struct label *, count);
	if (!labels) {
		return;
	}
	for (unsigned i = 0; i < count; ++i) {
		labels[i] = text_button_label(buttons[i]);
	}
	label_group_fit_width(labels, count, target_width);
	SDL_stack_free(labels);
}

void button_set_position(struct button *button, float x, float y) {
	SDL_FRect *rect = button_rect(button);
	rect->x = x;
	rect->y = y;
}

struct label *button_get_label(struct button *button) { return button->label; }

float button_get_font_size(const struct button *button) {
	SDL_assert(button->label && "Only text buttons have a font size.");
	return TTF_GetFontSize(button->label->font);
}

SDL_FPoint button_get_size(const struct button *button) {
	const SDL_FRect *rect = button_const_rect(button);
	return (SDL_FPoint){rect->w, rect->h};
}

bool button_is_hovered(const struct button *button, SDL_FPoint point) {
	return SDL_PointInRectFloat(&point, button_const_rect(button));
}

bool button_is_pressing(const struct button *button) {
	return button->pressing;
}

void button_set_enabled(struct button *button, bool enabled) {
	button->disabled = !enabled;
	if (!enabled) {
		button->pressing = false;
	}
}

bool button_is_enabled(const struct button *button) {
	return !button->disabled;
}
