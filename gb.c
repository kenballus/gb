#define _GNU_SOURCE   // for nanosleep(2)
#include <inttypes.h> // for PRI*
#include <stddef.h>   // for NULL
#include <stdint.h>   // for int*_t, uint*_t
#include <stdio.h>    // for printf, fprintf, stderr, fopen, fread, fclose
#include <stdlib.h>   // for exit, EXIT_FAILURE
#include <string.h>   // for memset
#include <time.h>     // for nanosleep

#include "gb.h"

#define DEBUG(...) fprintf(stderr, __VA_ARGS__)

#define DIE(...)                                                               \
    do {                                                                       \
        fprintf(stderr, __VA_ARGS__);                                          \
        exit(EXIT_FAILURE);                                                    \
    } while (0)

typedef unsigned _BitInt(1) uint1_t;
typedef unsigned _BitInt(2) uint2_t;
typedef unsigned _BitInt(3) uint3_t;
typedef unsigned _BitInt(4) uint4_t;
typedef unsigned _BitInt(5) uint5_t;
typedef unsigned _BitInt(9) uint9_t;
typedef _BitInt(12) int12_t;
typedef unsigned _BitInt(12) uint12_t;
typedef unsigned _BitInt(13) uint13_t;
typedef unsigned _BitInt(17) uint17_t;

uint16_t const IVT_OFFSET = 0x0000;
uint16_t const HEADER_OFFSET = 0x0100;
uint16_t const ROM_BANK_0 = 0x0150;
uint16_t const ROM_BANK_1 = 0x4000;
// This is where tiles live:
uint16_t const UNSIGNED_TILE_DATA_BASE = 0x8000;
uint16_t const SIGNED_TILE_DATA_BASE = 0x9000;
// This is where tile maps live:
uint16_t const TILE_MAP_1 = 0x9800;
uint16_t const TILE_MAP_2 = 0x9C00;

uint16_t const CARTRIDGE_RAM = 0xA000;
uint16_t const WRAM = 0xC000;
uint16_t const ECHO_RAM = 0xE000;
uint16_t const OAM = 0xFE00;
uint16_t const UNUSED_ADDRESSES = 0xFEA0;
uint16_t const IO_REGS = 0xFF00;
uint16_t const FAST_RAM = 0xFF80;

uint16_t const VBLANK_INTERRUPT_ADDRESS = 0x0040;
uint16_t const LCD_STAT_INTERRUPT_ADDRESS = 0x0048;
uint16_t const TIMER_INTERRUPT_ADDRESS = 0x0050;
uint16_t const SERIAL_INTERRUPT_ADDRESS = 0x0058;
uint16_t const JOYPAD_INTERRUPT_ADDRESS = 0x0060;

uint16_t const LCD_CONTROL = 0xFF40;
uint16_t const LCD_STATUS = 0xFF41;
uint16_t const SCY = 0xFF42;
uint16_t const SCX = 0xFF43;
uint16_t const LY = 0xFF44;
uint16_t const LYC = 0xFF45;
#define OAM_DMA_START (0xFF46)
uint16_t const BGP = 0xFF47;
uint16_t const OBP0 = 0xFF48;
uint16_t const OBP1 = 0xFF49;
uint16_t const WY = 0xFF4A;
uint16_t const WX = 0xFF4B;

#define JOYPAD_PORT (0xFF00)
#define SERIAL_DATA (0xFF01)
uint16_t const SERIAL_CONTROL = 0xFF02;
#define DIVIDER_REGISTER (0xFF04)
uint16_t const TIMA = 0xFF05;
uint16_t const TMA = 0xFF06;
uint16_t const TAC = 0xFF07;
#define INTERRUPT_FLAGS (0xFF0F)
#define INTERRUPT_ENABLE (0xFFFF)

#define FIRST_VBLANK_SCANLINE (144)
#define TILE_MAP_WIDTH (32)  // Tiles
#define TILE_MAP_HEIGHT (32) // Tiles
#define TILE_WIDTH (8)       // Pixels
#define TILE_HEIGHT (8)      // Pixels
#define BYTES_PER_TILE (0x10)
#define TILE_MAP_SIZE (0x400) // Bytes
#define DIVIDER_REGISTER_RATE (16384)
#define CLOCK_SPEED (1048576) // M-cycles
// So we do 64 M-cycles for each increment of the divider:
#define CLOCKS_PER_DIVIDER_INCREMENT (64)
#define NUM_SPRITES (40)

// 16 ms/frame gets us a little over 60 fps
#define MS_PER_CYCLE (100)

enum cc_cond { CC_NZ = 0b00, CC_Z = 0b01, CC_NC = 0b10, CC_C = 0b11 };

static char const *cc_to_str(enum cc_cond const cc) {
    switch (cc) {
    case CC_NZ:
        return "NZ";
    case CC_Z:
        return "Z";
    case CC_NC:
        return "NC";
    case CC_C:
        return "C";
    default:
        DIE("Invalid cc condition!\n");
    }
}

enum r_reg {
    R_A = 0b111,
    R_B = 0b000,
    R_C = 0b001,
    R_D = 0b010,
    R_E = 0b011,
    R_H = 0b100,
    R_L = 0b101
};

enum dd_reg { DD_BC = 0b00, DD_DE = 0b01, DD_HL = 0b10, DD_SP = 0b11 };

enum qq_reg { QQ_BC = 0b00, QQ_DE = 0b01, QQ_HL = 0b10, QQ_AF = 0b11 };

enum flag {
    FL_Z = 0b10000000,
    FL_N = 0b01000000,
    FL_H = 0b00100000,
    FL_C = 0b00010000
};

static void set_flag(struct gb *const gb, enum flag flag, uint1_t value) {
    if (value) {
        gb->af |= flag;
    } else {
        gb->af &= ~flag;
    }
}

static uint1_t get_flag(struct gb const *const gb, enum flag flag) {
    return (gb->af & flag) != 0;
}

static uint1_t check_cc(struct gb const *const gb, enum cc_cond const cc) {
    switch (cc) {
    case CC_NZ:
        return !get_flag(gb, FL_Z);
    case CC_Z:
        return get_flag(gb, FL_Z);
    case CC_NC:
        return !get_flag(gb, FL_C);
    case CC_C:
        return get_flag(gb, FL_C);
    default:
        DIE("Invalid cc condition!\n");
    }
}

static uint8_t read_mem8(struct gb *const gb, uint16_t addr) {
    if (ECHO_RAM < addr && addr < OAM) {
        addr -= 0x2000;
    }
    if (addr == LY) {
        // return 0x90; // TODO: DELETE THIS
    }
    if (addr == JOYPAD_PORT) {
        uint8_t retval = (0b11 << 6) | (gb->joypad_mode ? 0b00010000 : 0);
        if (gb->joypad_mode & DIRECTIONS) {
            retval |= ((uint4_t)gb->buttons_pressed[GB_KEY_DOWN] << 3) |
                      ((uint4_t)gb->buttons_pressed[GB_KEY_UP] << 2) |
                      ((uint4_t)gb->buttons_pressed[GB_KEY_LEFT] << 1) |
                      ((uint4_t)gb->buttons_pressed[GB_KEY_RIGHT] << 0);
        }
        if (gb->joypad_mode & ACTIONS) {
            retval |= ((uint4_t)gb->buttons_pressed[GB_KEY_START] << 3) |
                      ((uint4_t)gb->buttons_pressed[GB_KEY_SELECT] << 2) |
                      ((uint4_t)gb->buttons_pressed[GB_KEY_B] << 1) |
                      ((uint4_t)gb->buttons_pressed[GB_KEY_A] << 0);
        }

        return retval;
    }

    // XXX: OAM+VRAM should not be readable at all times.
    return gb->address_space[addr];
}

struct point get_origin(struct gb *gb) {
    return (struct point){.r = read_mem8(gb, SCY), .c = read_mem8(gb, SCX)};
}

void dump(struct gb *const gb) {
    printf("A:%02X F:%02X B:%02X C:%02X D:%02X E:%02X H:%02X L:%02X SP:%04X "
           "PC:%04X PCMEM:%02X,%02X,%02X,%02X\n",
           gb->af >> 8, gb->af & 0xffu, gb->bc >> 8, gb->bc & 0xffu,
           gb->de >> 8, gb->de & 0xffu, gb->hl >> 8, gb->hl & 0xffu, gb->sp,
           gb->pc, read_mem8(gb, gb->pc), read_mem8(gb, gb->pc + 1),
           read_mem8(gb, gb->pc + 2), read_mem8(gb, gb->pc + 3));
}

static uint16_t read_mem16(struct gb *const gb, uint16_t const addr) {
    return (read_mem8(gb, addr + 1) << 8) | read_mem8(gb, addr);
}

