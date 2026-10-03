/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 SDK example — primes
 *
 * Sieve of Eratosthenes. Exercises what the loader has to get right:
 * zeroed bss (the sieve), initialised data (the counters), a table of
 * string pointers (data that points at data), and a switch statement
 * (a jump table: data that points at code).
 */

#include "phoenix.h"

#define LIMIT 1000

static unsigned char composite[LIMIT + 1];      /* bss: must start as zeros */
static unsigned found = 0;                      /* data */
static unsigned largest = 2;

static const char *const labels[] = { "primes below ", "largest ", "last digit " };

static void print_number(unsigned value)
{
    char digits[6];
    int count = 0;

    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (count) {
        px_putc(digits[--count]);
    }
}

static const char *digit_name(unsigned digit)
{
    switch (digit) {
    case 1: return "one";
    case 3: return "three";
    case 7: return "seven";
    case 9: return "nine";
    case 2: return "two";
    case 5: return "five";
    default: return "?";
    }
}

int main(void)
{
    unsigned i, j;

    for (i = 2; i <= LIMIT; i++) {
        if (composite[i]) {
            continue;
        }
        found++;
        largest = i;
        for (j = i + i; j <= LIMIT; j += i) {
            composite[j] = 1;
        }
    }

    px_puts(labels[0]);
    print_number(LIMIT);
    px_puts(": ");
    print_number(found);
    px_putc('\n');

    px_puts(labels[1]);
    print_number(largest);
    px_putc('\n');

    px_puts(labels[2]);
    px_puts(digit_name(largest % 10));
    px_putc('\n');
    return 0;
}
