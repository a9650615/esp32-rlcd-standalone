#!/usr/bin/env python3
"""Minimal AirPlay 1 (RAOP) test sender for this project's own receiver.

Why this exists
----------------
modules/airplay/esp-raop-receiver/ is a real RTSP/RTP/ALAC receiver, but it
has never been exercised end to end: no real AirPlay sender has ever
connected to it (see modules/airplay/README.md, "What is unverified"). An
iPhone cannot be used to test it either - real senders always send an
Apple-Challenge on OPTIONS, which requires signing with Apple's AirPort
Express RSA private key, and this repository deliberately does not hold that
key (modules/airplay/README.md, "The RSA key situation").

This is possible without the key because the two RSA paths in raop.c are
both conditional, not mandatory:
  - apple_challenge() (raop.c) returns immediately when the request has no
    Apple-Challenge header - no signing happens.
  - The rsaaeskey decrypt in the ANNOUNCE handler (raop.c) is inside
    `if ((p = strcasestr(body, "rsaaeskey")) != NULL)` - no decrypt happens
    if the SDP body never mentions it.
Confirmed live against the board: OPTIONS *with* an Apple-Challenge header
gets a 256-byte Apple-Response; OPTIONS *without* one gets a clean
"RTSP/1.0 200 OK" and no Apple-Response. This tool always omits both, which
is why it can complete the handshake and stream audio with no key at all.

PCM vs ALAC - and why this sends real (compressed) ALAC
---------------------------------------------------------
The mDNS TXT record raop.c's raop_create() advertises says `et=0,1` and
`cn=0,1` (encryption types 0/1, codec types 0/1 - "0" nominally meaning raw
PCM). That advertisement is not the truth about what the RTP path can
decode: rtp.c's buffer_put_packet() -> alac_decode() calls alac_to_pcm()
unconditionally for every arriving data packet, with no branch on payload
type or codec. There is no PCM path in this receiver at all - only ALAC.
Sending raw PCM and calling it a pass would not exercise anything real.

So this tool is a real (if minimal) ALAC *encoder*, not just an RTP framer.
modules/airplay/alac/ only ships Apple's decoder (decode-only, see
modules/airplay/README.md); there is no encoder anywhere in this repository
or its vendored code to crib from. The encoder here was derived by reading
the decoder backwards (modules/airplay/alac/codec/ALACDecoder.cpp,
ag_dec.c, dp_dec.c, matrix_dec.c) and implementing the exact inverse of each
bitstream read. It deliberately uses the *simplest legal* ALAC frame shape
that still round-trips correctly - full "escape" (verbatim 16-bit PCM)
frames were tried first and rejected: at 352 samples/frame stereo 16-bit,
verbatim samples alone are 1408 bytes (352*2*2), before even the ~3-byte
element header - already at or over rtp.c's own MAX_PACKET (1408 bytes
*including* the 12-byte RTP header, so only 1396 bytes are actually
available for payload). Escape mode cannot fit. What's shipped instead is a
compressed CPE (stereo pair) frame using:
  - no prediction coefficients (numU=numV=0) with mode=1, which per
    ALACDecoder.cpp's CPE decode selects the "numactive==31" short-circuit
    integrator in dp_dec.c's unpc_block() as a pure first-order predictor
    (out[j] = residual[j] + out[j-1]) - i.e. plain sample-to-sample delta,
    with zero coefficient bits spent (no adaptive LMS state to keep in
    sync with a decoder that was never designed to be driven that way);
  - the adaptive Golomb-Rice entropy coder (ag_dec.c's dyn_decomp, inverted
    into dyn_comp here) on those deltas.
For a smooth sine wave the deltas are small and this compresses very well
(measured: a 440Hz/-20dBFS tone's 352-sample frame is ~850 bytes, comfortably
under the 1396-byte budget - see the packet-size guard in encode_cpe_frame()
for what happens if a frequency/amplitude combination ever does not fit).

This encoder's bit-exactness was verified, before writing this file, against
the *real* vendored decoder: modules/airplay/alac/codec/ALACDecoder.cpp
plus its ag_dec.c/dp_dec.c/matrix_dec.c/ALACBitUtilities.c/EndianPortable.c
were compiled standalone (Apache-2.0, no ESP-IDF dependency - they only use
stdint.h/string.h) into a throwaway host harness that decodes one frame from
a file and dumps PCM. Every encoded test case (silence, sine at several
amplitudes/frequencies, full-scale sine, random noise, alternating extremes,
a deliberately short/partial final frame, and forced zero-runs) round-tripped
byte-exact through that real decoder. That harness is not part of this
repository (throwaway, and it would add a C++ toolchain dependency this
otherwise-pure-Python tool doesn't need); --selftest instead ships a
pure-Python transliteration of the same decode-side logic so the round-trip
can still be checked with no C compiler involved.

The RTSP conversation (see raop.c's handle_rtsp() for all of this)
-------------------------------------------------------------------
  1. OPTIONS - no Apple-Challenge header. Gets back "200 OK", no
     Apple-Response (see above).
  2. ANNOUNCE - an SDP body with an `a=fmtp:` line and no `rsaaeskey`/`aesiv`
     anywhere. raop.c's ANNOUNCE handler pulls exactly three substrings out
     of the body via strcasestr(): "rsaaeskey" (absent here - no AES key is
     ever set, so rtp_init()'s `if (aesiv && aeskey)` stays false and the
     stream is never decrypted, matching an SDP that declares no
     encryption), "aesiv" (also absent, same reason), and "fmtp" (present -
     the substring after its ':' up to "\r\n" becomes ctx->rtsp.fmtp
     verbatim, later space-split with strsep() in rtp.c's rtp_init() into
     twelve ints: fmtp[0] payload type (unused by the parser, just
     consumed), fmtp[1] frameLength, fmtp[2] compatibleVersion, fmtp[3]
     bitDepth, fmtp[4] pb, fmtp[5] mb, fmtp[6] kb, fmtp[7] numChannels,
     fmtp[8] maxRun, fmtp[9] maxFrameBytes, fmtp[10] avgBitRate, fmtp[11]
     sampleRate - this is exactly the ALACSpecificConfig alac_init() in
     rtp.c builds the magic cookie from). The SDP also carries a standard
     `a=rtpmap:96 AppleLossless` line for realism/spec-compliance, but
     raop.c's ANNOUNCE handler never looks for "rtpmap" at all - it is not
     functionally consumed, only "fmtp" (and the two RSA substrings) are.
  3. SETUP - a Transport header with control_port=/timing_port= (this
     tool's own bound UDP ports; raop.c's SETUP handler sscanf()s both
     straight out of that header). The response's Transport header carries
     the receiver's own control_port=/timing_port=/server_port= (rtp.c's
     rtp_init() binds these fresh via bind_socket() with the port set to 0,
     i.e. OS-assigned - never assume a fixed value, this tool parses them
     back out of the response).
  4. RECORD - an RTP-Info header with seq=/rtptime= matching this tool's
     first audio packet exactly, so rtp.c's rtp_record()/buffer_put_packet()
     transitions straight from RTP_WAIT to RTP_PLAY on that very first
     packet with no FLUSH round-trip needed.
  5. Audio streams over UDP as RTP data packets (rtp.c's rtp_thread_func(),
     case 0x60) at the real 44100/352 pacing, alongside two more UDP
     exchanges rtp.c's RTP_SYNC/NTP_SYNC state machine requires before it
     will ever call the data callback that reaches the speaker
     (buffer_push_packet() checks `synchro.status != (RTP_SYNC|NTP_SYNC)`
     and returns without playing anything if it isn't set):
       - timing (case 0x53 on receive, this tool responds to the
         receiver's own periodic 0x52 NTP-style requests - rtp.c's
         rtp_request_timing() - on the timing_port this tool declared)
       - sync (case 0x54, this tool sends these unprompted, periodically,
         to the receiver's control_port)
     Every field of the packets in the timing/sync exchange is taken from
     what rtp_thread_func() itself reads at each offset - see
     RaopSession._timing_responder()/_sync_sender() below for the exact
     byte layout with citations.
  6. TEARDOWN at the end, so the receiver's cleanup_rtsp() runs and the
     module's RAOP_EVENT_DISCONNECTED fires cleanly instead of leaving the
     session dangling.

Known firmware issue found while building this (reported, not fixed - this
task is host-tooling only): raop.c's kd_add() always calls strdup() on
whatever kd_lookup(headers, "CSeq") returns; if a request omits CSeq
entirely that is strdup(NULL), undefined behaviour in C (a crash on most
libc's). This tool always sends CSeq on every request specifically to never
trigger that path.

What this cannot test
----------------------
  - Any of the RSA/authentication code paths (apple_challenge(),
    rsa_apply(), the rsaaeskey decrypt) - deliberately, that is the whole
    point (see above). A real AirPlay client's handshake is not exercised.
  - AES-encrypted audio (rtp.c's alac_decode() decrypt-then-decode branch).
    This tool always sends unencrypted ALAC.
  - Resend recovery: rtp.c's rtp_request_resend()/case 0x55 - this tool
    never listens for resend requests on its control port, so packet loss
    on the way to the board will show up as silence gaps or discarded
    frames, not recovered audio. Fine for a local test where loss should be
    negligible; not representative of a lossy real network.
  - The mDNS advertisement/discovery path entirely - a board address is
    always passed explicitly (see --host), never discovered.
  - Multi-room/AirPlay-2 anything - this is AirPlay 1 (RAOP) only, matching
    what esp-raop-receiver implements.
  - Whether what reaches the speaker actually *sounds* like the requested
    tone - that part is for a human to judge by ear; this tool's job stops
    at "the receiver accepted a real, correctly-decodable ALAC stream".

Standard library only: socket, struct, threading, argparse, math, time,
random, sys. No third-party dependencies.

Usage
-----
  python3 scripts/raop-test-sender.py 192.168.1.50
  python3 scripts/raop-test-sender.py 192.168.1.50 --freq 1000 --duration 3 --amplitude 6000
  python3 scripts/raop-test-sender.py 192.168.1.50 --dry-run
  python3 scripts/raop-test-sender.py --selftest
"""
import argparse
import base64
import math
import os
import random
import socket
import struct
import sys
import threading
import time

