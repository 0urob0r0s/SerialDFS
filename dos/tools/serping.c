/*
 * serdfs/dos/tools/serping.c
 * SERPING COM1|COM2 9600|19200|38400|57600
 *
 * Sends a SerialDFS PING frame, expects a PING response with STATUS_OK.
 * Prints OK (<N ticks) or FAIL with reason.
 * Used to validate Phase 2/3 serial transport end-to-end.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/seruart.h"
#include "../src/serframe.h"

#define PING_SEQ       1u
#define TIMEOUT_TICKS  36u   /* ~2 s at 18.2 ticks/s */

static unsigned char txbuf[FRAME_MIN_SIZE];
static unsigned char rxbuf[FRAME_MAX_SIZE];

static int parse_port(const char *s) {
    const char *p = s;
    if ((p[0]=='C'||p[0]=='c') && (p[1]=='O'||p[1]=='o') &&
        (p[2]=='M'||p[2]=='m'))
        p += 3;
    if (p[0]=='1' && p[1]=='\0') return SERUART_COM1;
    if (p[0]=='2' && p[1]=='\0') return SERUART_COM2;
    return 0;
}

int main(int argc, char *argv[]) {
    int port, rc;
    unsigned int div, txlen, rxlen;
    unsigned long t0, t1;
    FrameHdr hdr;
    const unsigned char *payload;
    unsigned int plen;

    if (argc < 3) {
        printf("Usage: SERPING COM1|COM2 9600|19200|38400|57600\n");
        return 1;
    }

    port = parse_port(argv[1]);
    if (!port) { printf("Unknown port: %s\n", argv[1]); return 1; }

    div = seruart_baud_to_div(atol(argv[2]));
    if (!div) { printf("Unsupported baud: %s\n", argv[2]); return 1; }

    seruart_init(port, div);
    seruart_drain(port);

    /* Build PING frame */
    txlen = frame_encode(CMD_PING, PING_SEQ, FLAG_NONE, STATUS_OK,
                         (unsigned char *)0, 0u, txbuf);

    t0 = seruart_ticks();
    seruart_send_block(port, txbuf, txlen);

    /* Receive header */
    rxlen = seruart_recv_block_timeout(port, rxbuf, FRAME_HDR_SIZE, TIMEOUT_TICKS);
    if (rxlen < FRAME_HDR_SIZE) { printf("FAIL timeout (header)\n"); return 1; }

    /* Peek at payload_len to know how many more bytes to read */
    plen = (unsigned int)rxbuf[8] | ((unsigned int)rxbuf[9] << 8);
    if (plen > FRAME_MAX_PAYLOAD) { printf("FAIL oversized payload\n"); return 1; }

    if (plen + FRAME_CRC_SIZE > 0u) {
        unsigned int got = seruart_recv_block_timeout(
            port, rxbuf + FRAME_HDR_SIZE,
            plen + FRAME_CRC_SIZE, TIMEOUT_TICKS);
        if (got < plen + FRAME_CRC_SIZE) { printf("FAIL timeout (payload)\n"); return 1; }
    }
    t1 = seruart_ticks();

    rc = frame_decode(rxbuf, FRAME_HDR_SIZE + plen + FRAME_CRC_SIZE,
                      &hdr, &payload, &plen);
    if (rc != FRAME_OK) { printf("FAIL decode %d\n", rc); return 1; }
    if (hdr.seq != PING_SEQ) { printf("FAIL seq mismatch\n"); return 1; }
    if (hdr.command != CMD_PING) { printf("FAIL bad cmd 0x%02X\n", (unsigned)hdr.command); return 1; }
    if (hdr.status != STATUS_OK) { printf("FAIL status 0x%02X\n", (unsigned)hdr.status); return 1; }

    if (t1 > t0)
        printf("OK (%lu ticks)\n", t1 - t0);
    else
        printf("OK (<1 tick)\n");

    return 0;
}
