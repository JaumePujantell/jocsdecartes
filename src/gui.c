#include "gui.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <math.h>

#include "app.h"

struct button {
	SDL_FRect rect;
	SDL_Texture *background;
	SDL_Point background_size;
	TTF_Text *text;
	TTF_Font *font;
	bool pressing;
	button_callback on_click;
	void *userdata;
};

struct gui_state {
	TTF_TextEngine *text_engine;
	TTF_Font *font;
	struct button *burger_button;
	SDL_Texture *menu_icon;
	struct button *new_game_button;
	struct button *main_menu_button;
	struct button *quit_button;
	bool app_menu_open;
};

#define MAX_APP_MENU_OPTIONS 3
#define APP_MENU_MARGIN 10.F
#define APP_MENU_BUTTON_WIDTH 140.F
#define MENU_ICON_SIZE 40.F
#define DEFAULT_FONT_SIZE 14.F
/* Corner radius as a fraction of the button height. */
#define BUTTON_CORNER_RADIUS 0.3F

const SDL_Color green_button_color = {120, 205, 120, SDL_ALPHA_OPAQUE};
const SDL_Color green_button_pressed_color = {90, 170, 90, SDL_ALPHA_OPAQUE};

static const SDL_Color menu_button_color = {240, 240, 240, SDL_ALPHA_OPAQUE};
static const SDL_Color menu_button_pressed_color = {190, 190, 190,
						    SDL_ALPHA_OPAQUE};

static const Uint8 icon_shade = 255;
static const Uint8 icon_pressed_shade = 170;

static void
get_visible_app_menu_buttons(struct app_state *as,
			     struct button *buttons[MAX_APP_MENU_OPTIONS],
			     unsigned *count) {
	unsigned n = 0;
	struct gui_state *gs = as->gui;
	if (as->current_game != NO_GAME) {
		buttons[n++] = gs->new_game_button;
		buttons[n++] = gs->main_menu_button;
	}
	buttons[n++] = gs->quit_button;
	*count = n;
}

static void position_app_menu(struct app_state *as) {
	const float margin = APP_MENU_MARGIN;
	const float window_width = (float)as->window_size.x;
	struct gui_state *gs = as->gui;
	SDL_FPoint burger_size = button_get_size(gs->burger_button);
	button_set_position(gs->burger_button,
			    window_width - burger_size.x - margin, margin);

	struct button *visible[MAX_APP_MENU_OPTIONS];
	unsigned count;
	get_visible_app_menu_buttons(as, visible, &count);

	float y = margin + burger_size.y + margin;
	for (unsigned i = 0; i < count; ++i) {
		SDL_FPoint size = button_get_size(visible[i]);
		button_set_position(visible[i], window_width - size.x - margin,
				    y);
		y += size.y + margin;
	}
}

static void toggle_app_menu(void *userdata) {
	struct app_state *as = (struct app_state *)userdata;
	as->gui->app_menu_open = !as->gui->app_menu_open;
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
	float density = SDL_GetWindowPixelDensity(as->window);
	if (density <= 0.F) {
		density = 1.F;
	}
	return (SDL_Point){(int)SDL_ceilf(w * density),
			   (int)SDL_ceilf(h * density)};
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

static SDL_Texture *create_button_background(struct app_state *as, float w,
					     float h) {
	float radius = fminf(h * BUTTON_CORNER_RADIUS, w / 2.F);
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
	    w, h, w - 1.F, h - 1.F, radius, radius);
	if (!svg) {
		return NULL;
	}
	SDL_IOStream *stream = SDL_IOFromConstMem(svg, SDL_strlen(svg));
	SDL_Texture *texture =
	    stream ? svg_texture_from_stream(as, stream, w, h) : NULL;
	SDL_free(svg);
	return texture;
}

#define BUTTON_PADDING_X 0.6F
#define BUTTON_PADDING_Y 0.2F

