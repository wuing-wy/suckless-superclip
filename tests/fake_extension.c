#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

int
main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	if (strcmp(argv[1], "--superclip-describe") == 0) {
		fputs("SUPERCLIP\t1\tfake\tshort\tenter\tFake extension\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--superclip-query") == 0) {
		fputs("BEGIN\t1\nITEM\t1\tid\tTitle\tDescription\nEND\t1\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--malformed") == 0) {
		fputs("ITEM\t1\ttoo-few\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--oversized") == 0) {
		for (size_t i = 0; i < 65536; i++)
			putchar('x');
		putchar('\n');
		return 0;
	}
	if (strcmp(argv[1], "--early-exit") == 0)
		return 0;
	if (strcmp(argv[1], "--action-error") == 0) {
		fputs("ERROR\t1\tintentional action failure\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--slow") == 0) {
		sleep(1);
		fputs("BEGIN\t1\nEND\t1\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--persistent") == 0) {
		char line[65536];

		while (fgets(line, sizeof(line), stdin) != NULL) {
			fputs("BEGIN\t1\nEND\t1\n", stdout);
			fflush(stdout);
		}
		return 0;
	}
	if (strcmp(argv[1], "--hang") == 0) {
		sleep(10);
		return 0;
	}
	if (strcmp(argv[1], "--many-items") == 0) {
		size_t i;

		fputs("BEGIN\t1\n", stdout);
		for (i = 0; i < SUPERCLIP_MAX_ITEMS + 10; i++)
			printf("ITEM\t1\tid-%zu\tTitle %zu\tDescription %zu\n", i, i, i);
		fputs("END\t1\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--stale-error") == 0) {
		fputs("ERROR\t999\tstale failure\n", stdout);
		return 0;
	}
	if (strcmp(argv[1], "--quit-echo") == 0) {
		char line[65536];

		while (fgets(line, sizeof(line), stdin) != NULL) {
			if (strcmp(line, "QUIT\n") == 0) {
				fputs("QUIT-SEEN\n", stdout);
				fflush(stdout);
				return 0;
			}
			fputs("BEGIN\t1\nEND\t1\n", stdout);
			fflush(stdout);
		}
		return 0;
	}
	return 2;
}
