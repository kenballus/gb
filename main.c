#include <stddef.h> // for NULL
#include <stdio.h>  // for printf, getchar, puts
#include <stdlib.h> // for EXIT_FAILURE

#include <SDL2/SDL.h>

#include "gb.h"

#define KEY_MAPPED_TO_A (SDLK_a)
#define KEY_MAPPED_TO_B (SDLK_b)
#define KEY_MAPPED_TO_START (SDLK_LSHIFT)
#define KEY_MAPPED_TO_SELECT (SDLK_RSHIFT)
#define KEY_MAPPED_TO_UP (SDLK_UP)
#define KEY_MAPPED_TO_DOWN (SDLK_DOWN)
#define KEY_MAPPED_TO_LEFT (SDLK_LEFT)
#define KEY_MAPPED_TO_RIGHT (SDLK_RIGHT)

typedef unsigned _BitInt(2) uint2_t;

int main(int argc, char const *const *const argv) {
    if (argc != 2) {
        printf("Usage: %s <rom_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return EXIT_FAILURE;
    }

    SDL_Window *const window =
        SDL_CreateWindow("gb", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        return EXIT_FAILURE;
    }

    SDL_Renderer *const renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        return EXIT_FAILURE;
    }

    SDL_PixelFormat *const format =
        SDL_AllocFormat(SDL_GetWindowPixelFormat(window));
    if (format == NULL) {
        return EXIT_FAILURE;
    }
    uint32_t const color_white = SDL_MapRGB(format, 0xFF, 0xFF, 0xFF);
    uint32_t const color_light_grey = SDL_MapRGB(format, 0xaa, 0xaa, 0xaa);
    uint32_t const color_dark_grey = SDL_MapRGB(format, 0x55, 0x55, 0x55);
    uint32_t const color_black = SDL_MapRGB(format, 0x00, 0x00, 0x00);
    SDL_FreeFormat(format);

    SDL_Texture *const gb_screen = SDL_CreateTexture(
        renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING,
        GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT);
    if (gb_screen == NULL) {
        return EXIT_FAILURE;
    }

    struct gb gb;
    initialize(&gb, argv[1]);

    while (1) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) { // While there are events to process
            if (event.type == SDL_QUIT) {
                goto done;
            }
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case KEY_MAPPED_TO_A:
                    press_button(&gb, GB_KEY_A);
                    break;
                case KEY_MAPPED_TO_B:
                    press_button(&gb, GB_KEY_B);
                    break;
                case KEY_MAPPED_TO_START:
                    press_button(&gb, GB_KEY_START);
                    break;
                case KEY_MAPPED_TO_SELECT:
                    press_button(&gb, GB_KEY_SELECT);
                    break;
                case KEY_MAPPED_TO_UP:
                    press_button(&gb, GB_KEY_UP);
                    break;
                case KEY_MAPPED_TO_DOWN:
                    press_button(&gb, GB_KEY_DOWN);
                    break;
                case KEY_MAPPED_TO_LEFT:
                    press_button(&gb, GB_KEY_LEFT);
                    break;
                case KEY_MAPPED_TO_RIGHT:
                    press_button(&gb, GB_KEY_RIGHT);
                    break;
                default:
                }
            } else if (event.type == SDL_KEYUP) {
                switch (event.key.keysym.sym) {
                case KEY_MAPPED_TO_A:
                    release_button(&gb, GB_KEY_A);
                    break;
                case KEY_MAPPED_TO_B:
                    release_button(&gb, GB_KEY_B);
                    break;
                case KEY_MAPPED_TO_START:
                    release_button(&gb, GB_KEY_START);
                    break;
                case KEY_MAPPED_TO_SELECT:
                    release_button(&gb, GB_KEY_SELECT);
                    break;
                case KEY_MAPPED_TO_UP:
                    release_button(&gb, GB_KEY_UP);
                    break;
                case KEY_MAPPED_TO_DOWN:
                    release_button(&gb, GB_KEY_DOWN);
                    break;
                case KEY_MAPPED_TO_LEFT:
                    release_button(&gb, GB_KEY_LEFT);
                    break;
                case KEY_MAPPED_TO_RIGHT:
                    release_button(&gb, GB_KEY_RIGHT);
                    break;
                default:
                }
            }
        }
        step(&gb);
        wait(&gb);

        if (gb.cycle_count % 1000 == 0) {
            void *raw_pixels = NULL;
            int unused = 0;
            SDL_LockTexture(gb_screen, NULL, &raw_pixels, &unused);
            uint32_t *const pixels = (uint32_t *)raw_pixels;
            struct point const origin = get_origin(&gb);
            for (size_t r = 0; r < GB_SCREEN_HEIGHT; r++) {
                for (size_t c = 0; c < GB_SCREEN_WIDTH; c++) {
                    uint32_t pixel_color;
                    switch (
                        (uint2_t)gb.screen[(origin.r + r) %
                                           (TILE_MAP_WIDTH * TILE_WIDTH)]
                                          [(origin.c + c) %
                                           (TILE_MAP_HEIGHT * TILE_HEIGHT)]) {
                    case 0b00:
                        pixel_color = color_white;
                        break;
                    case 0b01:
                        pixel_color = color_light_grey;
                        break;
                    case 0b10:
                        pixel_color = color_dark_grey;
                        break;
                    case 0b11:
                        pixel_color = color_black;
                        break;
                    default:
                        return EXIT_FAILURE;
                    }
                    pixels[r * GB_SCREEN_WIDTH + c] = pixel_color;
                }
            }

            SDL_UnlockTexture(gb_screen);
            SDL_RenderCopy(renderer, gb_screen, NULL, NULL);
            SDL_RenderPresent(renderer);
            SDL_UpdateWindowSurface(window);
        }
    }
done:
    SDL_DestroyTexture(gb_screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    SDL_Quit();
}
