#include "protocol.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int enable_log = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--log") == 0) {
            enable_log = 1;
        }
    }
    logger_init(enable_log);
    logger_log("Mastermind started (logging enabled)");
    logger_close();
    return 0;
}