# ---------------------------------------------------------------------------
# Protocol constants, all cited to the receiver source that consumes them.
# ---------------------------------------------------------------------------

RAOP_PORT = 5000            # raop.c raop_create(): ctx->port = 5000
SAMPLE_RATE = 44100         # fmtp[11] / mDNS "sr" TXT record (raop.c)
CHANNELS = 2                # fmtp[7]; this tool only ever builds CPE (stereo) frames
FRAME_SIZE = 352            # fmtp[1]; rtp.c ctx->frame_size, the ALAC frameLength
BIT_DEPTH = 16               # fmtp[3]
PB, MB, KB = 40, 10, 14      # fmtp[4],[5],[6] - aglib.h's own PB0/MB0/KB0 defaults
MAX_RUN = 255                # fmtp[8] - read into mConfig.maxRun but not referenced
                             # anywhere in ag_dec.c's dyn_decomp(); inert for decode.
FMTP_PAYLOAD_TYPE = 96       # fmtp[0]; also RTP header's payload-type byte (rtp.c: type = packet[1] & ~0x80)

# rtp.c: `char *packet = malloc(MAX_PACKET); recvfrom(sock, packet, MAX_PACKET, ...)`
# - this is the *entire* UDP datagram (12-byte RTP header included), not just
# the ALAC payload. See the module docstring above for why escape/verbatim
# frames alone already blow this budget.
MAX_PACKET = 1408
RTP_HEADER_LEN = 12
MAX_ALAC_PAYLOAD = MAX_PACKET - RTP_HEADER_LEN

# rtp.c: #define MIN_LATENCY 11025 - used here as this tool's fixed declared
# audio latency (in samples) for the sync-packet's rtp_now_latency field.
# Sitting exactly at MIN_LATENCY keeps rtp.c's clamp
# (`if (ctx->latency < MIN_LATENCY) ctx->latency = MIN_LATENCY;`) a no-op.
LATENCY_SAMPLES = 11025

# Wire values for rtp.c's rtp_thread_func() packet-type switch
# (`type = packet[1] & ~0x80`).
RTP_TYPE_DATA = 0x60
RTP_TYPE_TIMING_REQUEST = 0x52   # receiver -> us
RTP_TYPE_TIMING_RESPONSE = 0x53  # us -> receiver
RTP_TYPE_SYNC = 0x54             # us -> receiver

ID_CPE = 1   # ALACBitUtilities.h ELEMENT_TYPE: Channel Pair Element
ID_END = 7   # ELEMENT_TYPE: frame end marker

QBSHIFT = 9                       # aglib.h
QB = 1 << QBSHIFT
MMULSHIFT = 2                     # aglib.h
MDENSHIFT = QBSHIFT - MMULSHIFT - 1
MOFF = 1 << (MDENSHIFT - 2)
BITOFF = 24                       # aglib.h
MAX_PREFIX_32 = 9                 # aglib.h MAX_PREFIX_32
MAX_PREFIX_16 = 9                 # aglib.h MAX_PREFIX_16 (dyn_get's escape threshold)
N_MAX_MEAN_CLAMP = 0xffff         # ag_dec.c
N_MEAN_CLAMP_VAL = 0xffff


class RtspError(Exception):
    """Raised when the receiver's RTSP response indicates failure."""


# ---------------------------------------------------------------------------
# Bit-level writer, MSB-first - matches
# modules/airplay/alac/codec/ALACBitUtilities.c's BitBufferWrite() exactly
# (that file writes bit 7 of each byte first).
# ---------------------------------------------------------------------------

class BitWriter:
    def __init__(self):
        self.bytes = bytearray()
        self._acc = 0
        self._nbits = 0

    def write(self, value, nbits):
        if nbits == 0:
            return
        if not (0 <= value < (1 << nbits)):
            raise ValueError(f"value {value} does not fit in {nbits} bits")
        self._acc = (self._acc << nbits) | value
        self._nbits += nbits
        while self._nbits >= 8:
            self._nbits -= 8
            self.bytes.append((self._acc >> self._nbits) & 0xff)
        self._acc &= (1 << self._nbits) - 1 if self._nbits else 0

    def write_ones(self, n):
        for _ in range(n):
            self.write(1, 1)

    def align_byte(self):
        if self._nbits:
            self.write(0, 8 - self._nbits)

    def getvalue(self):
        self.align_byte()
        return bytes(self.bytes)


def zigzag_encode(v):
    """Inverse of ag_dec.c dyn_decomp()'s sign recovery:
    ndecode even -> del=ndecode/2 (>=0); ndecode odd -> del=-(ndecode+1)/2."""
    return 2 * v if v >= 0 else (-2 * v - 1)


