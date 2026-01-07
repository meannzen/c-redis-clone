#include "server.h"
#include <stdio.h>

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("Logs from your program will appear here!\n");

    run_server(6379);

    return 0;
}
