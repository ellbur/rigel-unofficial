#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>

#include <inhex32.h>

#define HEXTOOL_MAJOR_VER 0
#define HEXTOOL_MINOR_VER 2

#define MAX_PROGRAM_SIZE 0x20000

extern int optopt, optind;
extern char *optarg;

static struct option longopts[] = {
	{"hex2bin", no_argument, NULL, 'h'},
	{"hex2raw", no_argument, NULL, 'r'},
	{"bin2hex", no_argument, NULL, 'b'},
	{"verify", no_argument, NULL, 'v'},
	{"version", no_argument, NULL, 'w'}
};

int hex2raw(const char *hex, const char *raw)
{
	uint32_t start, end;
	uint8_t *buffer;
	FILE *out;

	printf("Converting INHEX32 format file %s to IFI BIN format.\n", hex);

	buffer = (uint8_t *) malloc(MAX_PROGRAM_SIZE);
	memset(buffer, 0xFF, MAX_PROGRAM_SIZE);

	if (inhex32_read(hex, buffer, MAX_PROGRAM_SIZE, &start, &end) == -1) {
		fprintf(stderr, "Error reading input file! Aborting.\n\n");
		exit(1);
	}

	if (!(out = fopen(raw, "wb"))) {
		fprintf(stderr, "Error creating output file %s.\n\n", raw);
		return -1;
	}

	int ret = fwrite(buffer, 1, end, out);
	if (ret < 0) {
		fprintf(stderr, "Write failed.");
		exit(1);
	}

	fclose(out);

	free(buffer);

	printf("Successfully converted %s to %s!\n\n", hex, raw);

	return 0;
}

int bin2hex(const char *bin, const char *hex)
{
	uint8_t *buffer;
	uint32_t start, end;

	printf("Converting IFI BIN file %s to an INHEX32 format program.\n",
	       bin);

	int ret = ifi_bin_read(bin, NULL, 0xFFFFF, &start, &end);
	if (ret < 0) {
		fprintf(stderr, "ifi_bin_read [size] failed\n");
		exit(1);
	}
	printf("s: %.06X e: %.06X\n", start, end);

	buffer = (uint8_t *) malloc(end);
	if (!buffer) {
		fprintf(stderr, "could not malloc %"PRIu32" bytes\n", end);
		exit(1);
	}
	memset(buffer, 0xFF, end);
	ret = ifi_bin_read(bin, buffer, end, &start, &end);
	if (ret < 0) {
		fprintf(stderr, "ifi_bin_read [real] failed\n");
		exit(1);
	}

	if (inhex32_write(hex, buffer, start, end) == -1) {
		fprintf(stderr, "Error writing output file! Aborting.\n\n");
		exit(1);
	}

	free(buffer);
	printf("Successfully converted %s to %s!\n\n", bin, hex);

	return 0;
}

int main(int argc, char **argv)
{
	char ch;

	while ((ch = getopt_long(argc, argv, "hbrv", longopts, NULL)) != -1) {
		switch (ch) {

		case 'r':
			if (argc - optind < 2)
				goto usage;
			return hex2raw(argv[optind], argv[optind + 1]);

		case 'b':
			if (argc - optind < 2)
				goto usage;
			return bin2hex(argv[optind], argv[optind + 1]);

		case 'v':
			if (inhex32_validate(argv[optind]) == -1) {
				printf("HEX file specified is NOT valid.\n");
			} else
				printf("HEX file specified is valid!\n");

			return 0;

		case 'w':
			printf
			    ("Rigel INHEX32 conversion utility, version %d.%d.\n",
			     HEXTOOL_MAJOR_VER, HEXTOOL_MINOR_VER);

		case '?':
			goto usage;

		default:
			exit(1);
		}
	}

 usage:
	printf("Rigel HEX conversion utility, version %d.%d.\n"
	       "Usage: %s [--bin2hex] [--hex2bin] [--verify] infile [outfile]\n",
	       HEXTOOL_MAJOR_VER, HEXTOOL_MINOR_VER, argv[0]);

	return 0;
}
