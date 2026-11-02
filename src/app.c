#define SDL_MAIN_USE_CALLBACKS 1 /* use the callbacks instead of main() */
#include <SDL3/SDL_main.h>

#include "app.h"

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdbool.h>

#include "card_texture.h"
#include "gui.h"
#include "mona.h"
#include "setimig.h"
#include "siset.h"
#include "solitari.h"
#include "tuti.h"

#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) &&                 \
    !defined(__CYGWIN__)
#define OS_PATH_SEPARATOR "\\"
#else
#define OS_PATH_SEPARATOR "/"
#endif

static const char *app_name = "Jocs de Cartes";
static const char *game_names[NUM_GAMES] = {
    "El Solitari", "El Set i mig", "La Mona", "El Siset", "El Tuti",
};

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
#define MIN_WINDOW_WIDTH 320
#define MIN_WINDOW_HEIGHT 240

#ifdef __ANDROID__
#define WINDOW_FLAGS (SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN)
#else
#define WINDOW_FLAGS SDL_WINDOW_RESIZABLE
#endif
#define GAME_BUTTON_WIDTH_FRACTION 0.4F
#define GAME_BUTTONS_HEIGHT_FRACTION 0.7F
#define GAME_BUTTON_SPACING_FRACTION 0.25F

static const SDL_Color background_color = {255, 255, 255, SDL_ALPHA_OPAQUE};

struct main_screen_state {
	struct button *game_buttons[NUM_GAMES];
};

bool get_path_to_resource(char **path, const char *kind, const char *name) {
	*path = NULL;
#ifdef __ANDROID__
	SDL_asprintf(path, "%s" OS_PATH_SEPARATOR "%s", kind, name);
#else
	const char *base_path = SDL_GetBasePath();
	if (!base_path) {
		return false;
	}
	SDL_asprintf(path, "%s%s" OS_PATH_SEPARATOR "%s", base_path, kind,
		     name);
#endif
	return *path != NULL;
}

/* Load a png to an SDL_Texture, without using image lib */
SDL_Texture *load_texture(SDL_Renderer *renderer, const char *file_name) {
	char *png_path = NULL;
	if (!get_path_to_resource(&png_path, "textures", file_name)) {
		return NULL;
	}
	SDL_Surface *surface = SDL_LoadPNG(png_path);
	SDL_free(png_path);
	if (!surface) {
		return NULL;
	}
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_DestroySurface(surface);
	return texture;
}

float window_pixel_density(const struct app_state *as) {
	const float density = SDL_GetWindowPixelDensity(as->window);
	return density > 0.F ? density : 1.F;
}

static void update_render_scale(struct app_state *as) {
	const float density = window_pixel_density(as);
	SDL_SetRenderScale(as->renderer, density, density);
}

static float game_button_spacing(const struct main_screen_state *ms) {
	return button_get_size(ms->game_buttons[0]).y *
	       GAME_BUTTON_SPACING_FRACTION;
}

static float game_buttons_height(const struct main_screen_state *ms) {
	const float spacing = game_button_spacing(ms);
	float total_height = -spacing;
	for (unsigned i = 0; i < NUM_GAMES; ++i) {
		total_height +=
		    button_get_size(ms->game_buttons[i]).y + spacing;
	}
	return total_height;
}

static void fit_game_buttons(struct app_state *as) {
	struct main_screen_state *ms = as->main_screen;
	const float width =
	    (float)as->window_size.x * GAME_BUTTON_WIDTH_FRACTION;
	button_group_fit_width(ms->game_buttons, NUM_GAMES, width);

	const float max_height =
	    (float)as->window_size.y * GAME_BUTTONS_HEIGHT_FRACTION;
	const float height = game_buttons_height(ms);
	if (height > max_height) {
		button_group_fit_width(ms->game_buttons, NUM_GAMES,
				       width * max_height / height);
	}
}

static void layout_game_buttons(struct app_state *as) {
	fit_game_buttons(as);

	struct main_screen_state *ms = as->main_screen;
	const float spacing = game_button_spacing(ms);
	float y = ((float)as->window_size.y - game_buttons_height(ms)) / 2.F;
	for (unsigned i = 0; i < NUM_GAMES; ++i) {
		SDL_FPoint size = button_get_size(ms->game_buttons[i]);
		button_set_position(ms->game_buttons[i],
				    ((float)as->window_size.x - size.x) / 2.F,
				    y);
		y += size.y + spacing;
	}
}

