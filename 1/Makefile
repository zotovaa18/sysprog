GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -I ../utils/heap_help/

all: libcoro.c solution.c
	gcc $(GCC_FLAGS) libcoro.c solution.c ../utils/heap_help/heap_help.c

clean:
	rm a.out
