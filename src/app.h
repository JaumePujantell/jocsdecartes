#ifndef APP_H
#define APP_H

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

enum game_id { NO_GAME = -1, SOLITARI, SET_I_MIG, LA_MONA, EL_SISET };
#define NUM_GAMES (EL_SISET + 1)

struct app_state {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *table_texture;
	SDL_Point window_size;
	struct card_texture *card_texture;
	Uint64 current_frame_ticks;
	enum game_id current_game;
	struct gui_state *gui;
	struct main_screen_state *main_screen;
	struct solitari_state *solitari;
	struct setimig_state *setimig;
	struct mona_state *mona;
	struct siset_state *siset;
};

bool get_path_to_resource(char **path, const char *kind, const char *name);
SDL_Texture *load_texture(SDL_Renderer *renderer, const char *file_name);
float window_pixel_density(const struct app_state *as);
bool current_game_can_restart(const struct app_state *as);
void restart_current_game(struct app_state *as);
void select_game(struct app_state *as, enum game_id id);

#endif // !APP_H