def _lead(x):
    """Port of ag_dec.c's lead(): number of leading zero bits in a 32-bit word."""
    x &= 0xffffffff
    return 32 if x == 0 else 32 - x.bit_length()


def _lg3a(x):
    """Port of ag_dec.c's lg3a(): floor(log2(x+3)) via lead()."""
    return 31 - _lead(x + 3)


def _rice_put_32bit(bw, n, m, k, maxbits):
    """Inverse of ag_dec.c's dyn_get_32bit(): adaptive Golomb-Rice code with
    modulus m=2^k-1 and a fixed-width escape. See the "wasted codeword"
    comment below for the one non-obvious part."""
    if k == 1:
        # dyn_get_32bit() skips the remainder read entirely when k==1 (m=1,
        # so the remainder range is exactly one value and needs zero bits) -
        # mirrored here by never writing a remainder field in this branch.
        quotient = n
        if quotient >= MAX_PREFIX_32:
            bw.write_ones(MAX_PREFIX_32)
            bw.write(n, maxbits)
        else:
            bw.write_ones(quotient)
            bw.write(0, 1)
        return
    quotient, remainder = divmod(n, m)
    if quotient >= MAX_PREFIX_32:
        bw.write_ones(MAX_PREFIX_32)
        bw.write(n, maxbits)
        return
    bw.write_ones(quotient)
    bw.write(0, 1)
    # "Wasted codeword": m=2^k-1 is one short of the 2^k values a k-bit field
    # can hold, so ag_dec.c's decoder speculatively reads a full k bits as v,
    # then rewinds exactly 1 bit whenever v<2 (both v=0 and v=1 decode to
    # remainder=0) because that trailing bit turns out to belong to the next
    # code, not this one. The true, minimal encoding of remainder=0 is
    # therefore only k-1 bits (all zero); remainder>=1 is written as the
    # full k-bit value (remainder+1), which is always >=2 so no rewind ever
    # happens on decode.
    if remainder == 0:
        bw.write(0, k - 1)
    else:
        bw.write(remainder + 1, k)


def _rice_put(bw, n, m, k):
    """Inverse of ag_dec.c's dyn_get() (used only for the zero-run length in
    the adaptive silence/near-silence escape - no fixed-width skip at k==1,
    unlike dyn_get_32bit above)."""
    quotient, remainder = divmod(n, m) if m else (n, 0)
    if quotient >= MAX_PREFIX_16:
        bw.write_ones(MAX_PREFIX_16)
        bw.write(n, 16)
        return
    bw.write_ones(quotient)
    bw.write(0, 1)
    if remainder == 0:
        bw.write(0, k - 1)
    else:
        bw.write(remainder + 1, k)


def _dyn_comp(bw, residuals, pb, kb, maxsize, mb0):
    """Inverse of ag_dec.c's dyn_decomp(): adaptive-mean Golomb-Rice encode
    of one channel's first-order-difference residuals, including the
    zero-run escape for near-silence."""
    mb = mb0
    wb = (1 << kb) - 1
    zmode = 0
    c = 0
    n_samples = len(residuals)
    while c < n_samples:
        ndecode = zigzag_encode(residuals[c])
        n = ndecode - zmode
        if n < 0:
            # Can only happen if a zero-run wasn't taken to its true maximal
            # length (see the run-length loop below) - i.e. an encoder bug,
            # not a legitimate input. Fail loudly rather than emit a stream
            # that would decode wrong.
            raise RuntimeError(
                f"internal error: negative Rice code value at sample {c} "
                f"(residual={residuals[c]!r}, zmode={zmode}) - zero-run "
                "escape was not taken to its full length"
            )
        m_val = mb >> QBSHIFT
        k = min(_lg3a(m_val), kb)
        m = (1 << k) - 1
        _rice_put_32bit(bw, n, m, k, maxsize)
        c += 1
        mb = pb * (n + zmode) + mb - ((pb * mb) >> QBSHIFT)
        if n > N_MAX_MEAN_CLAMP:
            mb = N_MEAN_CLAMP_VAL
        zmode = 0
        if ((mb << MMULSHIFT) < QB) and (c < n_samples):
            zmode = 1
            k2 = _lead(mb) - BITOFF + ((mb + MOFF) >> MDENSHIFT)
            if k2 < 1:
                raise RuntimeError(f"internal error: degenerate zero-run Rice parameter k={k2}")
            mz = ((1 << k2) - 1) & wb
            run = 0
            while c + run < n_samples and residuals[c + run] == 0:
                run += 1
            _rice_put(bw, run, mz, k2)
            c += run
            if run >= 65535:
                zmode = 0
            mb = 0


def encode_cpe_frame(left, right):
    """Builds one ALAC CPE (stereo pair) compressed frame, byte-exact
    against modules/airplay/alac/codec/ALACDecoder.cpp's CPE decode path
    with escapeFlag=0, mode=1, numU=numV=0 (see module docstring for why
    this shape). `left`/`right` are equal-length lists of signed 16-bit
    sample ints, at most FRAME_SIZE long."""
    n = len(left)
    if n != len(right):
        raise ValueError("left/right channel length mismatch")
    if n == 0 or n > FRAME_SIZE:
        raise ValueError(f"frame length {n} must be in 1..{FRAME_SIZE}")
    for name, chan in (("left", left), ("right", right)):
        for s in chan:
            if not (-32768 <= s <= 32767):
                raise ValueError(f"{name} sample {s} out of 16-bit signed range")

    bw = BitWriter()
    bw.write(ID_CPE, 3)
    bw.write(0, 4)     # elementInstanceTag - arbitrary, only OR'd into a bitmask by the decoder
    bw.write(0, 12)    # "unused header" bits - ALACDecoder.cpp requires these to be exactly 0
    partial = 1 if n != FRAME_SIZE else 0
    bw.write((partial << 3) | (0 << 1) | 0, 4)  # partialFrame | bytesShifted=0 | escapeFlag=0
    if partial:
        bw.write((n >> 16) & 0xffff, 16)
        bw.write(n & 0xffff, 16)
    bw.write(0, 8)   # mixBits
    bw.write(0, 8)   # mixRes=0 -> matrix_dec.c's unmix16() takes the "conventional
                     # separated stereo" branch: out is just u[]/v[] interleaved, no M/S mixing
    bw.write(1, 4); bw.write(0, 4)   # modeU=1, denShiftU=0 (denShift unused when numU=0)
    bw.write(4, 3); bw.write(0, 5)   # pbFactorU=4 (-> effective pb stays pb*4/4=pb), numU=0
    bw.write(1, 4); bw.write(0, 4)   # modeV=1, denShiftV=0
    bw.write(4, 3); bw.write(0, 5)   # pbFactorV=4, numV=0
    # chanBits for CPE compressed frames: mConfig.bitDepth - bytesShifted*8 + 1
    # (ALACDecoder.cpp) = 16 - 0 + 1 = 17 (the extra bit is headroom for a
    # mid/side sum that can exceed 16 bits; unused here since mixRes=0, but
    # the decoder always sizes the predictor/entropy stage at 17 bits for CPE).
    chan_bits = BIT_DEPTH + 1
    for chan in (left, right):
        residuals = [chan[0]] + [chan[i] - chan[i - 1] for i in range(1, n)]
        _dyn_comp(bw, residuals, PB, KB, chan_bits, MB)
    bw.write(ID_END, 3)
    payload = bw.getvalue()
    if RTP_HEADER_LEN + len(payload) > MAX_PACKET:
        raise RuntimeError(
            f"encoded ALAC frame is {len(payload)} bytes; with the 12-byte RTP "
            f"header that is {RTP_HEADER_LEN + len(payload)} bytes, over rtp.c's "
            f"MAX_PACKET ({MAX_PACKET} bytes, the whole recvfrom() buffer including "
            "the RTP header - see rtp.c rtp_thread_func()). This signal compresses "
            "too poorly to fit one frame; lower --amplitude and/or --freq and retry."
        )
    return payload


