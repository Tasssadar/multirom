#ifndef TETRIS_H
#define TETRIS_H

#define BLOCK_SIZE 20
#define LINES_LEVEL 7
#define TETRIS_BORDER_TOP    -2
#define TETRIS_BORDER_LEFT   -3
#define TETRIS_BORDER_RIGHT  -4
#define TETRIS_BORDER_BOTTOM -5

#define TETRIS_W 11
#define TETRIS_H 22

#define TETRIS_STARTED   0x01
#define TETRIS_SPAWN_NEW 0x02
#define TETRIS_FINISHED  0x04

#define TETRIS_PIECE_I 0
#define TETRIS_PIECE_J 1
#define TETRIS_PIECE_L 2
#define TETRIS_PIECE_O 3
#define TETRIS_PIECE_S 4
#define TETRIS_PIECE_T 5
#define TETRIS_PIECE_Z 6

#define TETRIS_DOWN      1
#define TETRIS_LEFT      2
#define TETRIS_RIGHT     3
#define TETRIS_DOWN_FAST 4

typedef struct
{
    char id;
    char type;
    char rotation;
    uint16_t color;
    uint16_t x;
    uint16_t y;
    char moved;
} tetris_piece;

void tetris_init();
void *tetris_thread(void *cookie);
void tetris_key(int key);
void tetris_spawn_new();
void tetris_draw(unsigned char move);
void tetris_exit();
unsigned char tetris_can_move_piece(tetris_piece *p, char dir);
void tetris_check_line();
inline void tetris_clear(char do_free);
inline void tetris_set_defaults();
inline uint16_t tetris_get_color_for_type(char type);
inline void tetris_move_piece(tetris_piece *p, char dir);
inline void tetris_rotate_piece();
inline void tetris_delete_if_nowhere(tetris_piece *p);
inline void tetris_set_piece(tetris_piece *p, tetris_piece *val);

static const uint8_t p_shape[7][4][5][5] =
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