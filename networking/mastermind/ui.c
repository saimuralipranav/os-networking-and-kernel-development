#include "ui.h"
#include <stdio.h>
#include <time.h>

void ui_print_colored_feedback(const char *fb) {
    if (!fb) return;
    for (int i = 0; fb[i] != '\0'; i++) {
        char c = fb[i];
        if (c == 'x') {
            printf("\033[32mx\033[0m"); /* Green */
        } else if (c == 'o') {
            printf("\033[33mo\033[0m"); /* Yellow */
        } else if (c == '-') {
            printf("\033[31m-\033[0m"); /* Red */
        } else {
            putchar(c);
        }
    }
}

void ui_render_lobby(const DiscoveredPlayer *players, int count, const char *local_name, uint16_t local_port, int cost_cutting) {
    printf("\033[2J\033[H"); /* Clear screen and reset cursor */
    printf("====================================================\n");
    printf(" Mastermind Lobby | Player: %s (Port: %u) [%s]\n",
           local_name, local_port, cost_cutting ? "UDP Cost-Cutting" : "TCP");
    printf("====================================================\n");
    printf("Players Online:\n");
    printf("%-4s %-16s %-16s %-6s %s\n", "ID", "Name", "IP Addr", "Port", "Last Seen");

    time_t now = time(NULL);
    for (int i = 0; i < count; i++) {
        long diff = (long)(now - players[i].last_seen);
        if (diff < 0) diff = 0;
        printf("%-4d %-16s %-16s %-6u %ld s. ago\n",
               players[i].id, players[i].name, players[i].ip, players[i].port, diff);
    }

    printf("____________________________________________________\n");
    printf("> ");
    fflush(stdout);
}

void ui_render_board(const GameState *game) {
    if (!game) return;

    printf("\033[2J\033[H"); /* Clear screen */
    printf("--- MASTERMIND GAME BOARD [%s] ---\n",
           game->is_mastermind ? "Mastermind (Codemaker)" : "Codebreaker");

    for (int i = 0; i < MM_MAX_ATTEMPTS; i++) {
        if (i < game->current_round) {
            printf("%s ", game->attempts[i]);
            if (!game->is_mastermind) {
                ui_print_colored_feedback(game->feedbacks[i]);
            } else {
                printf("%s", game->feedbacks[i]);
            }
            putchar('\n');
        } else {
            /* Placeholder for future attempts */
            printf("***** *****\n");
        }
    }

    printf("-----\n");
    if (game->is_mastermind || game->game_over) {
        printf("%s\n", game->master_seq);
    } else {
        printf("*****\n");
    }
    fflush(stdout);
}
