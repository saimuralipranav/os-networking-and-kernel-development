#ifndef MASTERMIND_UI_H
#define MASTERMIND_UI_H

#include "game.h"
#include <time.h>

typedef struct {
    int id;
    uint64_t instance_id;
    char name[MM_MAX_NAME_LEN];
    char ip[64];
    uint16_t port;
    int cost_cutting;
    time_t last_seen;
} DiscoveredPlayer;

/**
 * Clears terminal and displays the active online players lobby.
 */
void ui_render_lobby(const DiscoveredPlayer *players, int count, const char *local_name, uint16_t local_port, int cost_cutting);

/**
 * Renders the 12-row game board with ANSI colored feedback for Codebreaker.
 */
void ui_render_board(const GameState *game);

/**
 * Prints feedback characters with ANSI colors (o=yellow, x=green, -=red).
 */
void ui_print_colored_feedback(const char *fb);

#endif /* MASTERMIND_UI_H */