static uint1_t is_writable(uint16_t addr) {
    // XXX: OAM+VRAM should not be writable at all times.
    return (UNSIGNED_TILE_DATA_BASE <= addr && addr < ECHO_RAM) ||
           (OAM <= addr && addr < INTERRUPT_ENABLE);
}

static void do_dma(struct gb *const gb, uint8_t const src) {
    gb->cycles_to_wait += 160;
    for (uint16_t i = 0; i < 0xa0; i++) {
        // Avoid write_mem8 here to avoid recursion
        gb->address_space[OAM + i] = read_mem8(gb, (src << 8) + i);
    }
}

static void write_mem8(struct gb *const gb, uint16_t const addr,
                       uint8_t const val) {
    switch (addr) {
    case DIVIDER_REGISTER: {
        gb->address_space[addr] =
            0; // See page 25 of the Nintendo programming docs for why
        break;
    }
    case SERIAL_DATA: {
        DEBUG("[SERIAL]: '%c'\n", val);
        break;
    }
    case JOYPAD_PORT: {
        gb->address_space[addr] = (gb->address_space[addr] & 0b1111) | (val & 0b11110000);
        gb->joypad_mode = (enum joypad_mode)(uint2_t)(val >> 4);
        break;
    }
    case INTERRUPT_FLAGS:
    case INTERRUPT_ENABLE: {
        gb->address_space[addr] = val;
        gb->need_to_do_interrupts = 1;
        break;
    }
    case OAM_DMA_START: {
        do_dma(gb, val);
        break;
    }
    default: {
        if (is_writable(addr)) {
            gb->address_space[addr] = val;
        } else if (HEADER_OFFSET <= addr && addr < UNSIGNED_TILE_DATA_BASE) {
            fprintf(stderr,
                    "Attempted bank switch, which is not implemented.\n");
        } else {
            fprintf(stderr,
                    "Attempted potentially illegal write of 0x%02" PRIX8
                    " to 0x%04" PRIX16 "!\n",
                    val, addr);
        }
        break;
    }
    }
}

enum interrupt {
    INT_VBLANK = 0b1,
    INT_STAT = 0b10,
    INT_TIMER = 0b100,
    INT_SERIAL = 0b1000,
    INT_JOYPAD = 0b10000,
};

void request_interrupt(struct gb *const gb, enum interrupt const interrupt) {
    write_mem8(gb, INTERRUPT_FLAGS, read_mem8(gb, INTERRUPT_FLAGS) | interrupt);
}

void press_button(struct gb *const gb, enum joypad_button const btn) {
    // This is the opposite of what you'd think.
    gb->buttons_pressed[btn] = 0;
    request_interrupt(gb, INT_JOYPAD);
}

void release_button(struct gb *const gb, enum joypad_button const btn) {
    // This is the opposite of what you'd think.
    gb->buttons_pressed[btn] = 1;
}

static void enter_hblank(struct gb *const gb) {
    gb->address_space[LCD_STATUS] =
        (read_mem8(gb, LCD_STATUS) & 0b11111100) | 0b00;

    gb->graphics_mode = HBLANK;

    if (read_mem8(gb, LCD_STATUS) & 0b00001000) {
        request_interrupt(gb, INT_STAT);
    }
}

static void enter_vblank(struct gb *const gb) {
    gb->address_space[LCD_STATUS] =
        (read_mem8(gb, LCD_STATUS) & 0b11111100) | 0b01;

    if (read_mem8(gb, LCD_STATUS) & 0b00010000) {
        request_interrupt(gb, INT_STAT);
    }

    request_interrupt(gb, INT_VBLANK);

    gb->graphics_mode = VBLANK;
}

static void enter_searching(struct gb *const gb) {
    gb->address_space[LCD_STATUS] =
        (read_mem8(gb, LCD_STATUS) & 0b11111100) | 0b10;

    gb->graphics_mode = SEARCHING;

    if (read_mem8(gb, LCD_STATUS) & 0b00100000) {
        request_interrupt(gb, INT_STAT);
    }
}

static void enter_transferring(struct gb *const gb) {
    gb->address_space[LCD_STATUS] = read_mem8(gb, LCD_STATUS) | 0b11;

    gb->graphics_mode = TRANSFERRING;
}

static void write_mem16(struct gb *const gb, uint16_t const addr,
                        uint16_t const val) {
    write_mem8(gb, addr, val);
    write_mem8(gb, addr + 1, val >> 8);
}

static void handle_interrupts(struct gb *const gb) {
    // The interrupt(s) that are valid to be entered at this time
    uint8_t const interrupts_requested = read_mem8(gb, INTERRUPT_FLAGS);
    uint8_t const interrupts_enabled = read_mem8(gb, INTERRUPT_ENABLE);

    if (interrupts_requested & interrupts_enabled) {
        gb->halted = 0;
    }

    if (!gb->ime) {
        return;
    }

    uint1_t const vblank_requested = interrupts_requested;
    uint1_t const lcd_stat_requested = interrupts_requested >> 1;
    uint1_t const timer_requested = interrupts_requested >> 2;
    uint1_t const serial_requested = interrupts_requested >> 3;
    uint1_t const joypad_requested = interrupts_requested >> 4;

    // The interrupt(s) that the programmer asked for
    uint1_t const vblank_enabled = interrupts_enabled;
    uint1_t const lcd_stat_enabled = interrupts_enabled >> 1;
    uint1_t const timer_enabled = interrupts_enabled >> 2;
    uint1_t const serial_enabled = interrupts_enabled >> 3;
    uint1_t const joypad_enabled = interrupts_enabled >> 4;

    uint16_t new_pc = 0xFFFF; // Not an interrupt address.
    if (vblank_requested && vblank_enabled) {
        new_pc = VBLANK_INTERRUPT_ADDRESS;
        gb->address_space[INTERRUPT_FLAGS] = interrupts_requested & ~INT_VBLANK;
    } else if (lcd_stat_requested && lcd_stat_enabled) {
        new_pc = LCD_STAT_INTERRUPT_ADDRESS;
        gb->address_space[INTERRUPT_FLAGS] = interrupts_requested & ~INT_STAT;
    } else if (timer_requested && timer_enabled) {
        new_pc = TIMER_INTERRUPT_ADDRESS;
        gb->address_space[INTERRUPT_FLAGS] = interrupts_requested & ~INT_TIMER;
    } else if (serial_requested && serial_enabled) {
        new_pc = SERIAL_INTERRUPT_ADDRESS;
        gb->address_space[INTERRUPT_FLAGS] = interrupts_requested & ~INT_SERIAL;
    } else if (joypad_requested && joypad_enabled) {
        new_pc = JOYPAD_INTERRUPT_ADDRESS;
        gb->address_space[INTERRUPT_FLAGS] = interrupts_requested & ~INT_JOYPAD;
    }

    if (new_pc != 0xFFFF) {
        gb->ime = 0;
        write_mem16(gb, gb->sp - 2, gb->pc);
        gb->sp -= 2;
        gb->pc = new_pc;
    }

    gb->cycles_to_wait += 5;
    gb->need_to_do_interrupts = 0;
}

void initialize(struct gb *const gb, char const *const path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        DIE("Couldn't load rom from %s!\n", path);
    }
    fread(gb->address_space, sizeof(char), ADDRESS_SPACE_SIZE, f);
    fclose(f);

    gb->address_space[DIVIDER_REGISTER] = 0x18;
    gb->address_space[TIMA] = 0x00;
    gb->address_space[TMA] = 0x00;
    gb->address_space[TAC] = 0xF8;
    gb->address_space[INTERRUPT_FLAGS] = 0xE1;

    gb->address_space[LCD_CONTROL] = 0x91;
    gb->address_space[LCD_STATUS] = 0x81; // This is an illegal write otherwise.
    gb->address_space[SCY] = 0x00;
    gb->address_space[SCX] = 0x00;
    gb->address_space[LY] = 0x91;
    gb->address_space[LYC] = 0x00;
    gb->address_space[OAM_DMA_START] = 0xFF; // Don't want to trigger a DMA right now.
    gb->address_space[BGP] = 0xFC;
    gb->address_space[OBP0] = 0xFC; // This is uninitialized, so they can be
                                // whatever. May as well be same as BGP
    gb->address_space[OBP1] = 0xFC; // This is uninitialized, so they can be
                                // whatever. May as well be same as BGP
    gb->address_space[WY] = 0x00;
    gb->address_space[WX] = 0x00;
    gb->address_space[INTERRUPT_ENABLE] = 0x00;

    gb->af = 0x01B0;
    gb->bc = 0x0013;
    gb->de = 0x00d8;
    gb->hl = 0x014d;
    gb->pc = 0x0100;
    gb->sp = 0xfffe;
    gb->ime = 0;
    gb->cycles_to_wait = 0;
    gb->cycle_count = 0;
    gb->need_to_do_interrupts = 1;
    gb->graphics_mode = SEARCHING;
    gb->joypad_mode = BOTH; // This might be unitialized in reality
    gb->halted = 0;
    memset(gb->screen, 0, sizeof(gb->screen));
    memset(gb->buttons_pressed, 1, sizeof(gb->buttons_pressed));
}

