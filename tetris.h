#ifndef _TETRIS_H
#define _TETRIS_H

#define BLOCK_SIZE 20
#define LINES_LEVEL 7 // lines needed to proceed to next level
#define PIECE_BLOCKS 5 // Piece is 5x5 grid

#define TETRIS_W 11 // 11x22 blocks grid
#define TETRIS_H 22

enum _borders
{
    TETRIS_BORDER_TOP    = -2,
    TETRIS_BORDER_LEFT   = -3,
    TETRIS_BORDER_RIGHT  = -4,
    TETRIS_BORDER_BOTTOM = -5,
};

enum _state
{
    TETRIS_STARTED   = 0x01,
    TETRIS_SPAWN_NEW = 0x02,
    TETRIS_FINISHED  = 0x04,
    TETRIS_PAUSED    = 0x08,
};

enum _pieces
{
    TETRIS_PIECE_I = 0,
    TETRIS_PIECE_J,
    TETRIS_PIECE_L,
    TETRIS_PIECE_O,
    TETRIS_PIECE_S,
    TETRIS_PIECE_T,
    TETRIS_PIECE_Z,
    TETRIS_PIECE_MAX,
};

enum _direction
{
    TETRIS_DOWN = 1,
    TETRIS_LEFT,
    TETRIS_RIGHT,
    TETRIS_DOWN_FAST,
    TETRIS_UP,
};

typedef struct
{
    uint8_t type;
    uint8_t rotation;
    uint8_t moved;
    uint16_t x;
    uint16_t y;
} tetris_piece;

void tetris_init(void);
void *tetris_thread(void *cookie);
void tetris_key(int key);
void tetris_spawn_new(void);
void tetris_draw(uint8_t move);
void tetris_exit(void);
uint8_t tetris_can_move_piece(uint8_t dir);
void tetris_check_line(void);
int8_t tetris_check_border(uint8_t dir);
void tetris_set_defaults(void);
uint16_t tetris_get_color_for_type(uint8_t type);
void tetris_rotate_piece(void);
void tetris_delete_if_nowhere(tetris_piece *p);
void tetris_set_piece(tetris_piece *p, tetris_piece *val);
inline void tetris_clear(uint8_t do_free);
inline void tetris_move_piece(uint8_t dir);
inline void tetris_print_score(void);
void tetris_print_batt(void);

static const uint8_t p_shape[TETRIS_PIECE_MAX][4][PIECE_BLOCKS][PIECE_BLOCKS] =
{
    // I
    {
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 2, 1, 1}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 2, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 1, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {1, 1, 2, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 1, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 2, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} }
    },

    // J
    {
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 2, 1, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 2, 0, 0}, {0, 1, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 1, 2, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 1, 0}, {0, 0, 2, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} }
    },

    // L
    {
        { {0, 0, 0, 0, 0}, {0, 0, 0, 1, 0}, {0, 1, 2, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 2, 0, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 2, 1, 0}, {0, 1, 0, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 1, 1, 0, 0}, {0, 0, 2, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} }
    },

    // O
    {
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0} }
    },

    // S
    {
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 2, 1, 0}, {0, 1, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 1, 2, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 1, 0}, {0, 1, 2, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} }
    },

    // T
    {
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 2, 1, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 1, 2, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 1, 2, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} }
    },

    // Z
    {
        { {0, 0, 0, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 2, 1, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 2, 0, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 1, 2, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 0, 0, 0} },
        { {0, 0, 0, 0, 0}, {0, 1, 1, 0, 0}, {0, 0, 2, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0} }
    }
};

#endif