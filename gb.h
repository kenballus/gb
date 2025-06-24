#pragma once
#include <stdint.h> // for uint8_t, uint16_t, uint64_t

#define ADDRESS_SPACE_SIZE (0x10000)

#define GB_SCREEN_WIDTH (160)
#define GB_SCREEN_HEIGHT (144)

#define TILE_MAP_WIDTH (32) // Tiles
#define TILE_MAP_HEIGHT (32) // Tiles
#define TILE_WIDTH (8) // Pixels
#define TILE_HEIGHT (8) // Pixels

typedef unsigned _BitInt(1) uint1_t;

enum joypad_button {
    GB_KEY_A = 0,
    GB_KEY_B = 1,
    GB_KEY_START = 2,
    GB_KEY_SELECT = 3,
    GB_KEY_UP = 4,
    GB_KEY_DOWN = 5,
    GB_KEY_LEFT = 6,
    GB_KEY_RIGHT = 7,
    NUM_BUTTONS = 8,
};

enum joypad_mode {
    NEITHER = 0b00,
    ACTIONS = 0b01,
    DIRECTIONS = 0b10,
    BOTH = 0b11,
};

enum graphics_mode { HBLANK = 0, VBLANK = 1, SEARCHING = 2, TRANSFERRING = 3 };

struct point {
    uint8_t r;
    uint8_t c;
};

struct gb {
    uint16_t af;
    uint16_t bc;
    uint16_t de;
    uint16_t hl;
    uint16_t pc;
    uint16_t sp;
    uint1_t ime;
    uint8_t address_space[ADDRESS_SPACE_SIZE];
    uint8_t screen[TILE_MAP_HEIGHT * TILE_HEIGHT][TILE_MAP_WIDTH * TILE_WIDTH]; // We render everything to here.
    uint64_t cycles_to_wait;
    uint64_t cycle_count;
    uint1_t need_to_do_interrupts;
    uint64_t dot_count;
    enum graphics_mode graphics_mode;
    uint1_t halted;
    uint1_t buttons_pressed[NUM_BUTTONS];
    enum joypad_mode joypad_mode;
};

void press_button(struct gb *gb, enum joypad_button btn);

void release_button(struct gb *gb, enum joypad_button btn);

void step(struct gb *gb);

void wait(struct gb *gb);

struct point get_origin(struct gb *gb);

void dump(struct gb *gb);

void initialize(struct gb *gb, char const *path);