static SDL_FPoint padded_text_size(const struct button *button) {
	int w;
	int h;
	TTF_GetTextSize(button->text, &w, &h);
	const float text_w = (float)w;
	const float text_h = (float)h;
	return (SDL_FPoint){text_w + (2.F * BUTTON_PADDING_X * text_h),
			    text_h + (2.F * BUTTON_PADDING_Y * text_h)};
}

#define BUTTON_MIN_FONT_SIZE 4.F

static void button_set_font_size(struct button *button, float font_size) {
	float clamped_size = fmaxf(font_size, BUTTON_MIN_FONT_SIZE);
	TTF_SetFontSize(button->font, clamped_size);
	SDL_FPoint size = padded_text_size(button);
	button->rect.w = SDL_roundf(size.x);
	button->rect.h = SDL_roundf(size.y);
}

static void button_set_size(struct button *button, float w, float h) {
	button->rect.w = w;
	button->rect.h = h;
}

bool gui_init(struct app_state *appstate) {
	struct gui_state *gs = SDL_malloc(sizeof(*gs));
	if (!gs) {
		return false;
	}
	SDL_memset(gs, 0, sizeof(*gs));
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

	gs->burger_button = button_init(gs, NULL, toggle_app_menu, appstate);
	if (!gs->burger_button) {
		return false;
	}
	button_set_size(gs->burger_button, MENU_ICON_SIZE, MENU_ICON_SIZE);
	gs->menu_icon = load_svg_texture(appstate, "menu.svg", MENU_ICON_SIZE,
					 MENU_ICON_SIZE);
	if (!gs->menu_icon) {
		return false;
	}

	gs->new_game_button = button_init(
	    gs, u8"Comença de nou", restart_current_game_callback, appstate);
	if (!gs->new_game_button) {
		return false;
	}

	gs->main_menu_button =
	    button_init(gs, u8"Menú principal", go_to_main_menu, appstate);
	if (!gs->main_menu_button) {
		return false;
	}

	gs->quit_button = button_init(gs, "Surt", quit_app, appstate);
	if (!gs->quit_button) {
		return false;
	}

	button_fit_width(gs->new_game_button, APP_MENU_BUTTON_WIDTH);
	float font_size = TTF_GetFontSize(gs->new_game_button->font);
	button_set_font_size(gs->main_menu_button, font_size);
	button_set_font_size(gs->quit_button, font_size);
	struct button *options[] = {gs->new_game_button, gs->main_menu_button,
				    gs->quit_button};
	SDL_FPoint widest = {0.F, 0.F};
	for (unsigned i = 0; i < SDL_arraysize(options); ++i) {
		SDL_FPoint size = button_get_size(options[i]);
		widest.x = SDL_max(widest.x, size.x);
		widest.y = SDL_max(widest.y, size.y);
	}
	for (unsigned i = 0; i < SDL_arraysize(options); ++i) {
		button_set_size(options[i], widest.x, widest.y);
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

static void draw_icon_button(struct app_state *appstate,
			     const struct button *button, SDL_Texture *icon) {
	Uint8 shade =
	    button_is_pressing(button) ? icon_pressed_shade : icon_shade;
	SDL_SetTextureColorMod(icon, shade, shade, shade);
	SDL_RenderTexture(appstate->renderer, icon, NULL, &button->rect);
}

void gui_iterate(struct app_state *appstate) {
	position_app_menu(appstate);
	draw_icon_button(appstate, appstate->gui->burger_button,
			 appstate->gui->menu_icon);
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
	if (guistate->menu_icon) {
		SDL_DestroyTexture(guistate->menu_icon);
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

struct button *button_init(struct gui_state *guistate, const char *label,
			   button_callback on_click, void *userdata) {
	struct button *button = SDL_malloc(sizeof(*button));
	if (!button) {
		return NULL;
	}
	SDL_memset(button, 0, sizeof(*button));

	button->pressing = false;
	button->on_click = on_click;
	button->userdata = userdata;

	if (!label) {
		return button;
	}

	button->font = TTF_CopyFont(guistate->font);
	if (!button->font) {
		SDL_free(button);
		return NULL;
	}
	button->text = TTF_CreateText(guistate->text_engine, button->font,
				      label, SDL_strlen(label));
	if (!button->text) {
		TTF_CloseFont(button->font);
		SDL_free(button);
		return NULL;
	}
	TTF_SetTextColor(button->text, 0, 0, 0, SDL_ALPHA_OPAQUE);
	return button;
}

bool button_event(struct button *button, const SDL_Event *event) {
	SDL_FPoint p;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button != SDL_BUTTON_LEFT) {
			return false;
		}
		p.x = event->button.x;
		p.y = event->button.y;
		if (!SDL_PointInRectFloat(&p, &button->rect)) {
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
		if (SDL_PointInRectFloat(&p, &button->rect) &&
		    button->on_click) {
			button->on_click(button->userdata);
		}
		return true;
	default:
		return false;
	}
}

void button_draw(struct app_state *appstate, struct button *button,
		 SDL_Color fill_color) {
	SDL_Point pixels =
	    size_in_pixels(appstate, button->rect.w, button->rect.h);
	if (!button->background || pixels.x != button->background_size.x ||
	    pixels.y != button->background_size.y) {
		if (button->background) {
			SDL_DestroyTexture(button->background);
		}
		button->background = create_button_background(
		    appstate, button->rect.w, button->rect.h);
		button->background_size = pixels;
	}

	if (button->background) {
		SDL_SetTextureColorMod(button->background, fill_color.r,
				       fill_color.g, fill_color.b);
		SDL_SetTextureAlphaMod(button->background, fill_color.a);
		SDL_RenderTexture(appstate->renderer, button->background, NULL,
				  &button->rect);
	} else {
		SDL_SetRenderDrawColor(appstate->renderer, fill_color.r,
				       fill_color.g, fill_color.b,
				       fill_color.a);
		SDL_RenderFillRect(appstate->renderer, &button->rect);
	}

	int text_w;
	int text_h;
	TTF_GetTextSize(button->text, &text_w, &text_h);
	TTF_DrawRendererText(
	    button->text,
	    SDL_roundf(button->rect.x +
		       ((button->rect.w - (float)text_w) / 2.F)),
	    SDL_roundf(button->rect.y +
		       ((button->rect.h - (float)text_h) / 2.F)));
}

void button_quit(struct button *button) {
	if (button->background) {
		SDL_DestroyTexture(button->background);
	}
	if (button->text) {
		TTF_DestroyText(button->text);
	}
	if (button->font) {
		TTF_CloseFont(button->font);
	}
	SDL_free(button);
}

#define BUTTON_FIT_MAX_ROUNDS 8

void button_fit_width(struct button *button, float target_width) {
	SDL_FPoint size = padded_text_size(button);
	float font_size = TTF_GetFontSize(button->font);
	float old_w = 0.F;
	for (int round = 0;
	     round < BUTTON_FIT_MAX_ROUNDS && target_width > 0 && size.x > 0 &&
	     SDL_fabsf(size.x - target_width) > 1.F;
	     ++round) {
		font_size *= target_width / size.x;
		button_set_font_size(button, font_size);
		size = padded_text_size(button);
		if (size.x == old_w) {
			break;
		}
		old_w = size.x;
	}
	button_set_font_size(button, font_size);
}

void button_set_position(struct button *button, float x, float y) {
	button->rect.x = x;
	button->rect.y = y;
}

SDL_FPoint button_get_size(const struct button *button) {
	return (SDL_FPoint){button->rect.w, button->rect.h};
}

bool button_is_hovered(const struct button *button, SDL_FPoint point) {
	return SDL_PointInRectFloat(&point, &button->rect);
}

bool button_is_pressing(const struct button *button) {
	return button->pressing;
}
