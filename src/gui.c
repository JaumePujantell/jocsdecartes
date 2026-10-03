#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "app.h"
#include "gui.h"

struct button {
	SDL_FRect rect;
	/* White rounded rectangle the size of rect in screen pixels, or NULL
	 * until the button is first drawn. */
	SDL_Texture *background;
	SDL_Point background_size;
	TTF_Text *text;
	TTF_Font *font;
	float font_size;
	bool pressing;
	button_callback on_click;
	void *userdata;
};

struct gui_state {
	TTF_TextEngine *text_engine;
	TTF_Font *font;
	float font_size;
	struct button *burger_button;
	SDL_Texture *menu_icon;
	struct button *new_game_button;
	struct button *main_menu_button;
	struct button *quit_button;
	bool app_menu_open;
};

#define MAX_APP_MENU_OPTIONS 3

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
	float margin = 10.f;
	struct gui_state *gs = as->gui;
	SDL_FPoint burger_size = button_size(gs->burger_button);
	button_set_position(gs->burger_button,
			    as->window_size.x - burger_size.x - margin, margin);

	struct button *visible[MAX_APP_MENU_OPTIONS];
	unsigned count;
	get_visible_app_menu_buttons(as, visible, &count);

	float y = margin + burger_size.y + margin;
	for (unsigned i = 0; i < count; ++i) {
		SDL_FPoint size = button_size(visible[i]);
		button_set_position(visible[i],
				    as->window_size.x - size.x - margin, y);
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
	if (density <= 0.f)
		density = 1.f;
	return (SDL_Point){(int)SDL_ceilf(w * density),
			   (int)SDL_ceilf(h * density)};
}

/* The SVG is rasterized at the size it is drawn in real screen pixels, so
 * its edges stay sharp on high density displays. The stream is closed. */
static SDL_Texture *svg_texture_from_stream(struct app_state *as,
					    SDL_IOStream *stream, float w,
					    float h) {
	SDL_Point pixels = size_in_pixels(as, w, h);
	SDL_Surface *surface = IMG_LoadSizedSVG_IO(stream, pixels.x, pixels.y);
	SDL_CloseIO(stream);
	if (!surface)
		return NULL;
	SDL_Texture *texture =
	    SDL_CreateTextureFromSurface(as->renderer, surface);
	SDL_DestroySurface(surface);
	if (texture)
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
	return texture;
}

static SDL_Texture *load_svg_texture(struct app_state *as, const char *name,
				     float w, float h) {
	char *svg_path = NULL;
	if (!get_path_to_resource(&svg_path, "icons", name))
		return NULL;
	SDL_IOStream *stream = SDL_IOFromFile(svg_path, "rb");
	SDL_free(svg_path);
	if (!stream)
		return NULL;
	return svg_texture_from_stream(as, stream, w, h);
}

/* The shape is white with a slight top to bottom shade, so tinting it with
 * the fill color gives that color with a little depth. The thin outline is
 * dark and keeps its color under any tint. */
static SDL_Texture *create_button_background(struct app_state *as, float w,
					     float h) {
	float radius = SDL_min(h * 0.3f, w / 2.f);
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
	    w, h, w - 1.f, h - 1.f, radius, radius);
	if (!svg)
		return NULL;
	SDL_IOStream *stream = SDL_IOFromConstMem(svg, SDL_strlen(svg));
	SDL_Texture *texture =
	    stream ? svg_texture_from_stream(as, stream, w, h) : NULL;
	SDL_free(svg);
	return texture;
}