static uint8_t *r_reg(struct gb *const gb, enum r_reg const r) {
    // Assumes little-endian
    switch (r) {
    case R_A:
        return ((uint8_t *)(&gb->af)) + 1;
    case R_B:
        return ((uint8_t *)(&gb->bc)) + 1;
    case R_C:
        return (uint8_t *)&gb->bc;
    case R_D:
        return ((uint8_t *)(&gb->de)) + 1;
    case R_E:
        return (uint8_t *)&gb->de;
    case R_H:
        return ((uint8_t *)(&gb->hl)) + 1;
    case R_L:
        return (uint8_t *)&gb->hl;
    default:
        DIE("Invalid r register!\n");
    }
}

static uint16_t *dd_reg(struct gb *const gb, enum dd_reg const dd) {
    switch (dd) {
    case DD_BC:
        return &gb->bc;
    case DD_DE:
        return &gb->de;
    case DD_HL:
        return &gb->hl;
    case DD_SP:
        return &gb->sp;
    default:
        DIE("Invalid dd register!\n");
    }
}

static uint16_t *qq_reg(struct gb *const gb, enum qq_reg const qq) {
    switch (qq) {
    case QQ_BC:
        return &gb->bc;
    case QQ_DE:
        return &gb->de;
    case QQ_HL:
        return &gb->hl;
    case QQ_AF:
        return &gb->af;
    default:
        DIE("Invalid qq register!\n");
    }
}

static char const *r_to_str(enum r_reg r) {
    switch (r) {
    case R_A:
        return "A";
    case R_B:
        return "B";
    case R_C:
        return "C";
    case R_D:
        return "D";
    case R_E:
        return "E";
    case R_H:
        return "H";
    case R_L:
        return "L";
    default:
        DIE("Invalid r register!\n");
    }
}

static char const *dd_to_str(enum dd_reg dd) {
    switch (dd) {
    case DD_BC:
        return "BC";
    case DD_DE:
        return "DE";
    case DD_HL:
        return "HL";
    case DD_SP:
        return "SP";
    default:
        DIE("Invalid dd register!\n");
    }
}

static char const *qq_to_str(enum qq_reg qq) {
    switch (qq) {
    case QQ_BC:
        return "BC";
    case QQ_DE:
        return "DE";
    case QQ_HL:
        return "HL";
    case QQ_AF:
        return "AF";
    default:
        DIE("Invalid qq register!\n");
    }
}

static void render_tile(struct gb *const gb, int16_t const start_y,
                        int16_t const start_x, uint16_t const tile_address,
                        uint16_t const palette_address,
                        uint1_t const is_sprite_tile, uint1_t const y_flip,
                        uint1_t const x_flip) {
    if (x_flip) {
        puts("x_flip");
        getchar();
    }
    if (y_flip) {
        puts("y_flip");
        getchar();
    }
    for (uint8_t i = 0; i < 8; i++) {              // For each row in the tile,
        uint8_t const curr_y = y_flip ? 7 - i : i; // flip around if necessary
        // Read a row from the tile
        // (one row is 2 bytes, interleaved)
        uint8_t const tile_data_hi = read_mem8(gb, tile_address + 2 * curr_y);
        uint8_t const tile_data_lo =
            read_mem8(gb, tile_address + 2 * curr_y + 1);
        uint16_t const tile_data = (tile_data_hi << 8) | tile_data_lo;

        for (uint8_t j = 0; j < 8; j++) { // For each pixel in the row,
            uint8_t const curr_x =
                x_flip ? 7 - j : j; // flip around if necessary
            uint16_t const screen_y = start_y + curr_y;
            uint16_t const screen_x = start_x + curr_x;

            uint8_t const palette_index =
                ((tile_data >> (14 - curr_x)) & 0b10) |
                (uint1_t)(tile_data >> (7 - curr_x));
            if (screen_y < TILE_MAP_HEIGHT * TILE_HEIGHT &&
                screen_x < TILE_MAP_WIDTH * TILE_WIDTH &&
                (palette_index != 0b00 || !is_sprite_tile)) {
                uint8_t const color =
                    (read_mem8(gb, palette_address) >> (2 * palette_index)) &
                    0b11;
                gb->screen[screen_y][screen_x] = color;
            }
        }
    }
}

enum addressing_mode {
    ADDR_MODE_SIGNED = 0,
    ADDR_MODE_UNSIGNED = 1,
};

static void render_tilemap(struct gb *const gb, enum addressing_mode const addressing_mode,
                           uint16_t const tile_map,
                           uint16_t const palette_address,
                           uint8_t const origin_y, uint8_t const origin_x) {
    for (uint16_t i = 0; i < TILE_MAP_SIZE; i++) {
        if (addressing_mode == ADDR_MODE_UNSIGNED) {
            render_tile(gb, origin_y + (i / TILE_MAP_WIDTH) * TILE_HEIGHT,
                        origin_x + (i % TILE_MAP_WIDTH) * TILE_WIDTH,
                        UNSIGNED_TILE_DATA_BASE +
                            read_mem8(gb, tile_map + i) * BYTES_PER_TILE,
                        palette_address, 0, 0, 0);
        } else if (addressing_mode == ADDR_MODE_SIGNED) {
            render_tile(gb, origin_y + (i / TILE_MAP_WIDTH) * TILE_HEIGHT,
                        origin_x + (i % TILE_MAP_WIDTH) * TILE_WIDTH,
                        SIGNED_TILE_DATA_BASE +
                            (int8_t)read_mem8(gb, tile_map + i) *
                                BYTES_PER_TILE,
                        palette_address, 0, 0, 0);
        } else {
            DIE("This is impossible\n");
        }
    }
}

static void render_background(struct gb *const gb) {
    enum addressing_mode const addressing_mode = (uint1_t)(read_mem8(gb, LCD_CONTROL) >> 4);
    uint16_t const bg_map_data =
        (uint1_t)(read_mem8(gb, LCD_CONTROL) >> 3) ? TILE_MAP_2 : TILE_MAP_1;
    render_tilemap(gb, addressing_mode, bg_map_data, BGP, 0, 0);
}

static void render_window(struct gb *const gb) {
    enum addressing_mode const addressing_mode = (uint1_t)(read_mem8(gb, LCD_CONTROL) >> 4);
    uint16_t const win_map_data =
        (uint1_t)(read_mem8(gb, LCD_CONTROL) >> 6) ? TILE_MAP_2 : TILE_MAP_1;
    render_tilemap(gb, addressing_mode, win_map_data, BGP, read_mem8(gb, WY),
                   read_mem8(gb, WX) - 7);
}

static void render_sprite(struct gb *const gb, uint16_t const sprite_address) {
    uint8_t const y = read_mem8(gb, sprite_address) - 16;
    uint8_t const x = read_mem8(gb, sprite_address + 1) - 8;
    uint16_t const tile_address =
        UNSIGNED_TILE_DATA_BASE + read_mem8(gb, sprite_address + 2);
    uint8_t const attrs = read_mem8(gb, sprite_address + 3);
    uint16_t const palette_address = (uint1_t)(attrs >> 4) ? OBP1 : OBP0;
    uint1_t const x_flip = attrs >> 5;
    uint1_t const y_flip = attrs >> 6;
    // uint1_t const bg_and_window_over_obj = attrs >> 7; // Unimplemented

    render_tile(gb, y, x, tile_address, palette_address, 0, y_flip, x_flip);
}

enum sprite_size {
    SPRITE_SIZE_8x8 = 0,
    SPRITE_SIZE_8x16 = 1,
};

static void render_sprites(struct gb *const gb) {
    for (uint16_t i = 0; i < NUM_SPRITES; i++) { // for each of the 40 sprites,
        enum sprite_size const sprite_size = (uint1_t)(read_mem8(gb, LCD_CONTROL) >> 2);
        uint16_t const sprite_base_address = OAM + 4 * i;
        if (sprite_size == SPRITE_SIZE_8x16) {
            render_sprite(gb, sprite_base_address - sprite_base_address % 2);
            render_sprite(gb,
                          sprite_base_address - sprite_base_address % 2 + 1);
            i++; // Skip the next sprite because it's just part of this sprite.
        } else if (sprite_size == SPRITE_SIZE_8x8) {
            render_sprite(gb, sprite_base_address);
        } else {
            DIE("This is impossible.");
        }
    }
}

