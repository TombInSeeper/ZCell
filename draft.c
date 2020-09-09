#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int g_sleep_time_us = 1;

static void parse_args(int argc , char **argv) {
    int opt = -1;
	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			g_sleep_time_us = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s [-t g_sleep_time_us] \n", argv[0]);
			exit(1);
		}
	}
}

static void sleep_loop() {
    for(;;) {
        usleep(g_sleep_time_us);
    }
}


int main( int argc , char **argv) {
    parse_args(argc, argv);
    sleep_loop();
    return 0;
}