/*
 * serdfs/dos/src/serframe.h
 * Serial frame codec — 10-byte header, CRC-16/CCITT-FALSE.
 * See protocol/spec.txt for the wire format.
 */
#ifndef SERFRAME_H
#define SERFRAME_H

/* Frame constants */
#define FRAME_MAGIC_0      'S'
#define FRAME_MAGIC_1      'D'
#define FRAME_VERSION      1u
#define FRAME_HDR_SIZE     10u
#define FRAME_CRC_SIZE     2u
#define FRAME_MIN_SIZE     12u   /* HDR + CRC, no payload */
#define FRAME_MAX_PAYLOAD  512u
#define FRAME_MAX_SIZE     524u  /* 10 + 512 + 2 */

/* Command IDs (spec §3) */
#define CMD_PING            0x01u
#define CMD_GET_INFO        0x02u
#define CMD_LIST_DIR        0x10u
#define CMD_LIST_DIR_NEXT   0x11u
#define CMD_OPEN            0x20u
#define CMD_CREATE          0x21u
#define CMD_READ            0x22u
#define CMD_WRITE           0x23u
#define CMD_SEEK            0x24u
#define CMD_CLOSE           0x25u
#define CMD_DELETE          0x30u
#define CMD_RENAME          0x31u
#define CMD_MKDIR           0x32u
#define CMD_RMDIR           0x33u
#define CMD_GET_ATTR        0x40u
#define CMD_SET_ATTR        0x41u
#define CMD_GET_TIME        0x42u
#define CMD_SET_TIME        0x43u
#define CMD_FLUSH           0x50u
#define CMD_STATUS          0x60u

/* Status IDs (spec §4) */
#define STATUS_OK            0x00u
#define ERR_NOT_FOUND        0x01u
#define ERR_ACCESS           0x02u
#define ERR_BAD_HANDLE       0x03u
#define ERR_EXISTS           0x04u
#define ERR_IO               0x05u
#define ERR_CRC              0x06u
#define ERR_TIMEOUT          0x07u
#define ERR_UNSUPPORTED      0x08u
#define ERR_BAD_PATH         0x09u
#define ERR_PROTOCOL         0x0Au

/* Flag bits (spec §5) */
#define FLAG_NONE            0x00u
#define FLAG_RETRY           0x01u

/* Decoded frame header (populated by frame_decode) */
typedef struct {
    unsigned char  version;
    unsigned char  flags;
    unsigned char  seq;
    unsigned char  command;
    unsigned char  status;
    unsigned int   payload_len;
} FrameHdr;

/* frame_decode return codes */
#define FRAME_OK        0
#define FRAME_SHORT    (-1)   /* not enough bytes yet          */
#define FRAME_BADMAGIC (-2)   /* magic != 'S','D'              */
#define FRAME_BADVER   (-3)   /* version != 1                  */
#define FRAME_BADCRC   (-4)   /* CRC mismatch                  */
#define FRAME_TOOLONG  (-5)   /* payload_len > FRAME_MAX_PAYLOAD */

/*
 * CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no reflect, no xorout.
 * crc16_update: incremental, one byte at a time.
 * crc16_block:  compute over a buffer (init=0xFFFF internally).
 */
unsigned int crc16_update(unsigned int crc, unsigned char b);
unsigned int crc16_block(const unsigned char *buf, unsigned int len);

/*
 * frame_encode: build a complete frame into out[].
 * Returns total frame length on success, 0 if plen > FRAME_MAX_PAYLOAD.
 * out[] must be at least FRAME_MAX_SIZE bytes.
 */
unsigned int frame_encode(unsigned char cmd, unsigned char seq,
                          unsigned char flags, unsigned char status,
                          const unsigned char *payload, unsigned int plen,
                          unsigned char *out);

/*
 * frame_decode: parse buf[0..len-1].
 * On FRAME_OK: fills hdr; *payload_out points into buf; *plen_out = payload len.
 * On error: returns negative FRAME_* code.
 */
int frame_decode(const unsigned char *buf, unsigned int len,
                 FrameHdr *hdr,
                 const unsigned char **payload_out,
                 unsigned int *plen_out);

#endif