static void update_screen(struct gb *const gb) {
    // Called once per M-cycle, if the LCD is enabled.

    gb->dot_count += 16;    // Dot clock = 4 * real clock = 4 * 4 * our clock
    gb->dot_count %= 70224; // It takes 70224 dots to do one frame

    // Update the current line number
    write_mem8(gb, LY, gb->dot_count / 456); // To LY stub, comment me out.

    // STAT.2 is set iff LY=LYC, and updated constantly.
    if (read_mem8(gb, LY) == read_mem8(gb, LYC)) {
        write_mem8(gb, LCD_STATUS, read_mem8(gb, LCD_STATUS) | 0b00000100);
        if (read_mem8(gb, LCD_STATUS) & 0b01000000) {
            request_interrupt(gb, INT_STAT);
        }
    } else {
        write_mem8(gb, LCD_STATUS, read_mem8(gb, LCD_STATUS) & 0b11111011);
    }

    if (gb->dot_count >= 65664) { // vblank
        if (gb->graphics_mode != VBLANK) {
            enter_vblank(gb);
            // Draw the whole background at once upon entering vblank
            // The real thing does it line by line as it goes, but this is
            // easier
            uint8_t const lcdc = read_mem8(gb, LCD_CONTROL);
            uint1_t const window_and_bg_enabled = lcdc;
            uint1_t const window_enabled = lcdc >> 5;
            uint1_t const obj_enabled = lcdc >> 1;
            if (window_and_bg_enabled) {
                render_background(gb);
                if (window_enabled) {
                    render_window(gb);
                }
            }
            if (obj_enabled) {
                render_sprites(gb);
            }
        }
    } else if (gb->dot_count % 456 >=
               248) { // hblank (not yet allowing mode 3 extension)
        if (gb->graphics_mode != HBLANK) {
            enter_hblank(gb);
        }
    } else if (gb->dot_count % 456 >=
               80) { // transferring (not yet allowing mode 3 extension)
        if (gb->graphics_mode != TRANSFERRING) {
            enter_transferring(gb);
        }
    } else if (gb->dot_count % 456 < 80) { // searching
        if (gb->graphics_mode != SEARCHING) {
            enter_searching(gb);
        }
    }
}

void wait(struct gb *const gb) {
    uint1_t const timer_enabled = read_mem8(gb, TAC) >> 2;

    while (gb->cycles_to_wait > 0) {
        gb->cycle_count += 1;
        gb->cycles_to_wait -= 1;
        if (gb->cycle_count % CLOCKS_PER_DIVIDER_INCREMENT == 0) {
            write_mem8(gb, DIVIDER_REGISTER,
                       read_mem8(gb, DIVIDER_REGISTER) + 1);
        }

        // (our clocks, which are the true clocks / 4)
        uint8_t const clocks_per_timer_increment =
            1u << (((((read_mem8(gb, TAC) & 0b11) - 1) % 4) + 1) * 2);
        // This gives the following mapping:
        // 0 -> 256
        // 1 -> 4
        // 2 -> 16
        // 3 -> 64

        if (timer_enabled &&
            gb->cycle_count % clocks_per_timer_increment == 0) {
            if (read_mem8(gb, TIMA) == 0xFF) {
                write_mem8(
                    gb, TIMA,
                    read_mem8(gb, TMA)); // XXX:
                                         // Technically, if the last instruction
                                         // was a write to TMA, then we should
                                         // still copy the old value into TIMA.
                request_interrupt(gb, INT_TIMER);
            } else {
                write_mem8(gb, TIMA, read_mem8(gb, TIMA) + 1);
            }
        }

        if (read_mem8(gb, LCD_CONTROL) >> 7) { // If the LCD is enabled (LCDC.7)
            update_screen(gb);
        }
    }
}

static void add_a(struct gb *const gb, uint8_t const operand) {
    uint9_t const raw_result = *r_reg(gb, R_A) + operand;
    uint8_t const result = raw_result;

    uint5_t const raw_half_result =
        (uint5_t)(uint4_t)*r_reg(gb, R_A) + (uint5_t)(uint4_t)operand;
    uint4_t const half_result = raw_half_result;

    set_flag(gb, FL_C, raw_result != result);
    set_flag(gb, FL_H, raw_half_result != half_result);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 0);
    *r_reg(gb, R_A) = result;
}

static void adc_a(struct gb *const gb, uint8_t const operand) {
    uint1_t const orig_c_flag = get_flag(gb, FL_C);
    add_a(gb, operand);
    uint1_t const inter_c_flag = get_flag(gb, FL_C);
    uint1_t const inter_h_flag = get_flag(gb, FL_H);
    add_a(gb, orig_c_flag);
    set_flag(gb, FL_C, get_flag(gb, FL_C) | inter_c_flag);
    set_flag(gb, FL_H, get_flag(gb, FL_H) | inter_h_flag);
}

static void sub_a(struct gb *const gb, uint8_t const operand) {
    uint9_t const raw_result = (uint9_t)*r_reg(gb, R_A) - (uint9_t)operand;
    uint8_t const result = raw_result;

    uint5_t raw_half_result =
        (uint5_t)(uint4_t)*r_reg(gb, R_A) - (uint5_t)(uint4_t)operand;
    uint4_t half_result = raw_half_result;

    set_flag(gb, FL_C, raw_result != result);
    set_flag(gb, FL_H, raw_half_result != half_result);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 1);
    *r_reg(gb, R_A) = result;
}

static void sbc_a(struct gb *const gb, uint8_t const operand) {
    uint1_t const orig_c_flag = get_flag(gb, FL_C);
    sub_a(gb, operand);
    uint1_t const inter_c_flag = get_flag(gb, FL_C);
    uint1_t const inter_h_flag = get_flag(gb, FL_H);
    sub_a(gb, orig_c_flag);
    set_flag(gb, FL_C, get_flag(gb, FL_C) | inter_c_flag);
    set_flag(gb, FL_H, get_flag(gb, FL_H) | inter_h_flag);
}

static void and_a(struct gb *const gb, uint8_t const operand) {
    uint8_t const result = *r_reg(gb, R_A) & operand;
    set_flag(gb, FL_C, 0);
    set_flag(gb, FL_H, 1);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 0);

    *r_reg(gb, R_A) = result;
}

static void or_a(struct gb *const gb, uint8_t const operand) {
    uint8_t const result = *r_reg(gb, R_A) | operand;
    set_flag(gb, FL_C, 0);
    set_flag(gb, FL_H, 0);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 0);

    *r_reg(gb, R_A) = result;
}

static void xor_a(struct gb *const gb, uint8_t const operand) {
    uint8_t const result = *r_reg(gb, R_A) ^ operand;
    set_flag(gb, FL_C, 0);
    set_flag(gb, FL_H, 0);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 0);

    *r_reg(gb, R_A) = result;
}

static void cp_a(struct gb *const gb, uint8_t const operand) {
    uint8_t const a = *r_reg(gb, R_A);
    sub_a(gb, operand);
    *r_reg(gb, R_A) = a;
}

static uint8_t inc8(struct gb *const gb, uint8_t const operand) {
    uint8_t const result = operand + 1;

    uint5_t const raw_half_result = (uint4_t)operand + 1;
    uint4_t const half_result = raw_half_result;

    set_flag(gb, FL_H, raw_half_result != half_result);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 0);
    return result;
}

static uint8_t dec8(struct gb *const gb, uint8_t const operand) {
    uint8_t const result = operand - 1;

    uint5_t const raw_half_result = (uint4_t)operand - 1;
    uint4_t const half_result = raw_half_result;

    set_flag(gb, FL_H, raw_half_result > half_result);
    set_flag(gb, FL_Z, result == 0);
    set_flag(gb, FL_N, 1);
    return result;
}