static void leave_current_game(struct app_state *as) {
	switch (as->current_game) {
	case SET_I_MIG:
		setimig_leave(as);
		break;
	case LA_MONA:
		mona_leave(as);
		break;
	case EL_SISET:
		siset_leave(as);
		break;
	case EL_TUTI:
		tuti_leave(as);
		break;
	default:
		break;
	}
}

static bool enter_game(struct app_state *as, enum game_id id) {
	switch (id) {
	case NO_GAME:
		layout_game_buttons(as);
		break;
	case SOLITARI:
		if (!as->solitari) {
			return solitari_init(as);
		}
		solitari_resize(as);
		solitari_restart(as->solitari);
		break;
	case SET_I_MIG:
		if (!as->setimig) {
			return setimig_init(as);
		}
		setimig_resize(as);
		setimig_choose_options(as->setimig);
		break;
	case LA_MONA:
		if (!as->mona) {
			return mona_init(as);
		}
		mona_resize(as);
		mona_choose_options(as->mona);
		break;
	case EL_SISET:
		if (!as->siset) {
			return siset_init(as);
		}
		siset_resize(as);
		siset_choose_options(as->siset);
		break;
	case EL_TUTI:
		if (!as->tuti) {
			return tuti_init(as);
		}
		tuti_resize(as);
		tuti_choose_options(as->tuti);
		break;
	}
	return true;
}

void select_game(struct app_state *as, enum game_id id) {
	leave_current_game(as);
	if (!enter_game(as, id)) {
		SDL_Log("Failed to start %s: %s", game_names[id],
			SDL_GetError());
		return;
	}
	as->current_game = id;
}

bool current_game_can_restart(const struct app_state *as) {
	switch (as->current_game) {
	case SOLITARI:
		return true;
	case SET_I_MIG:
		return !setimig_is_choosing_options(as->setimig);
	case LA_MONA:
		return !mona_is_choosing_options(as->mona);
	case EL_SISET:
		return !siset_is_choosing_options(as->siset);
	case EL_TUTI:
		return !tuti_is_choosing_options(as->tuti);
	default:
		return false;
	}
}

void restart_current_game(struct app_state *as) {
	switch (as->current_game) {
	case NO_GAME:
		break;
	case SOLITARI:
		solitari_restart(as->solitari);
		break;
	case SET_I_MIG:
		setimig_restart(as->setimig);
		break;
	case LA_MONA:
		mona_restart(as->mona);
		break;
	case EL_SISET:
		siset_restart(as->siset);
		break;
	case EL_TUTI:
		tuti_restart(as->tuti);
		break;
	}
}

static struct game_button_context {
	struct app_state *as;
	enum game_id id;
} button_contexts[NUM_GAMES];

