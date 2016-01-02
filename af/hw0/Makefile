CC = gcc
CFLAGS = -g

all: main wc map

main:
	$(CC) $(CFLAGS) main.c -o main 

wc:
	$(CC) $(CFLAGS) wc.c -o wc 

map:
	$(CC) $(CFLAGS) map.c -o map

clean:
	rm main wc map

check:
	./main > tests/mainResults.txt
	./wc tests/TNULinchar.txt > tests/RNULchar.txt
	./wc tests/TstartNUL.txt > tests/RstartNUL.txt
	python test.py
