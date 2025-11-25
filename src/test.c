#include <stdint.h>
#include <stdio.h>

int main(){
    uint64_t value = 10;
    int8_t offset = -5;

    value += offset;

    printf("Value after adding offset: %lu\n", value);
}