# ---------------------------------------------------------------------------
# Sine generation
# ---------------------------------------------------------------------------

def generate_tone(freq, amplitude, sample_rate, n_samples, start_index=0):
    return [
        int(round(amplitude * math.sin(2 * math.pi * freq * (start_index + i) / sample_rate)))
        for i in range(n_samples)
    ]


# ---------------------------------------------------------------------------
# RTP packet framing
# ---------------------------------------------------------------------------

def build_rtp_data_header(seqno, rtptime, first):
    """rtp.c rtp_thread_func(), case 0x60: type = packet[1] & ~0x80 must be
    0x60; the "first packet" marker is packet[1] & 0x80. seqno read via
    ntohs(*(u16*)(pktp+2)), rtptime via ntohl(*(u32*)(pktp+4)); pktp then
    advances by 12 (bytes[8:12], SSRC-equivalent here, are never read)."""
    header = bytearray(RTP_HEADER_LEN)
    header[0] = 0x80
    header[1] = (0x80 if first else 0x00) | RTP_TYPE_DATA
    struct.pack_into(">H", header, 2, seqno & 0xffff)
    struct.pack_into(">I", header, 4, rtptime & 0xffffffff)
    return bytes(header)


def ms_to_ntp64(ms):
    """rtp.c's MS2NTP() macro: ((((u64)ms)<<22)/1000)<<10. Only relative
    differences between values built this way are ever used by the
    receiver (NTP2MS() of a difference), so no real epoch is needed."""
    ntp = ((int(ms) << 22) // 1000) << 10
    ntp &= (1 << 64) - 1
    return (ntp >> 32) & 0xffffffff, ntp & 0xffffffff


def now_ms():
    return int(time.time() * 1000) & 0xffffffff


def build_timing_response(reference):
    """rtp.c rtp_thread_func(), case 0x53 (read by the *receiver* from a
    packet *this tool* sends): reference = ntohl(pktp+12), remote =
    (ntohl(pktp+16)<<32)+ntohl(pktp+20). `reference` here is the value the
    receiver's own 0x52 request embedded at its offset 28 (rtp_request_timing()
    in rtp.c), echoed straight back."""
    pkt = bytearray(32)
    pkt[0] = 0x80
    pkt[1] = RTP_TYPE_TIMING_RESPONSE
    hi, lo = ms_to_ntp64(now_ms())
    struct.pack_into(">I", pkt, 12, reference & 0xffffffff)
    struct.pack_into(">I", pkt, 16, hi)
    struct.pack_into(">I", pkt, 20, lo)
    return bytes(pkt)


def parse_timing_request(pkt):
    """rtp.c rtp_request_timing(): sender builds req[28:32] = htonl(now)."""
    if len(pkt) < 32:
        return None
    return struct.unpack_from(">I", pkt, 28)[0]


def build_sync_packet(rtp_now, rtp_now_latency, first):
    """rtp.c rtp_thread_func(), case 0x54 (read by the receiver from a
    packet this tool sends, unprompted, periodically): flags =
    ntohs(pktp+2); rtp_now_latency = ntohl(pktp+4); remote =
    (ntohl(pktp+8)<<32)+ntohl(pktp+12); rtp_now = ntohl(pktp+16).
    `packet[0] & 0x10` marks "1st sync packet" (logging only, per
    ALACDecoder... no, per rtp.c's own case 0x54 body - purely informational)."""
    pkt = bytearray(20)
    pkt[0] = 0x90 if first else 0x80
    pkt[1] = RTP_TYPE_SYNC
    struct.pack_into(">H", pkt, 2, 0)  # flags=0: avoids rtp.c's "flags==7 or 4 -> latency+=11025" branch
    struct.pack_into(">I", pkt, 4, rtp_now_latency & 0xffffffff)
    hi, lo = ms_to_ntp64(now_ms())
    struct.pack_into(">I", pkt, 8, hi)
    struct.pack_into(">I", pkt, 12, lo)
    struct.pack_into(">I", pkt, 16, rtp_now & 0xffffffff)
    return bytes(pkt)


# ---------------------------------------------------------------------------
# RTSP layer
# ---------------------------------------------------------------------------

def build_sdp(our_ip, receiver_ip, crypto=None):
    fmtp = f"{FMTP_PAYLOAD_TYPE} {FRAME_SIZE} 0 {BIT_DEPTH} {PB} {MB} {KB} {CHANNELS} {MAX_RUN} 0 0 {SAMPLE_RATE}"
    lines = [
        "v=0",
        f"o=raop-test-sender 0 0 IN IP4 {our_ip}",
        "s=RLCD RAOP test sender",
        f"c=IN IP4 {receiver_ip}",
        "t=0 0",
        f"m=audio 0 RTP/AVP {FMTP_PAYLOAD_TYPE}",
        f"a=rtpmap:{FMTP_PAYLOAD_TYPE} AppleLossless",   # not parsed by raop.c - see module docstring
        f"a=fmtp:{fmtp}",
    ]
    if crypto is not None:
        # raop.c pulls these out with strcasestr(body, "rsaaeskey") /
        # strextract(p, ":", "\r\n"), so the name must be followed by ':' and
        # the value must end the line. Apple strips the base64 '=' padding and
        # raop.c pads it back with base64_pad(), so send it stripped - that is
        # the input the receiver's padding code actually has to handle.
        lines.append("a=rsaaeskey:" + crypto.rsaaeskey_b64)
        lines.append("a=aesiv:" + crypto.aesiv_b64)
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


class StreamCrypto:
    """The sender half of AirPlay 1's audio encryption.

    A real sender generates a random AES-128 session key, encrypts it to the
    receiver's public key with RSA-OAEP-SHA1, and sends that as `rsaaeskey`
    with a random IV as `aesiv`. Every RTP payload is then AES-128-CBC
    encrypted, the IV reset for each packet and any trailing bytes past the
    last whole 16-byte block left in the clear - see rtp.c's alac_decode(),
    which mirrors exactly that.

    OAEP-SHA1 is not a guess: raop.c's rsa_apply(RSA_MODE_KEY) sets
    mbedtls_rsa_set_padding(MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1) before
    decrypting. Padding that does not match makes the decrypt fail, which the
    receiver reports as "RSA decrypt error".

    Takes a PUBLIC key file. This tool never reads the private key - derive
    the public half once with:

        openssl rsa -in modules/airplay/secrets/raop_private_key.pem \
                    -pubout -out /tmp/raop_public.pem
    """

    def __init__(self, pubkey_path):
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        self._Cipher, self._algorithms, self._modes = Cipher, algorithms, modes

        with open(pubkey_path, "rb") as f:
            pub = serialization.load_pem_public_key(f.read())

        self.key = os.urandom(16)
        self.iv = os.urandom(16)
        wrapped = pub.encrypt(
            self.key,
            padding.OAEP(mgf=padding.MGF1(algorithm=hashes.SHA1()),
                         algorithm=hashes.SHA1(), label=None))
        self.rsaaeskey_b64 = base64.b64encode(wrapped).decode().rstrip("=")
        self.aesiv_b64 = base64.b64encode(self.iv).decode().rstrip("=")

    def encrypt_payload(self, payload):
        aeslen = len(payload) & ~0xF
        if aeslen == 0:
            return payload
        enc = self._Cipher(self._algorithms.AES(self.key),
                           self._modes.CBC(self.iv)).encryptor()
        return enc.update(payload[:aeslen]) + enc.finalize() + payload[aeslen:]


def format_request(request_line, headers, body=b""):
    lines = [request_line] + [f"{k}: {v}" for k, v in headers.items()]
    text = "\r\n".join(lines) + "\r\n\r\n"
    return text.encode("ascii") + body


def recv_rtsp_response(sock, timeout=5.0):
    sock.settimeout(timeout)
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RtspError("connection closed while waiting for a response")
        buf += chunk
    head, _, _ = buf.partition(b"\r\n\r\n")
    # util.c's http_send() never writes a body (kd_dump() is headers-only),
    # so no response in this protocol ever carries one - nothing to read past
    # the blank line.
    text = head.decode("ascii", errors="replace")
    lines = text.split("\r\n")
    status_line = lines[0]
    headers = {}
    for line in lines[1:]:
        if not line:
            continue
        k, _, v = line.partition(":")
        headers[k.strip().lower()] = v.strip()
    return status_line, headers, text


class RaopSession:
    def __init__(self, host, port, verbose=True):
        self.host = host
        self.port = port
        self.verbose = verbose
        self.sock = None
        self.cseq = 0
        self.our_ip = None

    def _log(self, prefix, text):
        if self.verbose:
            for line in text.rstrip("\r\n").split("\r\n"):
                print(f"{prefix} {line}")

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=5.0)
        self.our_ip = self.sock.getsockname()[0]

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def request(self, method, path, headers, body=b""):
        self.cseq += 1
        headers = dict(headers)
        headers["CSeq"] = str(self.cseq)  # raop.c's kd_add("CSeq", ...) strdup()s
                                           # kd_lookup(headers,"CSeq") unconditionally -
                                           # a missing CSeq is strdup(NULL), UB in C.
                                           # Always send one to never hit that.
        request_line = f"{method} {path} RTSP/1.0"
        raw = format_request(request_line, headers, body)
        self._log(">>>", raw.decode("ascii", errors="replace"))
        self.sock.sendall(raw)
        status_line, resp_headers, resp_text = recv_rtsp_response(self.sock)
        self._log("<<<", resp_text)
        parts = status_line.split(None, 2)
        code = int(parts[1]) if len(parts) >= 2 and parts[1].isdigit() else -1
        if code != 200:
            raise RtspError(f"{method} failed: receiver returned '{status_line}'")
        return resp_headers


