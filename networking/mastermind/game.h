#ifndef MASTERMIND_GAME_H
#define MASTERMIND_GAME_H

#include "protocol.h"

typedef struct {
    char attempts[MM_MAX_ATTEMPTS][MM_SEQ_LEN + 1];
    char feedbacks[MM_MAX_ATTEMPTS][MM_SEQ_LEN + 1];
    int current_round;                     /* Number of completed rounds (0 .. 12) */
    char master_seq[MM_SEQ_LEN + 1];       /* Secret 5-digit code */
    int is_mastermind;                     /* 1 if Mastermind (Codemaker), 0 if Codebreaker */
    int game_over;                         /* 1 if game has concluded */
    int codebreaker_won;                   /* 1 if codebreaker correctly identified code */
} GameState;

/**
 * Validates that sequence is strictly 5 characters, all digits '0'-'9'.
 */
int game_validate_sequence(const char *seq);

/**
 * Validates that feedback is strictly 5 characters, containing only 'x', 'o', '-'.
 */
int game_validate_feedback(const char *fb);

/**
 * Computes feedback pegs according to Mastermind rules with full duplicate handling.
 * Outputs a 5-character string (null-terminated) containing 'x', 'o', and '-' out of order.
 */
void game_compute_feedback(const char *master, const char *attempt, char *out_feedback);

/**
 * Initializes a new game state.
 */
void game_init(GameState *game, int is_mastermind);

/**
 * Adds an attempt and its feedback to the game board.
 * Returns 1 if game has ended, 0 otherwise.
 */
int game_record_turn(GameState *game, const char *attempt, const char *feedback);

#endif /* MASTERMIND_GAME_H */
