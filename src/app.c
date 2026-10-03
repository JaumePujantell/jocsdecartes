#define SDL_MAIN_USE_CALLBACKS 1 /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "app.h"
#include "gui.h"
#include "solitari.h"

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) &&                 \
    !defined(__CYGWIN__)
#define OS_PATH_SEPARATOR "\\"
#else
#define OS_PATH_SEPARATOR "/"
#endif

static const char *app_name = "Card Game";
static const char *game_names[] = {"Solitari"};
#define NUM_GAMES (sizeof(game_names) / sizeof(char *))

struct main_screen_state {
	struct button *game_buttons[NUM_GAMES];
};

bool get_path_to_resource(char **path, const char *kind, const char *name) {
	*path = NULL;
#ifdef __ANDROID__
	SDL_asprintf(path, "%s" OS_PATH_SEPARATOR "%s", kind, name);
#else
	const char *base_path = SDL_GetBasePath();
	if (!base_path)
		return false;
	SDL_asprintf(path, "%s%s" OS_PATH_SEPARATOR "%s", base_path, kind,
		     name);
#endif
	return *path != NULL;
}

/* Load to card_texture a png, without using image lib */
static SDL_Texture *load_texture(SDL_Renderer *renderer, char *file_name) {
	char *png_path = NULL;
	if (!get_path_to_resource(&png_path, "textures", file_name))
		return NULL;
	SDL_Surface *surface = SDL_LoadPNG(png_path);
	SDL_free(png_path);
	if (!surface) {
		return NULL;
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_DestroySurface(surface);
	return texture;
}

static void position_game_buttons(struct app_state *as) {
	float spacing = 10.f;
	float total_height = -spacing;
	struct main_screen_state *ms = as->main_screen;
	for (unsigned i = 0; i < NUM_GAMES; ++i)
		total_height += button_size(ms->game_buttons[i]).y + spacing;

	float y = (as->window_size.y - total_height) / 2.f;
	for (unsigned i = 0; i < NUM_GAMES; ++i) {
		SDL_FPoint size = button_size(ms->game_buttons[i]);
		button_set_position(ms->game_buttons[i],
				    (as->window_size.x - size.x) / 2.f, y);
		y += size.y + spacing;
	}
}

void select_game(struct app_state *as, enum game_id id) {
	switch (id) {
	case NO_GAME:
		position_game_buttons(as);
		break;
	case SOLITARI:
		if (!as->solitari) {
			if (!solitari_init(as)) {
				SDL_Log("Failed to start Solitari: %s",
					SDL_GetError());
				return;
			}
		} else {
			/* The window may have been resized while the game was
			 * not shown. */
			solitari_resize(as);
			solitari_restart(as->solitari);
		}
	};
	as->current_game = id;
}

void restart_current_game(struct app_state *as) {
	switch (as->current_game) {
	case NO_GAME:
		break;
	case SOLITARI:
		solitari_restart(as->solitari);
	};
}

struct game_button_context {
	struct app_state *as;
	enum game_id id;
} button_contexts[NUM_GAMES] = {{NULL, SOLITARI}};

static void select_game_callback(void *userdata) {
	struct game_button_context *ctx =
	    (struct game_button_context *)userdata;
	select_game(ctx->as, ctx->id);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
	srand(time(NULL));
	SDL_SetAppMetadata(app_name, "0.1", NULL);

	struct app_state *as = SDL_malloc(sizeof(*as));
	if (!as)
		return SDL_APP_FAILURE;
	*appstate = as;
	SDL_memset(as, 0, sizeof(*as));
	as->current_game = NO_GAME;

	/* Only affects Android/iOS. */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

	if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init())
		return SDL_APP_FAILURE;

	as->window = SDL_CreateWindow(app_name, 640, 480, SDL_WINDOW_RESIZABLE);
	if (!as->window)
		return SDL_APP_FAILURE;
	/* Below this size the cards and button labels are unreadable. */
	SDL_SetWindowMinimumSize(as->window, 320, 240);
	as->renderer = SDL_CreateRenderer(as->window, NULL);
	if (!as->renderer)
		return SDL_APP_FAILURE;

	as->table_texture = load_texture(as->renderer, "wood_tile.png");
	if (!as->table_texture)
		return SDL_APP_FAILURE;
	as->deck_texture =
	    load_texture(as->renderer, "baraja_espanola_GNU.png");
	if (!as->deck_texture)
		return SDL_APP_FAILURE;

	SDL_SetRenderDrawBlendMode(as->renderer, SDL_BLENDMODE_BLEND);

	SDL_GetTextureSize(as->deck_texture, &as->card_texture_size.x,
			   &as->card_texture_size.y);

	as->card_texture_size.x /= 12;
	as->card_texture_size.y /= 5;

	SDL_GetWindowSize(as->window, &as->window_size.x, &as->window_size.y);

	if (!gui_init(as))
		return SDL_APP_FAILURE;

	struct main_screen_state *ms = SDL_malloc(sizeof(*ms));
	if (!ms)
		return SDL_APP_FAILURE;
	as->main_screen = ms;
	SDL_memset(ms, 0, sizeof(*ms));

	for (unsigned i = 0; i < NUM_GAMES; ++i) {
		button_contexts[i].as = as;
		ms->game_buttons[i] =
		    button_init(as->gui, game_names[i], select_game_callback,
				&button_contexts[i]);
		if (!ms->game_buttons[i])
			return SDL_APP_FAILURE;
		button_fit_width(ms->game_buttons[i], 200.f);
	}
	position_game_buttons(as);

	as->current_frame_ticks = SDL_GetTicks();

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	struct app_state *as = (struct app_state *)appstate;
	switch (event->type) {
	case SDL_EVENT_WINDOW_RESIZED:
		as->window_size.x = event->window.data1;
		as->window_size.y = event->window.data2;
		if (as->current_game == NO_GAME)
			position_game_buttons(as);
		break;
	case SDL_EVENT_QUIT:
		return SDL_APP_SUCCESS;
	default:
		break;
	}
	if (gui_event(as, event))
		return SDL_APP_CONTINUE;

	switch (as->current_game) {
	case NO_GAME:
		for (unsigned i = 0; i < NUM_GAMES; ++i)
			button_event(as->main_screen->game_buttons[i], event);
		break;
	case SOLITARI:
		solitari_event(as, event);
		break;
	}
	return SDL_APP_CONTINUE;
}

