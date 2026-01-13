#include <stdint.h>
#include "scheduler.h"
#include "syscall_numbers.h"
#include "include/vga.h"
#include "uart.h"

void syscall_handler_c(uint64_t syscall_num, uint64_t arg1) {
    // הדפסה ראשונית כדי לוודא שנכנסנו
    vga_write("!!!!SYSCALL ENTRY!!!!\n");
    uart_puts("\n--- SYSCALL DEBUG ---\n");
    
    // הדפסת מספר הסיסקול שנתפס ב-RDI (שהגיע מ-RAX באסמבלי)
    uart_puts("Num: ");
    uart_puthex(syscall_num);
    uart_puts("\n");

    // הדפסת הארגומנט שנתפס ב-RSI (שהגיע מ-RDI באסמבלי)
    uart_puts("Arg1: ");
    uart_puthex(arg1);
    uart_puts("\n");

    if (syscall_num == 1) {
        uart_puts("Match! Syscall 1 (PRINT) detected.\n");
        vga_write("SYSCALL 1: ");
        
        if (arg1 != 0) {
            // הדפסת המחרוזת עצמה
            vga_write((const char*)arg1);
            uart_puts("Content: ");
            uart_puts((const char*)arg1);
            uart_puts("\n");
        } else {
            uart_puts("Error: arg1 is NULL\n");
        }
    } else {
        uart_puts("Unknown Syscall Number\n");
    }
    
    uart_puts("--- END DEBUG ---\n");
}