# ---------------------------------------------------------------------------
# Background UDP responders
# ---------------------------------------------------------------------------

def timing_responder_loop(sock, stop_event, verbose):
    sock.settimeout(0.5)
    while not stop_event.is_set():
        try:
            pkt, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        except OSError:
            return
        reference = parse_timing_request(pkt)
        if reference is None:
            continue
        resp = build_timing_response(reference)
        try:
            sock.sendto(resp, addr)
        except OSError:
            return
        if verbose:
            print(f"[timing] 0x52 request (reference={reference}) -> 0x53 response sent")


def sync_sender_loop(sock, dest, state_fn, stop_event, interval, verbose):
    first = True
    while not stop_event.is_set():
        rtp_now, rtp_now_latency = state_fn()
        pkt = build_sync_packet(rtp_now, rtp_now_latency, first)
        try:
            sock.sendto(pkt, dest)
        except OSError:
            return
        if verbose:
            print(f"[sync]    0x54 sync sent (rtp_now={rtp_now}, first={first})")
        first = False
        stop_event.wait(interval)


# ---------------------------------------------------------------------------
# Main run
# ---------------------------------------------------------------------------

def run(args):
    if not (1 <= args.amplitude <= 32767):
        raise SystemExit(f"--amplitude must be in 1..32767 (16-bit signed PCM range), got {args.amplitude}")
    if not (0 < args.freq < SAMPLE_RATE / 2):
        raise SystemExit(f"--freq must be in (0, {SAMPLE_RATE/2}) Hz (below Nyquist), got {args.freq}")
    if args.duration <= 0:
        raise SystemExit(f"--duration must be > 0, got {args.duration}")
    total_samples = round(args.duration * SAMPLE_RATE)
    if total_samples < 1:
        raise SystemExit(f"--duration {args.duration}s produces 0 samples at {SAMPLE_RATE}Hz - too short")

    samples = generate_tone(args.freq, args.amplitude, SAMPLE_RATE, total_samples)

    start_seq = random.randint(0, 0xffff)
    start_rtptime = random.randint(0, 0xffffffff)

    if args.dry_run:
        return dry_run(args, samples, start_seq, start_rtptime)

    session = RaopSession(args.host, args.rtsp_port)
    session.connect()
    print(f"connected to {args.host}:{args.rtsp_port} from {session.our_ip}")

    ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ctrl_sock.bind(("0.0.0.0", 0))
    timing_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    timing_sock.bind(("0.0.0.0", 0))
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    data_sock.bind(("0.0.0.0", 0))
    our_ctrl_port = ctrl_sock.getsockname()[1]
    our_timing_port = timing_sock.getsockname()[1]
    print(f"bound local UDP ports: control={our_ctrl_port} timing={our_timing_port} "
          f"data={data_sock.getsockname()[1]}")

    # Built before anything is sent so a bad key path fails immediately rather
    # than halfway through an RTSP conversation the receiver has to time out.
    crypto = StreamCrypto(args.pubkey) if args.pubkey else None
    if crypto:
        print(f"[crypto]  AES-128 session key wrapped with RSA-OAEP-SHA1 from {args.pubkey}")

    stop_event = threading.Event()
    threads = []
    try:
        # 1. OPTIONS. No Apple-Challenge by default (see module docstring);
        # --challenge adds one because that is the single RTSP path a real
        # sender takes that this tool otherwise never touches. It matters for
        # more than coverage: apple_challenge() runs an mbedtls RSA-2048
        # signature on the RTSP task's stack, so every stack measurement taken
        # without it is an underestimate of what an iPhone session costs.
        options_headers = {}
        if args.challenge:
            # 16 random bytes is what senders send; raop.c decodes it, appends
            # the receiver's IP and MAC, pads to 32, and signs that. The value
            # itself is never checked by anything, only its presence and
            # length, so it does not need to come from a real device.
            challenge = base64.b64encode(os.urandom(16)).decode().rstrip("=")
            options_headers["Apple-Challenge"] = challenge
        resp = session.request("OPTIONS", "*", options_headers)
        if args.challenge:
            # A missing or short response means the signing failed. Left as a
            # hard failure: a receiver that answers OPTIONS but does not sign
            # is exactly the receiver an iPhone pairs with and then refuses to
            # play to, which is the failure this flag exists to catch.
            apple_response = resp.get("apple-response", "")
            decoded_len = len(base64.b64decode(apple_response + "=" * (-len(apple_response) % 4)))
            assert decoded_len == 256, (
                f"Apple-Response is {decoded_len} bytes, expected 256 "
                f"(RSA-2048 signature) - the receiver did not sign the challenge")
            print(f"[auth]    Apple-Challenge accepted, {decoded_len}-byte Apple-Response")

        # 2. ANNOUNCE - SDP with fmtp, no rsaaeskey/aesiv.
        sdp = build_sdp(session.our_ip, args.host, crypto)
        if crypto is None:
            assert b"rsaaeskey" not in sdp and b"aesiv" not in sdp
        session.request(
            "ANNOUNCE", f"rtsp://{session.our_ip}/raop-test", {
                "Content-Type": "application/sdp",
                "Content-Length": str(len(sdp)),
            }, sdp,
        )

        # 3. SETUP - declare our control/timing ports, learn the receiver's.
        transport_req = (
            f"RTP/AVP/UDP;unicast;mode=record;"
            f"control_port={our_ctrl_port};timing_port={our_timing_port}"
        )
        resp_headers = session.request(
            "SETUP", f"rtsp://{session.our_ip}/raop-test", {"Transport": transport_req},
        )
        transport_resp = resp_headers.get("transport", "")
        recv_control_port = _extract_port(transport_resp, "control_port")
        recv_timing_port = _extract_port(transport_resp, "timing_port")
        recv_server_port = _extract_port(transport_resp, "server_port")
        if not (recv_control_port and recv_timing_port and recv_server_port):
            raise RtspError(f"SETUP response Transport header missing a port: {transport_resp!r}")
        print(f"receiver ports: server(data)={recv_server_port} control={recv_control_port} "
              f"timing={recv_timing_port}")

        # Background: respond to the receiver's NTP-style timing requests,
        # and send it periodic sync packets, from here on.
        t1 = threading.Thread(target=timing_responder_loop,
                               args=(timing_sock, stop_event, args.verbose), daemon=True)
        t1.start()
        threads.append(t1)

        # Encode before the clock starts. start_ms anchors every sync packet's
        # rtp_now, so any work done after this line is time the receiver is told
        # the stream advanced through when it did not.
        packets = encode_packets(samples, start_seq, start_rtptime, crypto)
        print(f"pre-encoded {len(packets)} ALAC frames"
              + (" (AES-128-CBC encrypted)" if crypto else ""))

        start_ms = now_ms()

        def sync_state():
            elapsed_ms = (now_ms() - start_ms) & 0xffffffff
            rtp_now = (start_rtptime + int(elapsed_ms * SAMPLE_RATE / 1000)) & 0xffffffff
            rtp_now_latency = (rtp_now - LATENCY_SAMPLES) & 0xffffffff
            return rtp_now, rtp_now_latency

        t2 = threading.Thread(
            target=sync_sender_loop,
            args=(ctrl_sock, (args.host, recv_control_port), sync_state, stop_event,
                  args.sync_interval, args.verbose),
            daemon=True,
        )
        t2.start()
        threads.append(t2)

        # 4. RECORD - RTP-Info seq/rtptime matches the first data packet exactly.
        session.request(
            "RECORD", f"rtsp://{session.our_ip}/raop-test", {
                "RTP-Info": f"seq={start_seq};rtptime={start_rtptime}",
            },
        )

        # 5. Stream audio.
        print(f"streaming {args.duration:.2f}s tone at {args.freq}Hz, amplitude {args.amplitude} "
              f"({20*math.log10(args.amplitude/32767):.1f} dBFS)")
        data_dest = (args.host, recv_server_port)
        stream_audio(data_sock, data_dest, packets)
        print("stream complete")

        # 6. TEARDOWN.
        session.request("TEARDOWN", f"rtsp://{session.our_ip}/raop-test", {})
    finally:
        stop_event.set()
        for t in threads:
            t.join(timeout=2.0)
        ctrl_sock.close()
        timing_sock.close()
        data_sock.close()
        session.close()


