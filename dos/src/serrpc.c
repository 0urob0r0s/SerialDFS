/*
 * serdfs/dos/src/serrpc.c
 * Implements serial_rpc(): SerialDFS RPC with retry/timeout.
 *
 * Ported from EtherDFS sendquery() (vendor/etherdfs-baseline/etherdfs/trunk/
 * etherdfs.c:279-395) by Mateusz Viste, GPL-2.0.
 * Transport replaced: packet driver → 8250/16550 UART via seruart.h.
 * CRC replaced: bsdsum() → CRC-16/CCITT-FALSE via serframe.h.
 * Protocol replaced: EtherDFS wire format → SerialDFS 10-byte header.
 */
#include "serrpc.h"
#include "seruart.h"
#include "serframe.h"

/* Static transmit/receive buffers (one concurrent RPC at a time).
   These must fit in the small-model data segment (64 KB total). */
static unsigned char serrpc_txbuf[FRAME_MAX_SIZE];
static unsigned char serrpc_rxbuf[FRAME_MAX_SIZE];

/* Sequence counter — incremented before each new RPC call, never 0. */
static unsigned char serrpc_seq;

int serial_rpc(int port, unsigned char cmd,
               const unsigned char *payload, unsigned int plen,
               unsigned char *replybuf, unsigned int *replylen,
               unsigned char *reply_status)
{
    unsigned int txlen;
    unsigned int rxlen;
    unsigned int bodylen;
    const unsigned char *rp;
    unsigned int rplen;
    FrameHdr hdr;
    int rc;
    unsigned int attempt;

    /* Advance sequence; skip 0 (reserved as "unset"). */
    serrpc_seq++;
    if (serrpc_seq == 0u) serrpc_seq = 1u;

    /* Encode request frame once — reused on every retry attempt. */
    txlen = frame_encode(cmd, serrpc_seq, FLAG_NONE, STATUS_OK,
                         payload, plen, serrpc_txbuf);
    if (txlen == 0u) return SERRPC_ERR_PAYLOAD;

    for (attempt = 0u; attempt < SERRPC_RETRIES; attempt++) {
#ifdef GLOBALS_SENTINEL
        if (attempt > 0u) glob_data.stat_retries++;
#endif

        /* Flush any stale bytes before sending. */
        seruart_drain(port);

        /* Send the request frame. */
        seruart_send_block(port, serrpc_txbuf, txlen);
#ifdef GLOBALS_SENTINEL
        glob_data.stat_tx_bytes += (unsigned long)txlen;
#endif

        /* ── Receive header (10 bytes) ──────────────────────────────── */
        rxlen = seruart_recv_block_timeout(port, serrpc_rxbuf,
                                           FRAME_HDR_SIZE,
                                           SERRPC_TICK_TIMEOUT);
        if (rxlen < FRAME_HDR_SIZE) {
#ifdef GLOBALS_SENTINEL
            glob_data.stat_timeouts++;
#endif
            continue;   /* timeout — retry */
        }

        /* Quick sanity: magic must be present before reading more bytes. */
        if (serrpc_rxbuf[0] != FRAME_MAGIC_0 || serrpc_rxbuf[1] != FRAME_MAGIC_1)
            continue;

        /* Peek payload_len from header (little-endian at offset 8). */
        bodylen = (unsigned int)serrpc_rxbuf[8]
                | ((unsigned int)serrpc_rxbuf[9] << 8);
        if (bodylen > FRAME_MAX_PAYLOAD) continue;  /* reject oversized */

        /* ── Receive payload + CRC ──────────────────────────────────── */
        if (bodylen + FRAME_CRC_SIZE > 0u) {
            unsigned int got = seruart_recv_block_timeout(
                port,
                serrpc_rxbuf + FRAME_HDR_SIZE,
                bodylen + FRAME_CRC_SIZE,
                SERRPC_TICK_TIMEOUT);
            if (got < bodylen + FRAME_CRC_SIZE) {
#ifdef GLOBALS_SENTINEL
                glob_data.stat_timeouts++;
#endif
                continue;  /* timeout */
            }
        }

        /* ── Full decode + CRC check ────────────────────────────────── */
        rc = frame_decode(serrpc_rxbuf,
                          FRAME_HDR_SIZE + bodylen + FRAME_CRC_SIZE,
                          &hdr, &rp, &rplen);
        if (rc != FRAME_OK) {
#ifdef GLOBALS_SENTINEL
            glob_data.stat_crc_errs++;
#endif
            continue;
        }

        /* Drop stale responses (seq mismatch). */
        if (hdr.seq != serrpc_seq) continue;

        /* ── Valid response — copy out and return ───────────────────── */
        *reply_status = hdr.status;
        *replylen = rplen;
        if (replybuf != 0 && rplen > 0u) {
            unsigned int i;
            for (i = 0u; i < rplen; i++) replybuf[i] = rp[i];
        }
#ifdef GLOBALS_SENTINEL
        glob_data.stat_rx_bytes += (unsigned long)(FRAME_HDR_SIZE + bodylen + FRAME_CRC_SIZE);
#endif
        return SERRPC_OK;
    }

    return SERRPC_ERR_TIMEOUT;
}
