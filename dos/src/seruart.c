/*
 * serdfs/dos/src/seruart.c
 * Direct 8250/16550 UART driver — polling mode for non-resident tools.
 */
#include "seruart.h"
#include <conio.h>   /* outp(), inp() */
#include <i86.h>     /* MK_FP() */

/* COM port base I/O addresses (index 1 = COM1, index 2 = COM2) */
static unsigned com_base[3] = { 0, 0x3F8u, 0x2F8u };

/* 8250/16550 register offsets */
#define UART_RBR  0u  /* receive buffer    (DLAB=0, read)  */
#define UART_THR  0u  /* transmit holding  (DLAB=0, write) */
#define UART_IER  1u  /* interrupt enable                  */
#define UART_IIR  2u  /* interrupt ID      (read)          */
#define UART_FCR  2u  /* FIFO control      (write)         */
#define UART_LCR  3u  /* line control                      */
#define UART_MCR  4u  /* modem control                     */
#define UART_LSR  5u  /* line status                       */
#define UART_DLL  0u  /* divisor latch low  (DLAB=1)       */
#define UART_DLH  1u  /* divisor latch high (DLAB=1)       */

#define LSR_DR    0x01u  /* data ready                   */
#define LSR_THRE  0x20u  /* transmit holding reg empty   */
#define MCR_DTR   0x01u
#define MCR_RTS   0x02u
#define MCR_OUT2  0x08u  /* enables IRQ on PC bus        */
#define LCR_DLAB  0x80u
#define LCR_8N1   0x03u  /* 8 data bits, no parity, 1 stop */
#define FCR_FIFO_EN  0x01u
#define FCR_FIFO_CLR 0xC7u  /* enable + clear + 14-byte trigger */
#define IIR_FIFO_OK  0xC0u  /* bits 7:6 set means 16550 FIFO active */

unsigned long seruart_ticks(void) {
    return *(unsigned long far *)MK_FP(0x40, 0x6C);
}

unsigned int seruart_baud_to_div(long baud) {
    if (baud == 9600L)   return SERUART_DIV_9600;
    if (baud == 19200L)  return SERUART_DIV_19200;
    if (baud == 38400L)  return SERUART_DIV_38400;
    if (baud == 57600L)  return SERUART_DIV_57600;
    if (baud == 115200L) return SERUART_DIV_115200;
    return 0u;
}

void seruart_init(int port, unsigned int divisor) {
    unsigned base = com_base[port];

    outp(base + UART_IER, 0x00u);           /* disable all interrupts      */

    outp(base + UART_LCR, LCR_DLAB);       /* set DLAB to access divisor  */
    outp(base + UART_DLL, divisor & 0xFFu);
    outp(base + UART_DLH, (divisor >> 8) & 0xFFu);

    outp(base + UART_LCR, LCR_8N1);        /* 8N1, clear DLAB             */

    /* Try to enable 16550 FIFO; fall back to 8250 mode if not present */
    outp(base + UART_FCR, FCR_FIFO_CLR);
    if ((inp(base + UART_IIR) & IIR_FIFO_OK) != IIR_FIFO_OK)
        outp(base + UART_FCR, 0x00u);      /* disable FIFO on plain 8250  */

    outp(base + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
}

void seruart_putchar(int port, unsigned char c) {
    unsigned base = com_base[port];
    unsigned long t0 = seruart_ticks();
    /* Wait up to ~2 s for transmit-holding-register empty */
    while (!(inp(base + UART_LSR) & LSR_THRE)) {
        if ((seruart_ticks() - t0) >= 36ul)
            return;   /* emulator stuck; give up silently */
    }
    outp(base + UART_THR, c);
}

void seruart_send_block(int port, const unsigned char far *buf, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len; i++)
        seruart_putchar(port, buf[i]);
}

int seruart_getchar_timeout(int port, unsigned char *c, unsigned int ticks) {
    unsigned base = com_base[port];
    unsigned long start = seruart_ticks();
    while (!(inp(base + UART_LSR) & LSR_DR)) {
        if ((seruart_ticks() - start) >= (unsigned long)ticks)
            return 0;
    }
    *c = (unsigned char)inp(base + UART_RBR);
    return 1;
}

unsigned int seruart_recv_block_timeout(int port, unsigned char far *buf,
                                        unsigned int len, unsigned int ticks) {
    unsigned base = com_base[port];
    unsigned long start = seruart_ticks();
    unsigned int i;
    for (i = 0; i < len; i++) {
        while (!(inp(base + UART_LSR) & LSR_DR)) {
            if ((seruart_ticks() - start) >= (unsigned long)ticks)
                return i;
        }
        buf[i] = (unsigned char)inp(base + UART_RBR);
    }
    return len;
}

void seruart_drain(int port) {
    unsigned base = com_base[port];
    unsigned i;
    /* 64 reads max: guard against emulators that return LSR=0xFF forever */
    for (i = 0; i < 64u && (inp(base + UART_LSR) & LSR_DR); i++)
        (void)inp(base + UART_RBR);
}