def encode_packets(samples, seqno, rtptime, crypto=None):
    """Build every RTP data packet up front. See stream_audio() for why."""
    packets = []
    idx = 0
    first = True
    while idx < len(samples):
        chunk = min(FRAME_SIZE, len(samples) - idx)
        chan = samples[idx:idx + chunk]
        payload = encode_cpe_frame(chan, chan)
        # Encrypted after ALAC, never before: the receiver decrypts and then
        # decodes (rtp.c alac_decode), so the ciphertext has to wrap the
        # compressed frame, not the PCM.
        if crypto is not None:
            payload = crypto.encrypt_payload(payload)
        packets.append(build_rtp_data_header(seqno, rtptime, first) + payload)
        first = False
        seqno = (seqno + 1) & 0xffff
        rtptime = (rtptime + chunk) & 0xffffffff
        idx += chunk
    return packets


def stream_audio(sock, dest, packets):
    # Every packet is built BEFORE the first one is sent, on purpose.
    #
    # encode_cpe_frame() is a pure-Python ALAC encoder and it does not run in
    # real time: one 352-sample frame has a 7.98 ms budget and encoding costs
    # more than that. Encoding inside the send loop therefore made the data
    # stream fall behind wall-clock, while the sync thread kept deriving its
    # rtp_now from wall-clock (see the state_fn near "elapsed_ms" below). The
    # two clocks drifted apart, and the receiver - correctly - saw frames whose
    # playtime had already passed and discarded them. Measured on the board:
    # "missed by" started at 31 ms and grew to 157 ms across one 4 s clip, with
    # the ring empty (W == R) the whole time, which is the signature of a
    # sender that cannot keep up rather than a receiver that is late.
    #
    # A real sender does not have this problem: its rtptime and its sync
    # packets both come from one audio clock. Pre-encoding is how this tool
    # gets the same property. It is not a way of making the test easier - the
    # pacing below is unchanged and still real-time.
    frame_period = FRAME_SIZE / SAMPLE_RATE
    started = time.monotonic()
    next_deadline = started
    worst_lag = 0.0
    for packet in packets:
        sock.sendto(packet, dest)
        next_deadline += frame_period
        lag = time.monotonic() - next_deadline
        if lag > worst_lag:
            worst_lag = lag
        sleep_for = -lag
        if sleep_for > 0:
            time.sleep(sleep_for)
    actual = time.monotonic() - started
    expected = len(packets) * frame_period
    print(f"[pace]    sent {len(packets)} frames in {actual*1000:.0f} ms "
          f"(expected {expected*1000:.0f} ms, drift {(actual-expected)*1000:+.0f} ms, "
          f"worst single-frame lag {worst_lag*1000:.1f} ms)")


def _extract_port(transport_header, key):
    import re
    m = re.search(rf"{key}=(\d+)", transport_header, re.IGNORECASE)
    return int(m.group(1)) if m else None


