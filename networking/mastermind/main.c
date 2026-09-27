#include "protocol.h"
#include "logger.h"
#include "discovery.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int cost_cutting = 0;
    int logging = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cost-cutting") == 0) {
            cost_cutting = 1;
        } else if (strcmp(argv[i], "--log") == 0) {
            logging = 1;
        }
    }

    logger_init(logging);
    logger_log("Mastermind launched. cost_cutting=%d, logging=%d", cost_cutting, logging);

    /* Prompt for player name */
    char local_name[MM_MAX_NAME_LEN];
    printf("Enter your name: ");
    fflush(stdout);
    if (!fgets(local_name, sizeof(local_name), stdin)) {
        return 0;
    }
    local_name[strcspn(local_name, "\r\n")] = '\0';
    if (strlen(local_name) == 0) {
        strcpy(local_name, "Player");
    }

    DiscoveryManager dm;
    if (discovery_init(&dm, local_name, 0, cost_cutting) < 0) {
        fprintf(stderr, "Failed to initialize UDP discovery\n");
        return 1;
    }

    /* Render lobby */
    ui_render_lobby(dm.players, dm.player_count, local_name, 0, cost_cutting);

    discovery_close(&dm);
    logger_close();
    return 0;
}
