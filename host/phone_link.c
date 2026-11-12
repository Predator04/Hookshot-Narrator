/* (local) The phone ocarina: a small UDP link from the Android app on the local network.

   Port 43210. Every packet starts "OCA1" and a type byte:
     1 hello    (no body)            -> answered "OCA1", 0x81, listening (1: the game has a microphone stream started)
     2 audio    int16 LE mono 48 kHz    (what the game hears in place of this PC's microphone while it arrives:
                                         nn_audio_in.c takes it with hos_phone_take)
     3 buttons  uint32 LE, nn::hid bits (A 0, B 1, X 2, Y 3, L 6, R 7...); forgotten after 1 s without a packet

   Only a sender on a private address is heard, and one at a time: another phone is taken after 5 s of silence.
   HOS_PHONE=0: no link. */
#if !defined(__linux__)
#include <winsock2.h>
#endif
#include "hos.h"
#include <stdlib.h>
#include <string.h>

int hos_mic_listening(void);                  /* nn_audio_in.c */
void hos_host_thread_register_self(const char *name);

#if !defined(__linux__)
enum { PORT = 43210, MAX_SAMPLES = 960, RING = 48000 };
static volatile LONG buttons;
static volatile LONG64 buttons_at, audio_at;
static SRWLOCK ring_lock = SRWLOCK_INIT;
static int16_t ring[RING];
static int ring_start, ring_len;

static int is_private(uint32_t a) {   /* (a: host order) */
    return (a >> 24) == 10 || (a >> 20) == (172 << 4 | 1) || (a >> 16) == (192 << 8 | 168);
}

static void ring_put(const int16_t *s, int n) {   /* (the oldest dropped when full) */
    AcquireSRWLockExclusive(&ring_lock);
    for (int i = 0; i < n; i++) {
        if (ring_len == RING) ring_start = (ring_start + 1) % RING, ring_len--;
        ring[(ring_start + ring_len) % RING] = s[i];
        ring_len++;
    }
    ReleaseSRWLockExclusive(&ring_lock);
}

int hos_phone_take(int16_t *out, int max) {
    AcquireSRWLockExclusive(&ring_lock);
    const int n = ring_len < max ? ring_len : max;
    for (int i = 0; i < n; i++) out[i] = ring[(ring_start + i) % RING];
    ring_start = (ring_start + n) % RING, ring_len -= n;
    ReleaseSRWLockExclusive(&ring_lock);
    return n;
}

static DWORD WINAPI phone_main(void *p) {
    (void)p;
    hos_host_thread_register_self("hos_phone");
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        hos_log("phone: no sockets");
        return 0;
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in me = {0};
    me.sin_family = AF_INET;
    me.sin_port = htons(PORT);
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (s == INVALID_SOCKET || bind(s, (struct sockaddr *)&me, sizeof me)) {
        hos_log("phone: port %d could not be opened (error %d)", PORT, WSAGetLastError());
        return 0;
    }
    hos_log("phone: waiting on UDP port %d", PORT);
    uint32_t peer = 0;
    int64_t peer_at = 0, last_log = 0, audio_packets = 0, peak = 0, state_at = 0;
    static unsigned char buf[8 + MAX_SAMPLES * 2];
    static int16_t samples[MAX_SAMPLES];
    for (;;) {
        struct sockaddr_in from;
        int len = sizeof from;
        const int n = recvfrom(s, (char *)buf, sizeof buf, 0, (struct sockaddr *)&from, &len);
        if (n < 5 || memcmp(buf, "OCA1", 4)) continue;
        const uint32_t ip = ntohl(from.sin_addr.s_addr);
        if (!is_private(ip)) continue;
        const int64_t now = hos_now_ns();
        if (peer != ip) {
            if (peer && now - peer_at < 5000000000LL) continue;
            peer = ip;
            hos_log("phone: connected from %u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
        }
        peer_at = now;
        switch (buf[4]) {
        case 1: {
            unsigned char r[6];
            memcpy(r, "OCA1", 4);
            r[4] = 0x81;
            r[5] = (unsigned char)hos_mic_listening();
            sendto(s, (const char *)r, sizeof r, 0, (struct sockaddr *)&from, sizeof from);
            break;
        }
        case 2: {
            const int count = (n - 5) / 2;
            if (count <= 0 || count > MAX_SAMPLES) break;
            memcpy(samples, buf + 5, (size_t)count * 2);
            InterlockedExchange64(&audio_at, now);
            ring_put(samples, count);
            audio_packets++;
            for (int i = 0; i < count; i++) {
                const int a = samples[i] < 0 ? -samples[i] : samples[i];
                if (a > peak) peak = a;
            }
            if (now - last_log > 5000000000LL) {
                hos_log("phone: %lld audio packets, peak level %lld of 32767, game listening: %d", (long long)audio_packets,
                        (long long)peak, hos_mic_listening());
                last_log = now, audio_packets = 0, peak = 0;
            }
            break;
        }
        case 3:
            if (n >= 9) {
                uint32_t m;
                memcpy(&m, buf + 5, 4);
                if (m != (uint32_t)buttons) hos_log("phone: buttons %#x", m);
                InterlockedExchange(&buttons, (LONG)m);
                InterlockedExchange64(&buttons_at, now);
            }
            break;
        }
        /* (local) The companion screen: Link's state back to the phone, five times a second while it sends anything.
           "OCA1", 0x83, valid u8, life i16, max life i16, rupees i16, clock i32 (seconds since midnight, -1), hurt count u16 */
        if (now - state_at >= 200000000LL) {
            HosOotHud h;
            const int valid = hos_oot_hud(&h);
            unsigned char r[18];
            memcpy(r, "OCA1", 4);
            r[4] = 0x83;
            r[5] = (unsigned char)valid;
            const int16_t life = (int16_t)(valid ? h.life : 0), max = (int16_t)(valid ? h.max_life : 0),
                          rupees = (int16_t)(valid ? h.rupees : 0);
            const int32_t clock = valid ? h.clock : -1;
            const uint16_t hurt = (uint16_t)h.hurt_seq;
            memcpy(r + 6, &life, 2), memcpy(r + 8, &max, 2), memcpy(r + 10, &rupees, 2);
            memcpy(r + 12, &clock, 4), memcpy(r + 16, &hurt, 2);
            sendto(s, (const char *)r, sizeof r, 0, (struct sockaddr *)&from, sizeof from);
            state_at = now;
        }
    }
}

void hos_phone_start(void) {
    static LONG started;
    if (getenv("HOS_PHONE") && atoi(getenv("HOS_PHONE")) == 0) return;
    if (!InterlockedExchange(&started, 1)) CreateThread(NULL, 0, phone_main, NULL, 0, NULL);
}
uint64_t hos_phone_buttons(void) {
    if (hos_now_ns() - buttons_at > 1000000000LL) return 0;
    return (uint32_t)buttons;
}
int hos_phone_audio_active(void) { return audio_at && hos_now_ns() - audio_at < 300000000LL; }
#else
void hos_phone_start(void) {}
uint64_t hos_phone_buttons(void) { return 0; }
int hos_phone_audio_active(void) { return 0; }
int hos_phone_take(int16_t *out, int max) { (void)out; (void)max; return 0; }
#endif
