#include "game.h"
#include <string.h>
#include <ctype.h>

int game_validate_sequence(const char *seq) {
    if (!seq) return 0;
    if (strlen(seq) != MM_SEQ_LEN) return 0;
    for (int i = 0; i < MM_SEQ_LEN; i++) {
        if (!isdigit((unsigned char)seq[i])) {
            return 0;
        }
    }
    return 1;
}

int game_validate_feedback(const char *fb) {
    if (!fb) return 0;
    if (strlen(fb) != MM_SEQ_LEN) return 0;
    for (int i = 0; i < MM_SEQ_LEN; i++) {
        char c = fb[i];
        if (c != 'x' && c != 'o' && c != '-') {
            return 0;
        }
    }
    return 1;
}

void game_compute_feedback(const char *master, const char *attempt, char *out_feedback) {
    if (!master || !attempt || !out_feedback) return;

    int master_used[MM_SEQ_LEN] = {0};
    int attempt_used[MM_SEQ_LEN] = {0};

    int red_pegs = 0;   /* 'x' : correct digit in correct position */
    int white_pegs = 0; /* 'o' : correct digit in wrong position */

    /* First pass: exact matches (position and digit) */
    for (int i = 0; i < MM_SEQ_LEN; i++) {
        if (attempt[i] == master[i]) {
            red_pegs++;
            master_used[i] = 1;
            attempt_used[i] = 1;
        }
    }

    /* Second pass: matching digits in wrong positions */
    for (int i = 0; i < MM_SEQ_LEN; i++) {
        if (attempt_used[i]) continue;
        for (int j = 0; j < MM_SEQ_LEN; j++) {
            if (!master_used[j] && attempt[i] == master[j]) {
                white_pegs++;
                master_used[j] = 1;
                attempt_used[i] = 1;
                break;
            }
        }
    }

    /* Format feedback out of order (x's, then o's, then -'s) */
    int idx = 0;
    for (int i = 0; i < red_pegs; i++) {
        out_feedback[idx++] = 'x';
    }
    for (int i = 0; i < white_pegs; i++) {
        out_feedback[idx++] = 'o';
    }
    while (idx < MM_SEQ_LEN) {
        out_feedback[idx++] = '-';
    }
    out_feedback[MM_SEQ_LEN] = '\0';
}

void game_init(GameState *game, int is_mastermind) {
    if (!game) return;
    memset(game, 0, sizeof(*game));
    game->is_mastermind = is_mastermind;
    for (int i = 0; i < MM_MAX_ATTEMPTS; i++) {
        strcpy(game->attempts[i], "*****");
        strcpy(game->feedbacks[i], "*****");
    }
    strcpy(game->master_seq, "*****");
}

int game_record_turn(GameState *game, const char *attempt, const char *feedback) {
    if (!game || game->current_round >= MM_MAX_ATTEMPTS) return 1;

    int r = game->current_round;
    if (attempt && attempt != game->attempts[r]) {
        strncpy(game->attempts[r], attempt, MM_SEQ_LEN);
        game->attempts[r][MM_SEQ_LEN] = '\0';
    }

    if (feedback && feedback != game->feedbacks[r]) {
        strncpy(game->feedbacks[r], feedback, MM_SEQ_LEN);
        game->feedbacks[r][MM_SEQ_LEN] = '\0';
    }

    game->current_round++;

    /* Check win condition: all 5 pegs are 'x' */
    if (strcmp(feedback, "xxxxx") == 0) {
        game->game_over = 1;
        game->codebreaker_won = 1;
        return 1;
    }

    /* Check attempt exhaustion: 12 attempts used */
    if (game->current_round >= MM_MAX_ATTEMPTS) {
        game->game_over = 1;
        game->codebreaker_won = 0;
        return 1;
    }

    return 0;
}
