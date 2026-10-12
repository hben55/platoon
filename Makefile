CFLAGS ?= -std=c11 -Wall -Wextra -O2

platoon: src/main.c src/platoon.c src/platoon.h
	$(CC) $(CFLAGS) -o $@ src/main.c src/platoon.c

test: src/test.c src/platoon.c src/platoon.h
	$(CC) $(CFLAGS) -o test_platoon src/test.c src/platoon.c && ./test_platoon

clean:
	rm -f platoon test_platoon

.PHONY: test clean
