#include <stdio.h>
#include "defer.h"
int main(void) {
    defer(puts("goodbye"));
    puts("hello");
    return 0;
}