bool gui_init(struct app_state *appstate) {
	struct gui_state *gs = SDL_malloc(sizeof(*gs));
	if (!gs)
		return false;
	SDL_memset(gs, 0, sizeof(*gs));
	appstate->gui = gs;

	gs->text_engine = TTF_CreateRendererTextEngine(appstate->renderer);
	if (!gs->text_engine)
		return false;

	char *font_path = NULL;
	if (!get_path_to_resource(&font_path, "fonts", "OpenSans-Regular.ttf"))
		return false;
	gs->font_size = 14.f;
	gs->font = TTF_OpenFont(font_path, gs->font_size);
	SDL_free(font_path);
	if (!gs->font)
		return false;

	gs->burger_button = button_init(gs, "", toggle_app_menu, appstate);
	if (!gs->burger_button)
		return false;
	button_set_size(gs->burger_button, 40.f, 40.f);
	gs->menu_icon = load_svg_texture(appstate, "menu.svg", 40.f, 40.f);
	if (!gs->menu_icon)
		return false;

	gs->new_game_button = button_init(
	    gs, u8"Comença de nou", restart_current_game_callback, appstate);
	if (!gs->new_game_button)
		return false;

	gs->main_menu_button =
	    button_init(gs, u8"Menú principal", go_to_main_menu, appstate);
	if (!gs->main_menu_button)
		return false;

	gs->quit_button = button_init(gs, "Surt", quit_app, appstate);
	if (!gs->quit_button)
		return false;

	/* The longest label sets the font size, and every menu option shares
	 * it and the widest width, so the options line up as one column. */
	button_fit_width(gs->new_game_button, 140.f);
	float font_size = button_font_size(gs->new_game_button);
	button_set_font_size(gs->main_menu_button, font_size);
	button_set_font_size(gs->quit_button, font_size);
	struct button *options[] = {gs->new_game_button, gs->main_menu_button,
				    gs->quit_button};
	SDL_FPoint widest = {0.f, 0.f};
	for (unsigned i = 0; i < SDL_arraysize(options); ++i) {
		SDL_FPoint size = button_size(options[i]);
		widest.x = SDL_max(widest.x, size.x);
		widest.y = SDL_max(widest.y, size.y);
	}
	for (unsigned i = 0; i < SDL_arraysize(options); ++i)
		button_set_size(options[i], widest.x, widest.y);

	position_app_menu(appstate);

	return true;
}

bool gui_event(struct app_state *appstate, SDL_Event *event) {
	if (button_event(appstate->gui->burger_button, event))
		return true;

	if (appstate->gui->app_menu_open) {
		struct button *visible[MAX_APP_MENU_OPTIONS];
		unsigned count;
		get_visible_app_menu_buttons(appstate, visible, &count);
		for (unsigned i = 0; i < count; ++i)
			if (button_event(visible[i], event))
				return true;

		/* A press outside the menu closes it and still reaches the
		 * screen below, so that one click both closes the menu and
		 * acts on the game. */
		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			SDL_FPoint p = {event->button.x, event->button.y};
			bool on_menu =
			    button_is_hovered(appstate->gui->burger_button, p);
			for (unsigned i = 0; i < count; ++i)
				on_menu |= button_is_hovered(visible[i], p);
			if (!on_menu)
				appstate->gui->app_menu_open = false;
		}
	}
	return false;
}

static void draw_icon_button(struct app_state *appstate,
			     const struct button *button, SDL_Texture *icon) {
	/* A pressed button is drawn darker to show that the press was felt. */
	Uint8 shade = button_is_pressing(button) ? 170 : 255;
	SDL_SetTextureColorMod(icon, shade, shade, shade);
	SDL_RenderTexture(appstate->renderer, icon, NULL, &button->rect);
}

void gui_iterate(struct app_state *appstate) {
	/* The window size and the visible options can change between frames,
	 * so the layout is rebuilt every frame. */
	position_app_menu(appstate);
	draw_icon_button(appstate, appstate->gui->burger_button,
			 appstate->gui->menu_icon);
	if (appstate->gui->app_menu_open) {
		struct button *visible[MAX_APP_MENU_OPTIONS];
		unsigned count;
		get_visible_app_menu_buttons(appstate, visible, &count);
		for (unsigned i = 0; i < count; ++i)
			button_draw(
			    appstate, visible[i],
			    button_is_pressing(visible[i])
				? (SDL_Color){190, 190, 190, SDL_ALPHA_OPAQUE}
				: (SDL_Color){240, 240, 240, SDL_ALPHA_OPAQUE});
	}
}

void gui_quit(struct gui_state *guistate) {
	if (guistate->burger_button)
		button_quit(guistate->burger_button);
	if (guistate->menu_icon)
		SDL_DestroyTexture(guistate->menu_icon);
	if (guistate->new_game_button)
		button_quit(guistate->new_game_button);
	if (guistate->main_menu_button)
		button_quit(guistate->main_menu_button);
	if (guistate->quit_button)
		button_quit(guistate->quit_button);
	if (guistate->font)
		TTF_CloseFont(guistate->font);
	if (guistate->text_engine)
		TTF_DestroyRendererTextEngine(guistate->text_engine);
	SDL_free(guistate);
}

