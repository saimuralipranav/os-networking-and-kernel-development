#ifndef MASTERMIND_UI_H
#define MASTERMIND_UI_H

#include "discovery.h"
#include "game.h"
#include <time.h>

/**
 * Clears terminal and displays the active online players lobby.
 */
void ui_render_lobby(const DiscoveredPlayer *players, int count, const char *local_name, uint16_t local_port, int cost_cutting);

/**
 * Renders the 12-row game board with ANSI colored feedback for Tatva.
 */
void ui_render_board(const GameState *game);

/**
 * Prints feedback characters with ANSI colors (o=yellow, x=green, -=red).
 */
void ui_print_colored_feedback(const char *fb);

#endif /* MASTERMIND_UI_H */