def dry_run(args, samples, start_seq, start_rtptime):
    print("=== DRY RUN: no network activity, illustrative ports shown ===\n")
    our_ip = "203.0.113.1"      # RFC 5737 documentation-only placeholder
    ctrl_port, timing_port = 6001, 6002
    recv_ctrl, recv_timing, recv_data = 7001, 7002, 7003

    def show(method, path, headers, body=b""):
        raw = format_request(f"{method} {path} RTSP/1.0", {**headers, "CSeq": "N"}, body)
        print(">>>", raw.decode("ascii", errors="replace").replace("\r\n", "\n>>> ").rstrip(">>> \n"))

    show("OPTIONS", "*", {})
    print("<<< RTSP/1.0 200 OK (expected - no Apple-Challenge was sent)\n")

    sdp = build_sdp(our_ip, args.host)
    show("ANNOUNCE", f"rtsp://{our_ip}/raop-test",
         {"Content-Type": "application/sdp", "Content-Length": str(len(sdp))}, sdp)
    print("<<< RTSP/1.0 200 OK (expected)\n")

    show("SETUP", f"rtsp://{our_ip}/raop-test",
         {"Transport": f"RTP/AVP/UDP;unicast;mode=record;control_port={ctrl_port};timing_port={timing_port}"})
    print(f"<<< RTSP/1.0 200 OK (expected)\n<<< Transport: RTP/AVP/UDP;unicast;mode=record;"
          f"control_port={recv_ctrl};timing_port={recv_timing};server_port={recv_data} "
          "(illustrative - real values are receiver-assigned)\n")

    show("RECORD", f"rtsp://{our_ip}/raop-test",
         {"RTP-Info": f"seq={start_seq};rtptime={start_rtptime}"})
    print("<<< RTSP/1.0 200 OK (expected)\n")

    show("TEARDOWN", f"rtsp://{our_ip}/raop-test", {})
    print("<<< RTSP/1.0 200 OK (expected)\n")

    print("=== ALAC frame construction (real computation, no network) ===")
    frame0 = samples[:FRAME_SIZE] if len(samples) >= FRAME_SIZE else samples
    payload = encode_cpe_frame(frame0, frame0)
    print(f"frame 0: {len(frame0)} samples/channel -> {len(payload)}-byte ALAC payload "
          f"(+{RTP_HEADER_LEN} RTP header = {len(payload)+RTP_HEADER_LEN} bytes, "
          f"budget is {MAX_PACKET})")
    print("first 32 bytes:", payload[:32].hex())

    print("\n=== First 3 RTP data packet headers ===")
    seq, rtp = start_seq, start_rtptime
    for i in range(3):
        chunk = min(FRAME_SIZE, len(samples) - i * FRAME_SIZE)
        if chunk <= 0:
            break
        hdr = build_rtp_data_header(seq, rtp, i == 0)
        print(f"packet {i}: seq={seq} rtptime={rtp} header={hdr.hex()}")
        seq = (seq + 1) & 0xffff
        rtp = (rtp + chunk) & 0xffffffff

    print("\n=== Example timing/sync packets ===")
    print("0x53 (our response to a 0x52 request with reference=12345):",
          build_timing_response(12345).hex())
    print("0x54 (our sync packet, first=True, rtp_now=1000, latency=11025):",
          build_sync_packet(1000, 1000 - LATENCY_SAMPLES, True).hex())


# ---------------------------------------------------------------------------
# --selftest
# ---------------------------------------------------------------------------

