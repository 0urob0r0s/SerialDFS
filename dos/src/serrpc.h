/*
 * serdfs/dos/src/serrpc.h
 * serial_rpc() — single SerialDFS RPC call with retry/timeout.
 * Replaces EtherDFS sendquery() for the serial transport.
 */
#ifndef SERRPC_H
#define SERRPC_H

#include "serframe.h"
#include "seruart.h"

/* Retry and timeout tunables */
#define SERRPC_RETRIES      10u  /* 86Box+QEMU UART occasionally drops a byte
                                    per ~30 reads under sustained load; with
                                    128 reads in a 64 KB COPY a single retry
                                    budget gets exhausted. 10 retries gives
                                    ample headroom while still bounding the
                                    worst case (10 × SERRPC_TICK_TIMEOUT). */
#define SERRPC_TICK_TIMEOUT 90u  /* ~5 s per attempt (18.2 ticks/s).
                                    Empirical headroom for the 86Box+QEMU
                                    stack on Apple Silicon: a 522-byte
                                    response chunk at ~2 ms/byte already
                                    takes ~1.04 s of pure serial time;
                                    daemon scheduling + bridge throttle +
                                    UART servicing under load can stretch
                                    that. 5 s gives ~5x margin. */

/* Return codes */
#define SERRPC_OK           0
#define SERRPC_ERR_TIMEOUT  (-1)
#define SERRPC_ERR_PAYLOAD  (-2)   /* payload too large to encode */

/*
 * serial_rpc — send cmd+payload, receive response with retry.
 *
 *   port         SERUART_COM1 or SERUART_COM2
 *   cmd          Command ID (CMD_*)
 *   payload      Request payload bytes; may be NULL when plen==0
 *   plen         Payload byte count (0..FRAME_MAX_PAYLOAD)
 *   replybuf     Caller buffer for response payload (>= FRAME_MAX_PAYLOAD bytes)
 *   replylen     OUT: bytes written to replybuf
 *   reply_status OUT: server status byte (STATUS_OK, ERR_*, ...)
 *
 * Returns SERRPC_OK on success (reply received, seq matched, CRC ok).
 * Returns SERRPC_ERR_TIMEOUT after SERRPC_RETRIES failed attempts.
 * Returns SERRPC_ERR_PAYLOAD if plen exceeds the frame limit.
 *
 * The seq counter is a file-scope static; each call increments it.
 */
int serial_rpc(int port, unsigned char cmd,
               const unsigned char *payload, unsigned int plen,
               unsigned char *replybuf, unsigned int *replylen,
               unsigned char *reply_status);

#endif
