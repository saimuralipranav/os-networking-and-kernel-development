#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "tempest: invalid location\n");
        return 1;
    }
    return 0;
}
