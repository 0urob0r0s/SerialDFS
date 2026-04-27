/*
 * serdfs/dos/src/seruart.h
 * Direct 8250/16550 UART driver for SerialDFS DOS client.
 * Polling send/receive for Phase 2-5 non-resident tools.
 * IRQ-driven receive ring added in Phase 8 (TSR).
 */
#ifndef SERUART_H
#define SERUART_H

#define SERUART_COM1        1
#define SERUART_COM2        2

/* Baud rate divisors (UART clock = 1.8432 MHz) */
#define SERUART_DIV_9600    12u
#define SERUART_DIV_19200   6u
#define SERUART_DIV_38400   3u
#define SERUART_DIV_57600   2u
#define SERUART_DIV_115200  1u

/* Map baud rate (as long) to divisor. Returns 0 for unsupported rates. */
unsigned int seruart_baud_to_div(long baud);

/* Initialize UART: set baud divisor, 8N1, assert DTR+RTS+OUT2.
   Auto-detects 16550 FIFO and enables it if present. */
void seruart_init(int port, unsigned int divisor);

/* Send one byte (polls LSR THRE until ready). */
void seruart_putchar(int port, unsigned char c);

/* Send a block of bytes. */
void seruart_send_block(int port, const unsigned char far *buf, unsigned int len);

/* Receive one byte with timeout.
   ticks: BIOS timer ticks to wait (18.2/s; 1 tick ~= 55 ms).
   Returns 1 on success, 0 on timeout. */
int seruart_getchar_timeout(int port, unsigned char *c, unsigned int ticks);

/* Receive up to len bytes with a shared deadline.
   Returns actual bytes received (may be < len on timeout). */
unsigned int seruart_recv_block_timeout(int port, unsigned char far *buf,
                                        unsigned int len, unsigned int ticks);

/* Discard any bytes waiting in the receive buffer. */
void seruart_drain(int port);

/* Read BIOS timer tick counter at 0040:006C (18.2 ticks/s). */
unsigned long seruart_ticks(void);

#endif