struct button *button_init(struct gui_state *guistate, const char *label,
			   button_callback on_click, void *userdata) {
	struct button *button = SDL_malloc(sizeof(*button));
	if (!button)
		return NULL;
	SDL_memset(button, 0, sizeof(*button));

	button->font = TTF_CopyFont(guistate->font);
	if (!button->font) {
		SDL_free(button);
		return NULL;
	}
	button->font_size = TTF_GetFontSize(button->font);
	button->text = TTF_CreateText(guistate->text_engine, button->font,
				      label, SDL_strlen(label));
	if (!button->text) {
		TTF_CloseFont(button->font);
		SDL_free(button);
		return NULL;
	}
	TTF_SetTextColor(button->text, 0, 0, 0, SDL_ALPHA_OPAQUE);
	button->pressing = false;
	button->on_click = on_click;
	button->userdata = userdata;
	return button;
}

/* The padding around the label is a fraction of the text line height, so
 * it keeps the same look at every font size. */
#define BUTTON_PADDING_X 0.6f
#define BUTTON_PADDING_Y 0.2f

static SDL_FPoint padded_text_size(const struct button *button) {
	int w, h;
	TTF_GetTextSize(button->text, &w, &h);
	return (SDL_FPoint){w + 2.f * BUTTON_PADDING_X * h,
			    h + 2.f * BUTTON_PADDING_Y * h};
}

#define BUTTON_MIN_FONT_SIZE 4.f

void button_set_font_size(struct button *button, float font_size) {
	button->font_size = SDL_max(font_size, BUTTON_MIN_FONT_SIZE);
	TTF_SetFontSize(button->font, button->font_size);
	SDL_FPoint size = padded_text_size(button);
	button->rect.w = SDL_roundf(size.x);
	button->rect.h = SDL_roundf(size.y);
}

/* The padded width grows almost in proportion to the font size, so the
 * first round lands close and the rest only correct rounding. */
#define BUTTON_FIT_MAX_ROUNDS 8

void button_fit_width(struct button *button, float target_width) {
	SDL_FPoint size = padded_text_size(button);
	float old_w = 0.f;
	for (int round = 0;
	     round < BUTTON_FIT_MAX_ROUNDS && target_width > 0 && size.x > 0 &&
	     SDL_fabsf(size.x - target_width) > 1.f;
	     ++round) {
		button_set_font_size(button,
				     button->font_size * target_width / size.x);
		size = padded_text_size(button);
		if (size.x == old_w)
			break;
		old_w = size.x;
	}
	button_set_font_size(button, button->font_size);
}

float button_font_size(const struct button *button) {
	return button->font_size;
}

void button_set_position(struct button *button, float x, float y) {
	button->rect.x = x;
	button->rect.y = y;
}

void button_set_size(struct button *button, float w, float h) {
	button->rect.w = w;
	button->rect.h = h;
}

SDL_FPoint button_size(const struct button *button) {
	return (SDL_FPoint){button->rect.w, button->rect.h};
}

void button_quit(struct button *button) {
	if (button->background)
		SDL_DestroyTexture(button->background);
	if (button->text)
		TTF_DestroyText(button->text);
	if (button->font)
		TTF_CloseFont(button->font);
	SDL_free(button);
}

bool button_event(struct button *button, const SDL_Event *event) {
	SDL_FPoint p;
	switch (event->type) {
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (event->button.button != SDL_BUTTON_LEFT)
			return false;
		p.x = event->button.x;
		p.y = event->button.y;
		if (!SDL_PointInRectFloat(&p, &button->rect))
			return false;
		button->pressing = true;
		return true;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button != SDL_BUTTON_LEFT ||
		    !button->pressing)
			return false;
		button->pressing = false;
		p.x = event->button.x;
		p.y = event->button.y;
		if (SDL_PointInRectFloat(&p, &button->rect) && button->on_click)
			button->on_click(button->userdata);
		return true;
	default:
		return false;
	}
}

bool button_is_hovered(const struct button *button, SDL_FPoint point) {
	return SDL_PointInRectFloat(&point, &button->rect);
}

bool button_is_pressing(const struct button *button) {
	return button->pressing;
}

void button_draw(struct app_state *appstate, struct button *button,
		 SDL_Color fill_color) {
	/* The background is rebuilt when the button or the display density
	 * changes size, so its corners stay smooth. */
	SDL_Point pixels =
	    size_in_pixels(appstate, button->rect.w, button->rect.h);
	if (!button->background || pixels.x != button->background_size.x ||
	    pixels.y != button->background_size.y) {
		if (button->background)
			SDL_DestroyTexture(button->background);
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

	int text_w, text_h;
	TTF_GetTextSize(button->text, &text_w, &text_h);
	TTF_DrawRendererText(
	    button->text,
	    SDL_roundf(button->rect.x + (button->rect.w - text_w) / 2.f),
	    SDL_roundf(button->rect.y + (button->rect.h - text_h) / 2.f));
}
