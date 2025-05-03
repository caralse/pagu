CC = cc
pagu: pagu.c
	$(CC) pagu.c -o pagu -Wall -Wextra -pedantic -std=c23

.PHONY: run
run: pagu
	./pagu

.PHONY: all
all: pagu run
