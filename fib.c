/* fib.c - Fibonacci calculator used to test SimpleShell ("./fib 40") */
#include <stdio.h>
#include <stdlib.h>

static unsigned long fib(int n)
{
    return n < 2 ? (unsigned long)n : fib(n - 1) + fib(n - 2);
}

int main(int argc, char *argv[])
{
    int n = 40;                      /* default if no argument is given */
    if (argc > 1)
        n = atoi(argv[1]);
    if (n < 0) {
        fprintf(stderr, "usage: %s [non-negative n]\n", argv[0]);
        return 1;
    }
    printf("Fib(%d) = %lu\n", n, fib(n));
    return 0;
}