static void draw_background(struct app_state *as) {
	SDL_SetRenderDrawColor(as->renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(as->renderer);
	SDL_FRect rect = {
	    .x = 0, .y = 0, .w = as->window_size.x, .h = as->window_size.y};
	SDL_RenderTextureTiled(as->renderer, as->table_texture, NULL, 1, &rect);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
	struct app_state *as = (struct app_state *)appstate;
	as->last_frame_ticks = as->current_frame_ticks;
	as->current_frame_ticks = SDL_GetTicks();
	draw_background(as);
	switch (as->current_game) {
	case NO_GAME:
		for (unsigned i = 0; i < NUM_GAMES; ++i)
			button_draw(
			    as, as->main_screen->game_buttons[i],
			    button_is_pressing(as->main_screen->game_buttons[i])
				? (SDL_Color){90, 170, 90, SDL_ALPHA_OPAQUE}
				: (SDL_Color){120, 205, 120, SDL_ALPHA_OPAQUE});
		break;
	case SOLITARI:
		solitari_iterate(as);
		break;
	}

	gui_iterate(as);
	SDL_RenderPresent(as->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	struct app_state *as = (struct app_state *)appstate;
	if (result == SDL_APP_FAILURE)
		SDL_Log("App finished due to failure: %s", SDL_GetError());
	if (!as)
		return;
	if (as->main_screen) {
		for (unsigned i = 0; i < NUM_GAMES; ++i)
			if (as->main_screen->game_buttons[i])
				button_quit(as->main_screen->game_buttons[i]);
		SDL_free(as->main_screen);
	}
	if (as->solitari)
		solitari_quit(as->solitari);
	if (as->gui)
		gui_quit(as->gui);
	if (as->table_texture)
		SDL_DestroyTexture(as->table_texture);
	if (as->deck_texture)
		SDL_DestroyTexture(as->deck_texture);
	TTF_Quit();
	SDL_free(as);
}
