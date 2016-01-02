#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

void wc(FILE *ofile, FILE *infile, char *inname) {
	int num_words = 0;
	int num_lines = 0;
	int num_bytes = 0;
		int c;
		int in_word = 0;
		while ((c = fgetc(infile)) != EOF) {
			num_bytes++;
			if (c == '\n') {
				num_lines++;
			}
			if (in_word == 0 && c == 0) {
				in_word = 0;
				continue;
			}
			if (in_word == 0 && !isspace(c) ) {
				num_words++;
			}
			if (isspace(c)) {
				in_word = 0;
			}
			if (!isspace(c)) {
				in_word = 1;
			}
			
		}

	if (inname == NULL) {
		printf("%d %d %d\n", num_lines, num_words, num_bytes);
	} else {
    	printf("%d %d %d %s\n", num_lines, num_words, num_bytes, inname);
    }
}

int main (int argc, char *argv[]) {
	FILE *infile = fopen(argv[1], "r");
	if (infile == NULL) {
		wc(stdout, stdin, NULL);
	} else {
	   	wc(stdout, infile, argv[1]);
	   	fclose(infile);
	}
    return 0; 
}