void step(struct gb *const gb) {
    uint8_t const opcode = read_mem8(gb, gb->pc);
    uint8_t const imm8 = read_mem8(gb, gb->pc + 1);
    uint16_t const imm16 = read_mem16(gb, gb->pc + 1);
    enum r_reg const upper_r = (uint3_t)(opcode >> 3);
    enum r_reg const lower_r = (uint3_t)opcode;
    enum dd_reg const dd = (uint2_t)(opcode >> 4); // Same as ss
    enum qq_reg const qq = (uint2_t)(opcode >> 4);
    uint3_t const b = opcode >> 3;
    enum cc_cond const cc = (uint2_t)(opcode >> 3);

    if (gb->halted) {
        if (gb->cycles_to_wait == 0) {
            gb->cycles_to_wait += 1;
        }
        if (gb->need_to_do_interrupts) {
            handle_interrupts(gb);
        }
        return;
    }

    dump(gb);

    DEBUG("[0x%04" PRIX16 "]: 0x%02" PRIX8 "\t", gb->pc, opcode);
    switch (opcode) {
    case 0b01000000:
    case 0b01000001:
    case 0b01000010:
    case 0b01000011:
    case 0b01000100:
    case 0b01000101:
    case 0b01000111:
    case 0b01001000:
    case 0b01001001:
    case 0b01001010:
    case 0b01001011:
    case 0b01001100:
    case 0b01001101:
    case 0b01001111:
    case 0b01010000:
    case 0b01010001:
    case 0b01010010:
    case 0b01010011:
    case 0b01010100:
    case 0b01010101:
    case 0b01010111:
    case 0b01011000:
    case 0b01011001:
    case 0b01011010:
    case 0b01011011:
    case 0b01011100:
    case 0b01011101:
    case 0b01011111:
    case 0b01100000:
    case 0b01100001:
    case 0b01100010:
    case 0b01100011:
    case 0b01100100:
    case 0b01100101:
    case 0b01100111:
    case 0b01101000:
    case 0b01101001:
    case 0b01101010:
    case 0b01101011:
    case 0b01101100:
    case 0b01101101:
    case 0b01101111:
    case 0b01111000:
    case 0b01111001:
    case 0b01111010:
    case 0b01111011:
    case 0b01111100:
    case 0b01111101:
    case 0b01111111: {
        DEBUG("LD %s, %s\n", r_to_str(upper_r), r_to_str(lower_r));
        *r_reg(gb, upper_r) = *r_reg(gb, lower_r);
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00000110:
    case 0b00001110:
    case 0b00010110:
    case 0b00011110:
    case 0b00100110:
    case 0b00101110:
    case 0b00111110: {
        DEBUG("LD %s, %" PRIu8 "\n", r_to_str(upper_r), imm8);
        *r_reg(gb, upper_r) = imm8;
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b01000110:
    case 0b01001110:
    case 0b01010110:
    case 0b01011110:
    case 0b01100110:
    case 0b01101110:
    case 0b01111110: {
        DEBUG("LD %s, (HL)\n", r_to_str(upper_r));
        *r_reg(gb, upper_r) = read_mem8(gb, gb->hl);
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b01110000:
    case 0b01110001:
    case 0b01110010:
    case 0b01110011:
    case 0b01110100:
    case 0b01110101:
    case 0b01110111: {
        DEBUG("LD (HL), %s\n", r_to_str(lower_r));
        write_mem8(gb, gb->hl, *r_reg(gb, lower_r));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00110110: {
        DEBUG("LD (HL), %" PRIu8 "\n", imm8);
        write_mem8(gb, gb->hl, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b00001010: {
        DEBUG("LD A, (BC)\n");
        *r_reg(gb, R_A) = read_mem8(gb, gb->bc);
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00011010: {
        DEBUG("LD A, (DE)\n");
        *r_reg(gb, R_A) = read_mem8(gb, gb->de);
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b11110010: {
        DEBUG("LD A, (C)\n");
        *r_reg(gb, R_A) = read_mem8(gb, 0xff00 | *r_reg(gb, R_C));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b11100010: {
        DEBUG("LD (C), A\n");
        write_mem8(gb, 0xff00 | *r_reg(gb, R_C), *r_reg(gb, R_A));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b11110000: {
        DEBUG("LD A, (0xFF%02" PRIX8 ")\n", imm8);
        *r_reg(gb, R_A) = read_mem8(gb, 0xff00 | imm8);
        gb->cycles_to_wait += 3;
        gb->pc += 2;
        break;
    }
    case 0b11100000: {
        DEBUG("LD (0xFF%02" PRIX8 "), A\n", imm8);
        write_mem8(gb, 0xff00 | imm8, *r_reg(gb, R_A));
        gb->cycles_to_wait += 3;
        gb->pc += 2;
        break;
    }
    case 0b11111010: {
        DEBUG("LD A, (0x%04" PRIX16 ")\n", imm16);
        *r_reg(gb, R_A) = read_mem8(gb, imm16);
        gb->cycles_to_wait += 4;
        gb->pc += 3;
        break;
    }
    case 0b11101010: {
        DEBUG("LD (0x%04" PRIX16 "), A\n", imm16);
        write_mem8(gb, imm16, *r_reg(gb, R_A));
        gb->cycles_to_wait += 4;
        gb->pc += 3;
        break;
    }
    case 0b00101010: {
        DEBUG("LD A, (HLI)\n");
        *r_reg(gb, R_A) = read_mem8(gb, gb->hl);
        gb->hl++;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00111010: {
        DEBUG("LD A, (HLD)\n");
        *r_reg(gb, R_A) = read_mem8(gb, gb->hl);
        gb->hl--;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00000010: {
        DEBUG("LD (BC), A\n");
        write_mem8(gb, gb->bc, *r_reg(gb, R_A));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00010010: {
        DEBUG("LD (DE), A\n");
        write_mem8(gb, gb->de, *r_reg(gb, R_A));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00100010: {
        DEBUG("LD (HLI), A\n");
        write_mem8(gb, gb->hl, *r_reg(gb, R_A));
        gb->hl++;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00110010: {
        DEBUG("LD (HLD), A\n");
        write_mem8(gb, gb->hl, *r_reg(gb, R_A));
        gb->hl--;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00000001:
    case 0b00010001:
    case 0b00100001:
    case 0b00110001: {
        DEBUG("LD %s, 0x%04" PRIX16 "\n", dd_to_str(dd), imm16);
        *dd_reg(gb, dd) = imm16;
        gb->cycles_to_wait += 3;
        gb->pc += 3;
        break;
    }
    case 0b11111001: {
        DEBUG("LD SP, HL\n");
        gb->sp = gb->hl;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b11000101:
    case 0b11010101:
    case 0b11100101:
    case 0b11110101: {
        DEBUG("PUSH %s\n", qq_to_str(qq));
        write_mem16(gb, gb->sp - 2, *qq_reg(gb, qq));
        gb->sp -= 2;
        gb->cycles_to_wait += 4;
        gb->pc++;
        break;
    }
    case 0b11000001:
    case 0b11010001:
    case 0b11100001:
    case 0b11110001: {
        DEBUG("POP %s\n", qq_to_str(qq));
        *qq_reg(gb, qq) =
            (read_mem8(gb, gb->sp + 1) << 8) | read_mem8(gb, gb->sp);
        gb->af &= 0xfff0; // the low bits of the flags can't be set
        gb->sp += 2;
        gb->cycles_to_wait += 3;
        gb->pc++;
        break;
    }
    case 0b11111000: {
        DEBUG("LDHL SP, %" PRIi8 "\n", imm8);
        uint9_t const raw_byte_result =
            (uint9_t)(uint8_t)gb->sp + (uint9_t)imm8;
        uint8_t const byte_result = raw_byte_result;

        uint5_t const raw_half_result =
            (uint5_t)(uint4_t)gb->sp + (uint5_t)(uint4_t)imm8;
        uint4_t const half_result = raw_half_result;

        set_flag(gb, FL_H, raw_half_result != half_result);
        set_flag(gb, FL_C, raw_byte_result != byte_result);
        set_flag(gb, FL_N, 0);
        set_flag(gb, FL_Z, 0);

        gb->hl = gb->sp + (int8_t)imm8;
        gb->cycles_to_wait += 3;
        gb->pc += 2;
        break;
    }
    case 0b00001000: {
        DEBUG("LD (0x%" PRIX16 "), SP\n", imm16);
        write_mem16(gb, imm16, gb->sp);
        gb->cycles_to_wait += 5;
        gb->pc += 3;
        break;
    }
    case 0b10000111:
    case 0b10000000:
    case 0b10000001:
    case 0b10000010:
    case 0b10000011:
    case 0b10000100:
    case 0b10000101: {
        DEBUG("ADD A, %s\n", r_to_str(lower_r));
        add_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11000110: {
        DEBUG("ADD A, %" PRIu8 "\n", imm8);
        add_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10000110: {
        DEBUG("ADD A, (HL)\n");
        add_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b10001111:
    case 0b10001000:
    case 0b10001001:
    case 0b10001010:
    case 0b10001011:
    case 0b10001100:
    case 0b10001101: {
        DEBUG("ADC A, %s\n", r_to_str(lower_r));
        adc_a(gb, *r_reg(gb, lower_r));
        gb->pc++;
        break;
    }
    case 0b11001110: {
        DEBUG("ADC A, %" PRIu8 "\n", imm8);
        adc_a(gb, imm8);
        gb->pc += 2;
        break;
    }
    case 0b10001110: {
        DEBUG("ADC A, (HL)\n");
        adc_a(gb, read_mem8(gb, gb->hl));
        gb->pc++;
        break;
    }
    case 0b10010111:
    case 0b10010000:
    case 0b10010001:
    case 0b10010010:
    case 0b10010011:
    case 0b10010100:
    case 0b10010101: {
        DEBUG("SUB A, %s\n", r_to_str(lower_r));
        sub_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b11010110: {
        DEBUG("SUB A, %" PRIu8 "\n", imm8);
        sub_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10010110: {
        DEBUG("SUB A, (HL)\n");
        sub_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b10011111:
    case 0b10011000:
    case 0b10011001:
    case 0b10011010:
    case 0b10011011:
    case 0b10011100:
    case 0b10011101: {
        DEBUG("SBC A, %s\n", r_to_str(lower_r));
        sbc_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11011110: {
        DEBUG("SBC A, %" PRIu8 "\n", imm8);
        sbc_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10011110: {
        DEBUG("SBC A, (HL)\n");
        sbc_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b10100111:
    case 0b10100000:
    case 0b10100001:
    case 0b10100010:
    case 0b10100011:
    case 0b10100100:
    case 0b10100101: {
        DEBUG("AND A, %s\n", r_to_str(lower_r));
        and_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11100110: {
        DEBUG("AND A, %" PRIu8 "\n", imm8);
        and_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10100110: {
        DEBUG("AND A, (HL)\n");
        and_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b10110111:
    case 0b10110000:
    case 0b10110001:
    case 0b10110010:
    case 0b10110011:
    case 0b10110100:
    case 0b10110101: {
        DEBUG("OR A, %s\n", r_to_str(lower_r));
        or_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11110110: {
        DEBUG("OR A, %" PRIu8 "\n", imm8);
        or_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10110110: {
        DEBUG("OR A, (HL)\n");
        or_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b10101111:
    case 0b10101000:
    case 0b10101001:
    case 0b10101010:
    case 0b10101011:
    case 0b10101100:
    case 0b10101101: {
        DEBUG("XOR A, %s\n", r_to_str(lower_r));
        xor_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11101110: {
        DEBUG("XOR A, %" PRIu8 "\n", imm8);
        xor_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10101110: {
        DEBUG("XOR A, (HL)\n");
        xor_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b10111111:
    case 0b10111000:
    case 0b10111001:
    case 0b10111010:
    case 0b10111011:
    case 0b10111100:
    case 0b10111101: {
        DEBUG("CP A, %s\n", r_to_str(lower_r));
        cp_a(gb, *r_reg(gb, lower_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11111110: {
        DEBUG("CP A, %" PRIu8 "\n", imm8);
        cp_a(gb, imm8);
        gb->cycles_to_wait += 2;
        gb->pc += 2;
        break;
    }
    case 0b10111110: {
        DEBUG("CP A, (HL)\n");
        cp_a(gb, read_mem8(gb, gb->hl));
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00111100:
    case 0b00000100:
    case 0b00001100:
    case 0b00010100:
    case 0b00011100:
    case 0b00100100:
    case 0b00101100: {
        DEBUG("INC %s\n", r_to_str(upper_r));
        *r_reg(gb, upper_r) = inc8(gb, *r_reg(gb, upper_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00110100: {
        DEBUG("INC (HL)\n");
        write_mem8(gb, gb->hl, inc8(gb, read_mem8(gb, gb->hl)));
        gb->cycles_to_wait += 3;
        gb->pc++;
        break;
    }
    case 0b00111101:
    case 0b00000101:
    case 0b00001101:
    case 0b00010101:
    case 0b00011101:
    case 0b00100101:
    case 0b00101101: {
        DEBUG("DEC %s\n", r_to_str(upper_r));
        *r_reg(gb, upper_r) = dec8(gb, *r_reg(gb, upper_r));
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00110101: {
        DEBUG("DEC (HL)\n");
        write_mem8(gb, gb->hl, dec8(gb, read_mem8(gb, gb->hl)));
        gb->cycles_to_wait += 3;
        gb->pc++;
        break;
    }
    case 0b00001001:
    case 0b00011001:
    case 0b00101001:
    case 0b00111001: {
        DEBUG("ADD HL, %s\n", dd_to_str(dd));
        uint17_t const raw_result =
            (uint17_t)gb->hl + (uint17_t)*dd_reg(gb, dd);
        uint16_t const result = raw_result;

        uint13_t const raw_half_result =
            (uint13_t)(uint12_t)gb->hl + (uint13_t)(uint12_t)*dd_reg(gb, dd);
        uint12_t const half_result = raw_half_result;

        set_flag(gb, FL_H, raw_half_result != half_result);
        set_flag(gb, FL_C, raw_result != result);
        set_flag(gb, FL_N, 0);
        gb->hl = result;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b11101000: {
        DEBUG("ADD SP, %" PRIi8 "\n", imm8);
        uint9_t const raw_byte_result =
            (uint9_t)(uint8_t)gb->sp + (uint9_t)imm8;
        uint8_t const byte_result = raw_byte_result;

        uint5_t const raw_half_result =
            (uint5_t)(uint4_t)gb->sp + (uint5_t)(uint4_t)imm8;
        uint4_t const half_result = raw_half_result;

        set_flag(gb, FL_H, raw_half_result != half_result);
        set_flag(gb, FL_C, raw_byte_result != byte_result);
        set_flag(gb, FL_N, 0);
        set_flag(gb, FL_Z, 0);
        gb->sp += (int8_t)imm8;
        gb->cycles_to_wait += 4;
        gb->pc += 2;
        break;
    }
    case 0b00000011:
    case 0b00010011:
    case 0b00100011:
    case 0b00110011: {
        DEBUG("INC %s\n", dd_to_str(dd));
        *dd_reg(gb, dd) += 1;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00001011:
    case 0b00011011:
    case 0b00101011:
    case 0b00111011: {
        DEBUG("DEC %s\n", dd_to_str(dd));
        *dd_reg(gb, dd) -= 1;
        gb->cycles_to_wait += 2;
        gb->pc++;
        break;
    }
    case 0b00000111: {
        DEBUG("RLCA\n");
        uint8_t result = (*r_reg(gb, R_A) << 1) | (*r_reg(gb, R_A) >> 7);
        set_flag(gb, FL_C, result);
        set_flag(gb, FL_H, 0);
        set_flag(gb, FL_N, 0);
        set_flag(gb, FL_Z, 0);
        *r_reg(gb, R_A) = result;
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00010111: {
        DEBUG("RLA\n");
        uint8_t result = (*r_reg(gb, R_A) << 1) | get_flag(gb, FL_C);
        set_flag(gb, FL_C, *r_reg(gb, R_A) >> 7);
        set_flag(gb, FL_H, 0);
        set_flag(gb, FL_N, 0);
        set_flag(gb, FL_Z, 0);
        *r_reg(gb, R_A) = result;
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00001111: {
        DEBUG("RRCA\n");
        uint8_t result = (*r_reg(gb, R_A) << 7) | (*r_reg(gb, R_A) >> 1);
        set_flag(gb, FL_C, *r_reg(gb, R_A));
        set_flag(gb, FL_H, 0);
        set_flag(gb, FL_N, 0);
        set_flag(gb, FL_Z, 0);
        *r_reg(gb, R_A) = result;
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00011111: {
        DEBUG("RRA\n");
        uint8_t result =
            ((uint8_t)get_flag(gb, FL_C) << 7) | (*r_reg(gb, R_A) >> 1);
        set_flag(gb, FL_C, *r_reg(gb, R_A));
        set_flag(gb, FL_H, 0);
        set_flag(gb, FL_N, 0);
        set_flag(gb, FL_Z, 0);
        *r_reg(gb, R_A) = result;
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11001011: {
        enum r_reg const cb_r = (uint3_t)imm8;
        uint3_t const cb_b = imm8 >> 3;
        switch (imm8) {
        case 0b00000111:
        case 0b00000000:
        case 0b00000001:
        case 0b00000010:
        case 0b00000011:
        case 0b00000100:
        case 0b00000101: {
            DEBUG("RLC %s\n", r_to_str(cb_r));
            uint8_t const result =
                (*r_reg(gb, cb_r) << 1) | (*r_reg(gb, cb_r) >> 7);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, *r_reg(gb, cb_r) >> 7);
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00000110: {
            DEBUG("RLC (HL)\n");
            uint8_t const result =
                (read_mem8(gb, gb->hl) << 1) | (read_mem8(gb, gb->hl) >> 7);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_C, read_mem8(gb, gb->hl) >> 7);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00001111:
        case 0b00001000:
        case 0b00001001:
        case 0b00001010:
        case 0b00001011:
        case 0b00001100:
        case 0b00001101: {
            DEBUG("RRC %s\n", r_to_str(cb_r));
            uint8_t const result =
                (*r_reg(gb, cb_r) << 7) | (*r_reg(gb, cb_r) >> 1);
            set_flag(gb, FL_C, *r_reg(gb, cb_r));
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00001110: {
            DEBUG("RRC (HL)\n");
            uint8_t const result =
                (read_mem8(gb, gb->hl) << 7) | (read_mem8(gb, gb->hl) >> 1);
            set_flag(gb, FL_C, read_mem8(gb, gb->hl));
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00010111:
        case 0b00010000:
        case 0b00010001:
        case 0b00010010:
        case 0b00010011:
        case 0b00010100:
        case 0b00010101: {
            DEBUG("RL %s\n", r_to_str(cb_r));
            uint8_t const result =
                ((*r_reg(gb, cb_r)) << 1) | get_flag(gb, FL_C);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, (*r_reg(gb, cb_r)) >> 7);
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00010110: {
            DEBUG("RL (HL)\n");
            uint8_t const result =
                (read_mem8(gb, gb->hl) << 1) | get_flag(gb, FL_C);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, read_mem8(gb, gb->hl) >> 7);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00011111:
        case 0b00011000:
        case 0b00011001:
        case 0b00011010:
        case 0b00011011:
        case 0b00011100:
        case 0b00011101: {
            DEBUG("RR %s\n", r_to_str(cb_r));
            uint8_t const result =
                ((uint8_t)get_flag(gb, FL_C) << 7) | ((*r_reg(gb, cb_r)) >> 1);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, *r_reg(gb, cb_r));
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00011110: {
            DEBUG("RR (HL)\n");
            uint8_t const curr = read_mem8(gb, gb->hl);
            uint8_t const result =
                ((uint8_t)get_flag(gb, FL_C) << 7) | (curr >> 1);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, curr);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00100111:
        case 0b00100000:
        case 0b00100001:
        case 0b00100010:
        case 0b00100011:
        case 0b00100100:
        case 0b00100101: {
            DEBUG("SLA %s\n", r_to_str(cb_r));
            uint8_t const result = (*r_reg(gb, cb_r)) << 1;
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, (*r_reg(gb, cb_r)) >> 7);
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00100110: {
            DEBUG("SLA (HL)\n");
            uint8_t const result = read_mem8(gb, gb->hl) << 1;
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, read_mem8(gb, gb->hl) >> 7);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00101111:
        case 0b00101000:
        case 0b00101001:
        case 0b00101010:
        case 0b00101011:
        case 0b00101100:
        case 0b00101101: {
            DEBUG("SRA %s\n", r_to_str(cb_r));
            uint8_t const result =
                ((*r_reg(gb, cb_r)) & 0b10000000) | (*r_reg(gb, cb_r) >> 1);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, *r_reg(gb, cb_r));
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00101110: {
            DEBUG("SRA (HL)\n");
            uint8_t const result = (read_mem8(gb, gb->hl) & 0b10000000) |
                                   (read_mem8(gb, gb->hl) >> 1);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            set_flag(gb, FL_C, read_mem8(gb, gb->hl));
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00110111:
        case 0b00110000:
        case 0b00110001:
        case 0b00110010:
        case 0b00110011:
        case 0b00110100:
        case 0b00110101: {
            DEBUG("SWAP %s\n", r_to_str(cb_r));
            uint8_t const result =
                ((*r_reg(gb, cb_r)) << 4) | ((*r_reg(gb, cb_r)) >> 4);
            set_flag(gb, FL_C, 0);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00110110: {
            DEBUG("SWAP (HL)\n");
            uint8_t const result =
                (read_mem8(gb, gb->hl) << 4) | (read_mem8(gb, gb->hl) >> 4);
            set_flag(gb, FL_C, 0);
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b00111111:
        case 0b00111000:
        case 0b00111001:
        case 0b00111010:
        case 0b00111011:
        case 0b00111100:
        case 0b00111101: {
            DEBUG("SRL %s\n", r_to_str(cb_r));
            uint8_t const result = *r_reg(gb, cb_r) >> 1;
            set_flag(gb, FL_C, *r_reg(gb, cb_r));
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            *r_reg(gb, cb_r) = result;
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b00111110: {
            DEBUG("SRL (HL)\n");
            uint8_t const result = read_mem8(gb, gb->hl) >> 1;
            set_flag(gb, FL_C, read_mem8(gb, gb->hl));
            set_flag(gb, FL_H, 0);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, result == 0);
            write_mem8(gb, gb->hl, result);
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b01000111:
        case 0b01000000:
        case 0b01000001:
        case 0b01000010:
        case 0b01000011:
        case 0b01000100:
        case 0b01000101:
        case 0b01001111:
        case 0b01001000:
        case 0b01001001:
        case 0b01001010:
        case 0b01001011:
        case 0b01001100:
        case 0b01001101:
        case 0b01010111:
        case 0b01010000:
        case 0b01010001:
        case 0b01010010:
        case 0b01010011:
        case 0b01010100:
        case 0b01010101:
        case 0b01011111:
        case 0b01011000:
        case 0b01011001:
        case 0b01011010:
        case 0b01011011:
        case 0b01011100:
        case 0b01011101:
        case 0b01100111:
        case 0b01100000:
        case 0b01100001:
        case 0b01100010:
        case 0b01100011:
        case 0b01100100:
        case 0b01100101:
        case 0b01101111:
        case 0b01101000:
        case 0b01101001:
        case 0b01101010:
        case 0b01101011:
        case 0b01101100:
        case 0b01101101:
        case 0b01110111:
        case 0b01110000:
        case 0b01110001:
        case 0b01110010:
        case 0b01110011:
        case 0b01110100:
        case 0b01110101:
        case 0b01111111:
        case 0b01111000:
        case 0b01111001:
        case 0b01111010:
        case 0b01111011:
        case 0b01111100:
        case 0b01111101: {
            DEBUG("BIT %u, %s\n", (unsigned int)cb_b, r_to_str(cb_r));
            set_flag(gb, FL_H, 1);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, ~((*r_reg(gb, cb_r)) >> cb_b));
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b01000110:
        case 0b01001110:
        case 0b01010110:
        case 0b01011110:
        case 0b01100110:
        case 0b01101110:
        case 0b01110110:
        case 0b01111110: {
            DEBUG("BIT %u, (HL)\n", (unsigned int)cb_b);
            set_flag(gb, FL_H, 1);
            set_flag(gb, FL_N, 0);
            set_flag(gb, FL_Z, ~(read_mem8(gb, gb->hl) >> cb_b));
            gb->cycles_to_wait += 3;
            gb->pc += 2;
            break;
        }
        case 0b10000111:
        case 0b10000000:
        case 0b10000001:
        case 0b10000010:
        case 0b10000011:
        case 0b10000100:
        case 0b10000101:
        case 0b10001111:
        case 0b10001000:
        case 0b10001001:
        case 0b10001010:
        case 0b10001011:
        case 0b10001100:
        case 0b10001101:
        case 0b10010111:
        case 0b10010000:
        case 0b10010001:
        case 0b10010010:
        case 0b10010011:
        case 0b10010100:
        case 0b10010101:
        case 0b10011111:
        case 0b10011000:
        case 0b10011001:
        case 0b10011010:
        case 0b10011011:
        case 0b10011100:
        case 0b10011101:
        case 0b10100111:
        case 0b10100000:
        case 0b10100001:
        case 0b10100010:
        case 0b10100011:
        case 0b10100100:
        case 0b10100101:
        case 0b10101111:
        case 0b10101000:
        case 0b10101001:
        case 0b10101010:
        case 0b10101011:
        case 0b10101100:
        case 0b10101101:
        case 0b10110111:
        case 0b10110000:
        case 0b10110001:
        case 0b10110010:
        case 0b10110011:
        case 0b10110100:
        case 0b10110101:
        case 0b10111111:
        case 0b10111000:
        case 0b10111001:
        case 0b10111010:
        case 0b10111011:
        case 0b10111100:
        case 0b10111101: {
            DEBUG("RES %u, %s\n", (unsigned int)cb_b, r_to_str(cb_r));
            *r_reg(gb, cb_r) &= ~(1u << cb_b);
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b10000110:
        case 0b10001110:
        case 0b10010110:
        case 0b10011110:
        case 0b10100110:
        case 0b10101110:
        case 0b10110110:
        case 0b10111110: {
            DEBUG("RES %u, (HL)\n", (unsigned int)cb_b);
            write_mem8(gb, gb->hl, read_mem8(gb, gb->hl) & ~(1u << cb_b));
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        case 0b11000111:
        case 0b11000000:
        case 0b11000001:
        case 0b11000010:
        case 0b11000011:
        case 0b11000100:
        case 0b11000101:
        case 0b11001111:
        case 0b11001000:
        case 0b11001001:
        case 0b11001010:
        case 0b11001011:
        case 0b11001100:
        case 0b11001101:
        case 0b11010111:
        case 0b11010000:
        case 0b11010001:
        case 0b11010010:
        case 0b11010011:
        case 0b11010100:
        case 0b11010101:
        case 0b11011111:
        case 0b11011000:
        case 0b11011001:
        case 0b11011010:
        case 0b11011011:
        case 0b11011100:
        case 0b11011101:
        case 0b11100111:
        case 0b11100000:
        case 0b11100001:
        case 0b11100010:
        case 0b11100011:
        case 0b11100100:
        case 0b11100101:
        case 0b11101111:
        case 0b11101000:
        case 0b11101001:
        case 0b11101010:
        case 0b11101011:
        case 0b11101100:
        case 0b11101101:
        case 0b11110111:
        case 0b11110000:
        case 0b11110001:
        case 0b11110010:
        case 0b11110011:
        case 0b11110100:
        case 0b11110101:
        case 0b11111111:
        case 0b11111000:
        case 0b11111001:
        case 0b11111010:
        case 0b11111011:
        case 0b11111100:
        case 0b11111101: {
            DEBUG("SET %u, %s\n", (unsigned int)cb_b, r_to_str(cb_r));
            *r_reg(gb, cb_r) |= (1u << cb_b);
            gb->cycles_to_wait += 2;
            gb->pc += 2;
            break;
        }
        case 0b11000110:
        case 0b11001110:
        case 0b11010110:
        case 0b11011110:
        case 0b11100110:
        case 0b11101110:
        case 0b11110110:
        case 0b11111110: {
            DEBUG("SET %u, (HL)\n", (unsigned int)cb_b);
            write_mem8(gb, gb->hl, read_mem8(gb, gb->hl) | (1u << cb_b));
            gb->cycles_to_wait += 4;
            gb->pc += 2;
            break;
        }
        default: {
            DIE("Unrecognized CB opcode 0x%02" PRIX8 "!\n", imm8);
        }
        }
        break;
    }
    case 0b11000011: {
        DEBUG("JP 0x%04" PRIX16 "\n", imm16);
        gb->cycles_to_wait += 4;
        gb->pc = imm16;
        break;
    }
    case 0b11000010:
    case 0b11001010:
    case 0b11010010:
    case 0b11011010: {
        DEBUG("JP %s, 0x%04" PRIX16 "\n", cc_to_str(cc), imm16);
        if (check_cc(gb, cc)) {
            gb->cycles_to_wait += 4;
            gb->pc = imm16;
        } else {
            gb->cycles_to_wait += 3;
            gb->pc += 3;
        }
        break;
    }
    case 0b00011000: {
        DEBUG("JR %" PRIi8 "\n", (int16_t)(int8_t)imm8 + 2);
        gb->cycles_to_wait += 3;
        gb->pc += (int16_t)(int8_t)imm8 + 2;
        break;
    }
    case 0b00100000:
    case 0b00101000:
    case 0b00110000:
    case 0b00111000: {
        DEBUG("JR %s, %" PRIi8 "\n", cc_to_str(cc), (int16_t)(int8_t)imm8 + 2);
        if (check_cc(gb, cc)) {
            gb->cycles_to_wait += 3;
            gb->pc += (int16_t)(int8_t)imm8 + 2;
        } else {
            gb->cycles_to_wait += 2;
            gb->pc += 2;
        }
        break;
    }
    case 0b11101001: {
        DEBUG("JP (HL)\n");
        gb->pc = gb->hl;
        gb->cycles_to_wait++;
        break;
    }
    case 0b11001101: {
        DEBUG("CALL 0x%04" PRIX16 "\n", imm16);
        gb->sp -= 2;
        write_mem16(gb, gb->sp, gb->pc + 3);
        gb->pc = imm16;
        gb->cycles_to_wait += 6;
        break;
    }
    case 0b11000100:
    case 0b11001100:
    case 0b11010100:
    case 0b11011100: {
        DEBUG("CALL %s, 0x%04" PRIX16 "\n", cc_to_str(cc), imm16);
        if (check_cc(gb, cc)) {
            gb->sp -= 2;
            write_mem16(gb, gb->sp, gb->pc + 3);
            gb->pc = imm16;
            gb->cycles_to_wait += 6;
        } else {
            gb->pc += 3;
            gb->cycles_to_wait += 3;
        }
        break;
    }
    case 0b11001001: {
        DEBUG("RET\n");
        gb->pc = read_mem16(gb, gb->sp);
        gb->sp += 2;
        gb->cycles_to_wait += 4;
        break;
    }
    case 0b11011001: {
        DEBUG("RETI\n");
        gb->pc = read_mem16(gb, gb->sp);
        gb->sp += 2;
        gb->ime = 1;
        gb->need_to_do_interrupts = 1;
        gb->cycles_to_wait += 4;
        break;
    }
    case 0b11000000:
    case 0b11001000:
    case 0b11010000:
    case 0b11011000: {
        DEBUG("RET %s\n", cc_to_str(cc));
        if (check_cc(gb, cc)) {
            gb->pc = read_mem16(gb, gb->sp);
            gb->sp += 2;
            gb->cycles_to_wait += 5;
        } else {
            gb->pc++;
            gb->cycles_to_wait += 2;
        }
        break;
    }
    case 0b11000111:
    case 0b11001111:
    case 0b11010111:
    case 0b11011111:
    case 0b11100111:
    case 0b11101111:
    case 0b11110111:
    case 0b11111111: {
        DEBUG("RST %u\n", (unsigned int)b);
        write_mem16(gb, gb->sp - 2, gb->pc + 1);
        gb->sp -= 2;
        gb->cycles_to_wait += 4;
        gb->pc = (uint16_t)b * 8;
        break;
    }
    case 0b00100111: {
        DEBUG("DAA\n");
        uint1_t const c_contents = get_flag(gb, FL_C);
        uint1_t const h_contents = get_flag(gb, FL_H);
        uint1_t const n_contents = get_flag(gb, FL_N);
        uint8_t const a_contents = *r_reg(gb, R_A);

        int8_t addend = 0;
        uint1_t carry = c_contents;

        if (n_contents) { // Subtraction
            if (c_contents) {
                addend -= 0x60;
            }
            if (h_contents) {
                addend -= 0x6;
            }
        } else { // Addition
            if (c_contents || a_contents > 0x99) {
                addend += 0x60;
                carry = 1;
            }
            if (h_contents || (a_contents & 0b1111) > 0x9) {
                addend += 0x6;
            }
        }

        set_flag(gb, FL_C, carry);
        set_flag(gb, FL_H, 0);
        *r_reg(gb, R_A) = a_contents + addend;
        set_flag(gb, FL_Z, *r_reg(gb, R_A) == 0);
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00101111: {
        DEBUG("CPL\n");
        *r_reg(gb, R_A) = ~*r_reg(gb, R_A);
        set_flag(gb, FL_H, 1);
        set_flag(gb, FL_N, 1);
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00000000: {
        DEBUG("NOP\n");
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00111111: {
        DEBUG("CCF\n");
        set_flag(gb, FL_C, !get_flag(gb, FL_C));
        set_flag(gb, FL_H, 0);
        set_flag(gb, FL_N, 0);
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b00110111: {
        DEBUG("SCF\n");
        set_flag(gb, FL_C, 1);
        set_flag(gb, FL_H, 0);
        set_flag(gb, FL_N, 0);
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11110011: {
        DEBUG("DI\n");
        gb->ime = 0;
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b11111011: {
        DEBUG("EI\n");
        // XXX: The effect of EI should actually be delayed by one cycle (so EI
        // DI should not allow any interrupts)
        gb->ime = 1;
        gb->need_to_do_interrupts = 1;
        gb->cycles_to_wait++;
        gb->pc++;
        break;
    }
    case 0b01110110: {
        DEBUG("HALT\n");
        gb->halted = 1;
        gb->cycles_to_wait += 1;
        gb->pc++;
        break;
    }
    case 0b00010000: {
        DEBUG("STOP\n");
        write_mem8(gb, DIVIDER_REGISTER, 0);
        gb->cycles_to_wait += 1;
        gb->pc += 2;
        gb->halted = 1;
        break;
    }
    default: {
        DIE("Unrecognized opcode 0x%02" PRIX8 "!\n", opcode);
    }
    }

    if (gb->need_to_do_interrupts) {
        handle_interrupts(gb);
    }
}