static void select_game_callback(void *userdata) {
	struct game_button_context *ctx =
	    (struct game_button_context *)userdata;
	select_game(ctx->as, ctx->id);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	SDL_SetAppMetadata(app_name, "0.3", NULL);

	struct app_state *as = SDL_calloc(1, sizeof(*as));
	if (!as) {
		return SDL_APP_FAILURE;
	}
	*appstate = as;
	as->current_game = NO_GAME;

	/* Only affects Android/iOS. */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

	if (!SDL_Init(SDL_INIT_VIDEO) || !TTF_Init()) {
		return SDL_APP_FAILURE;
	}

	as->window = SDL_CreateWindow(app_name, WINDOW_WIDTH, WINDOW_HEIGHT,
				      WINDOW_FLAGS);
	if (!as->window) {
		return SDL_APP_FAILURE;
	}
	SDL_SetWindowMinimumSize(as->window, MIN_WINDOW_WIDTH,
				 MIN_WINDOW_HEIGHT);
	as->renderer = SDL_CreateRenderer(as->window, NULL);
	if (!as->renderer) {
		return SDL_APP_FAILURE;
	}
	update_render_scale(as);

	as->table_texture = load_texture(as->renderer, "wood_tile.png");
	if (!as->table_texture) {
		return SDL_APP_FAILURE;
	}
	as->card_texture = card_texture_init(as->renderer);
	if (!as->card_texture) {
		return SDL_APP_FAILURE;
	}

	SDL_SetRenderDrawBlendMode(as->renderer, SDL_BLENDMODE_BLEND);

	SDL_GetWindowSize(as->window, &as->window_size.x, &as->window_size.y);

	if (!gui_init(as)) {
		return SDL_APP_FAILURE;
	}

	struct main_screen_state *ms = SDL_calloc(1, sizeof(*ms));
	if (!ms) {
		return SDL_APP_FAILURE;
	}
	as->main_screen = ms;

	for (unsigned i = 0; i < NUM_GAMES; ++i) {
		button_contexts[i].as = as;
		button_contexts[i].id = (enum game_id)i;
		ms->game_buttons[i] =
		    button_init(as->gui, game_names[i], select_game_callback,
				&button_contexts[i]);
		if (!ms->game_buttons[i]) {
			return SDL_APP_FAILURE;
		}
	}
	layout_game_buttons(as);

	as->current_frame_ticks = SDL_GetTicks();

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	struct app_state *as = (struct app_state *)appstate;
	switch (event->type) {
	case SDL_EVENT_WINDOW_RESIZED:
		as->window_size.x = event->window.data1;
		as->window_size.y = event->window.data2;
		if (as->current_game == NO_GAME) {
			layout_game_buttons(as);
		}
		break;
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		update_render_scale(as);
		break;
	case SDL_EVENT_QUIT:
		return SDL_APP_SUCCESS;
	default:
		break;
	}
	if (gui_event(as, event)) {
		return SDL_APP_CONTINUE;
	}

	switch (as->current_game) {
	case NO_GAME:
		for (unsigned i = 0; i < NUM_GAMES; ++i) {
			button_event(as->main_screen->game_buttons[i], event);
		}
		break;
	case SOLITARI:
		solitari_event(as, event);
		break;
	case SET_I_MIG:
		setimig_event(as, event);
		break;
	case LA_MONA:
		mona_event(as, event);
		break;
	case EL_SISET:
		siset_event(as, event);
		break;
	case EL_TUTI:
		tuti_event(as, event);
		break;
	}
	return SDL_APP_CONTINUE;
}

static void draw_background(struct app_state *as) {
	SDL_SetRenderDrawColor(as->renderer, background_color.r,
			       background_color.g, background_color.b,
			       background_color.a);
	SDL_RenderClear(as->renderer);
	SDL_FRect rect = {
	    .x = 0,
	    .y = 0,
	    .w = (float)as->window_size.x,
	    .h = (float)as->window_size.y,
	};
	SDL_RenderTextureTiled(as->renderer, as->table_texture, NULL, 1, &rect);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
	struct app_state *as = (struct app_state *)appstate;
	as->current_frame_ticks = SDL_GetTicks();

	draw_background(as);

	switch (as->current_game) {
	case NO_GAME:
		for (unsigned i = 0; i < NUM_GAMES; ++i) {
			button_draw(
			    as, as->main_screen->game_buttons[i],
			    button_is_pressing(as->main_screen->game_buttons[i])
				? green_button_pressed_color
				: green_button_color);
		}
		break;
	case SOLITARI:
		solitari_iterate(as);
		break;
	case SET_I_MIG:
		setimig_iterate(as);
		break;
	case LA_MONA:
		mona_iterate(as);
		break;
	case EL_SISET:
		siset_iterate(as);
		break;
	case EL_TUTI:
		tuti_iterate(as);
		break;
	}

	gui_iterate(as);

	SDL_RenderPresent(as->renderer);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	struct app_state *as = (struct app_state *)appstate;
	if (result == SDL_APP_FAILURE) {
		SDL_Log("App finished due to failure: %s", SDL_GetError());
	}
	if (!as) {
		return;
	}
	if (as->main_screen) {
		for (unsigned i = 0; i < NUM_GAMES; ++i) {
			if (as->main_screen->game_buttons[i]) {
				button_quit(as->main_screen->game_buttons[i]);
			}
		}
		SDL_free(as->main_screen);
	}
	if (as->solitari) {
		solitari_quit(as->solitari);
	}
	if (as->setimig) {
		setimig_quit(as);
	}
	if (as->mona) {
		mona_quit(as);
	}
	if (as->siset) {
		siset_quit(as);
	}
	if (as->tuti) {
		tuti_quit(as);
	}
	if (as->gui) {
		gui_quit(as->gui);
	}
	if (as->table_texture) {
		SDL_DestroyTexture(as->table_texture);
	}
	if (as->card_texture) {
		card_texture_quit(as->card_texture);
	}
	TTF_Quit();
	SDL_free(as);
}
