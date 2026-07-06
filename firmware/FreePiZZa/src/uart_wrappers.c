/* UART wrapper functions for suspend code compatibility */
#include <xprintf.h>

typedef unsigned int uint32_t;

void uart_print(const char *str) {
    puts(str);
}

void uart_print_hex(uint32_t val) {
    printf("0x%08X", val);
}