class _MirrorBitReader:
    """Pure-Python transliteration of ALACBitUtilities.c's BitBufferRead-family
    semantics, used only by --selftest to decode this tool's own encoder
    output without needing a C compiler. Independently cross-checked once
    against the real vendored decoder - see module docstring."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, nbits):
        val = 0
        for _ in range(nbits):
            byte = self.data[self.pos // 8]
            bit = (byte >> (7 - (self.pos % 8))) & 1
            val = (val << 1) | bit
            self.pos += 1
        return val


def _mirror_rice_get_32bit(br, m, k, maxbits):
    start = br.pos
    ones = 0
    while ones < MAX_PREFIX_32:
        if br.read(1) != 1:
            break
        ones += 1
    if ones >= MAX_PREFIX_32:
        br.pos = start + MAX_PREFIX_32
        return br.read(maxbits)
    if k == 1:
        return ones
    v = br.read(k)
    result = ones * m
    if v >= 2:
        result += v - 1
    else:
        # Mirrors ag_dec.c's `tempbits -= 1`: the k-bit read above was
        # speculative (see the "wasted codeword" comment on
        # _rice_put_32bit) - when it turns out v<2, only k-1 bits actually
        # belonged to this code, so give the extra bit back to the stream.
        br.pos -= 1
    return result


def _mirror_rice_get(br, m, k):
    ones = 0
    while ones < MAX_PREFIX_16:
        if br.read(1) != 1:
            break
        ones += 1
    if ones >= MAX_PREFIX_16:
        return br.read(16)
    v = br.read(k)
    result = ones * m
    if v < 2:
        result -= (v - 1)
        br.pos -= 1  # see the give-back comment in _mirror_rice_get_32bit above
    else:
        result += v - 1
    return result


def _zigzag_decode(n):
    return (n // 2) if (n % 2 == 0) else (-((n + 1) // 2))


def _mirror_dyn_decomp(br, num_samples, pb, kb, maxsize, mb0):
    mb = mb0
    wb = (1 << kb) - 1
    zmode = 0
    c = 0
    out = []
    while c < num_samples:
        m_val = mb >> QBSHIFT
        k = min(_lg3a(m_val), kb)
        m = (1 << k) - 1
        n = _mirror_rice_get_32bit(br, m, k, maxsize)
        del_ = _zigzag_decode(n + zmode)
        out.append(del_)
        c += 1
        mb = pb * (n + zmode) + mb - ((pb * mb) >> QBSHIFT)
        if n > N_MAX_MEAN_CLAMP:
            mb = N_MEAN_CLAMP_VAL
        zmode = 0
        if ((mb << MMULSHIFT) < QB) and (c < num_samples):
            zmode = 1
            k2 = _lead(mb) - BITOFF + ((mb + MOFF) >> MDENSHIFT)
            mz = ((1 << k2) - 1) & wb
            run = _mirror_rice_get(br, mz, k2)
            out.extend([0] * run)
            c += run
            if run >= 65535:
                zmode = 0
            mb = 0
    return out


def _mirror_decode_cpe_frame(payload):
    br = _MirrorBitReader(payload)
    tag = br.read(3)
    assert tag == ID_CPE, tag
    br.read(4)   # elementInstanceTag
    unused = br.read(12)
    assert unused == 0
    header = br.read(4)
    partial = header >> 3
    n = FRAME_SIZE
    if partial:
        n = (br.read(16) << 16) | br.read(16)
    br.read(8)   # mixBits
    br.read(8)   # mixRes
    br.read(4); br.read(4)  # modeU, denShiftU
    br.read(3); numU = br.read(5)
    for _ in range(numU):
        br.read(16)
    br.read(4); br.read(4)  # modeV, denShiftV
    br.read(3); numV = br.read(5)
    for _ in range(numV):
        br.read(16)
    chan_bits = BIT_DEPTH + 1
    residuals_u = _mirror_dyn_decomp(br, n, PB, KB, chan_bits, MB)
    residuals_v = _mirror_dyn_decomp(br, n, PB, KB, chan_bits, MB)

    def integrate(residuals):
        out = [residuals[0]]
        for r in residuals[1:]:
            out.append(out[-1] + r)
        return out

    left = integrate(residuals_u)
    right = integrate(residuals_v)
    end_tag = br.read(3)
    assert end_tag == ID_END, end_tag
    return left, right


def _selftest():
    failures = []

    def check(name, cond):
        if not cond:
            failures.append(name)
            print(f"selftest: FAIL {name}")

    # --- BitWriter: known bit pattern -> known bytes ---
    bw = BitWriter()
    bw.write(0b101, 3)
    bw.write(0b11001, 5)
    check("bitwriter basic pack", bw.getvalue() == bytes([0b10111001]))

    bw2 = BitWriter()
    bw2.write(0, 0)  # zero-width write must be a no-op
    bw2.write(1, 1)
    check("bitwriter zero-width write is a no-op", bw2.getvalue() == bytes([0b10000000]))

    # --- sine generation ---
    tone = generate_tone(440.0, 1000, 44100, 100)
    check("sine starts at zero", tone[0] == 0)
    check("sine stays in range", all(-1000 <= s <= 1000 for s in tone))
    quarter_period_idx = round(44100 / 440.0 / 4)
    check("sine peaks near amplitude at quarter period",
          abs(tone[quarter_period_idx] - 1000) <= 5)

    # --- RTP header pack/parse round trip, and seq/timestamp progression ---
    hdr = build_rtp_data_header(65535, 0xfffffff0, True)
    check("rtp header length", len(hdr) == RTP_HEADER_LEN)
    check("rtp header byte0", hdr[0] == 0x80)
    check("rtp header type + marker", hdr[1] == (0x80 | RTP_TYPE_DATA))
    seq_back = struct.unpack_from(">H", hdr, 2)[0]
    rtp_back = struct.unpack_from(">I", hdr, 4)[0]
    check("rtp seqno round-trips", seq_back == 65535)
    check("rtp rtptime round-trips", rtp_back == 0xfffffff0)

    hdr_not_first = build_rtp_data_header(0, 0, False)
    check("rtp header marker bit only set for first packet", hdr_not_first[1] == RTP_TYPE_DATA)

    seq, rtp = 65534, 0xfffffffe
    for _ in range(4):
        seq = (seq + 1) & 0xffff
        rtp = (rtp + FRAME_SIZE) & 0xffffffff
    check("seqno wraps at 16 bits", seq == 2)
    check("rtptime wraps at 32 bits", rtp == (0xfffffffe + 4 * FRAME_SIZE) & 0xffffffff)

    # --- SDP construction ---
    sdp = build_sdp("192.0.2.1", "192.0.2.2")
    text = sdp.decode()
    expected_fmtp = f"a=fmtp:{FMTP_PAYLOAD_TYPE} {FRAME_SIZE} 0 {BIT_DEPTH} {PB} {MB} {KB} {CHANNELS} {MAX_RUN} 0 0 {SAMPLE_RATE}"
    check("sdp contains expected fmtp line", expected_fmtp in text)
    check("sdp never mentions rsaaeskey", "rsaaeskey" not in text.lower())
    check("sdp never mentions aesiv", "aesiv" not in text.lower())
    # --- timing/sync packet field round trip ---
    # Build a synthetic 0x52 request the way rtp.c's rtp_request_timing() does.
    req_pkt = bytearray(32)
    req_pkt[0] = 0x80
    req_pkt[1] = 0x52 | 0x80
    struct.pack_into(">I", req_pkt, 28, 424242)
    parsed_ref = parse_timing_request(bytes(req_pkt))
    check("timing request reference parses back", parsed_ref == 424242)
    resp = build_timing_response(parsed_ref)
    check("timing response echoes reference", struct.unpack_from(">I", resp, 12)[0] == 424242)

    sync_pkt = build_sync_packet(rtp_now=99999, rtp_now_latency=88888, first=True)
    check("sync packet length", len(sync_pkt) == 20)
    check("sync packet first-flag bit set", (sync_pkt[0] & 0x10) != 0)
    check("sync packet rtp_now_latency field", struct.unpack_from(">I", sync_pkt, 4)[0] == 88888)
    check("sync packet rtp_now field", struct.unpack_from(">I", sync_pkt, 16)[0] == 99999)

    # --- ALAC round trip via the pure-Python decode mirror ---
    import random as _random
    _random.seed(1234)
    cases = {
        "silence": ([0] * FRAME_SIZE, [0] * FRAME_SIZE),
        "sine-440-quiet": (generate_tone(440.0, 3277, 44100, FRAME_SIZE),) * 2,
        "sine-1000-fullscale": (generate_tone(1000.0, 32767, 44100, FRAME_SIZE),) * 2,
        "partial-frame": (generate_tone(440.0, 3277, 44100, 137),) * 2,
        "zero-runs": (
            [0] * 50 + [100] * 5 + [0] * 200 + [-100] * 5 + [0] * (FRAME_SIZE - 260),
            [0] * FRAME_SIZE,
        ),
        "random-noise": (
            [_random.randint(-32768, 32767) for _ in range(FRAME_SIZE)],
            [_random.randint(-32768, 32767) for _ in range(FRAME_SIZE)],
        ),
    }
    for name, (left, right) in cases.items():
        try:
            payload = encode_cpe_frame(left, right)
        except RuntimeError as exc:
            # Incompressible content (e.g. random noise) legitimately can
            # exceed MAX_PACKET even with real prediction+entropy coding -
            # that is the packet-size guard doing its job, not a bug. Any
            # other case raising here is a real failure.
            if name == "random-noise" and "MAX_PACKET" in str(exc):
                print(f"selftest: {name} correctly rejected as oversized ({exc})")
                continue
            check(f"alac roundtrip {name} (encode)", False)
            continue
        dl, dr = _mirror_decode_cpe_frame(payload)
        check(f"alac roundtrip {name} (left)", dl == left)
        check(f"alac roundtrip {name} (right)", dr == right)

    # --- packet-size guard actually fires ---
    try:
        huge = [32767 if i % 2 == 0 else -32768 for i in range(FRAME_SIZE)]
        encode_cpe_frame(huge, huge)
        check("packet-size guard rejects incompressible content", False)
    except RuntimeError as exc:
        check("packet-size guard rejects incompressible content", "MAX_PACKET" in str(exc))

    if failures:
        print(f"selftest: {len(failures)} FAILURE(S): {failures}")
        sys.exit(1)
    print("selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("host", nargs="?", help="board address (IP or hostname), e.g. 192.168.1.50")
    ap.add_argument("--rtsp-port", type=int, default=RAOP_PORT,
                    help=f"RTSP port (default {RAOP_PORT}, matches raop.c raop_create())")
    ap.add_argument("--freq", type=float, default=440.0, help="tone frequency in Hz (default 440)")
    ap.add_argument("--duration", type=float, default=5.0, help="tone duration in seconds (default 5)")
    ap.add_argument("--amplitude", type=int, default=3277,
                    help="peak amplitude, 1..32767 (default 3277, ~10%% of full scale / "
                         "-20dBFS - conservative default since this drives a real speaker "
                         "in someone's home; raise with care)")
    ap.add_argument("--sync-interval", type=float, default=1.0,
                    help="seconds between RTP sync (0x54) packets (default 1.0; rtp.c's own "
                         "sync handler re-requests NTP roughly every 3s once synced, so 1s "
                         "keeps well ahead of that with margin to spare)")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the RTSP conversation and packet construction without touching the network")
    ap.add_argument("--challenge", action="store_true",
                    help="send an Apple-Challenge on OPTIONS and require a signed "
                         "256-byte Apple-Response, the way a real sender does; "
                         "exercises the RSA path (and its stack cost) that the "
                         "default run skips entirely")
    ap.add_argument("--pubkey", metavar="PEM",
                    help="encrypt the stream the way a real sender does: wrap a random "
                         "AES-128 key with this RSA PUBLIC key (RSA-OAEP-SHA1), send it "
                         "as rsaaeskey with a random aesiv, and AES-128-CBC every RTP "
                         "payload. This is the only way to exercise the receiver's "
                         "decrypt path without a real iPhone. Derive the public half "
                         "with: openssl rsa -in <private>.pem -pubout -out pub.pem")
    ap.add_argument("--selftest", action="store_true", help="run internal self-tests and exit")
    ap.add_argument("--quiet", dest="verbose", action="store_false", default=True,
                    help="suppress the RTSP/timing/sync exchange log")
    args = ap.parse_args()

    if args.selftest:
        _selftest()
        return

    if not args.host:
        ap.error("host is required unless --selftest")

    try:
        run(args)
    except (RtspError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
