/* Game controllers through SDL (host/hos_sdl.c: SDL3, else SDL2, third_party/sdl2/SDL2.dll; zlib license, loaded at
   run time): DualSense, DualShock 4, Switch Pro, Xbox and most other pads, over USB or Bluetooth. XInput alone sees
   only Xbox-style pads.

   A thread of its own (on Linux the window's, which runs SDL there too) opens every controller SDL knows (and ones
   plugged in later), polls them every 4 ms and keeps
   the merged state; nn_hid.c ORs it into the input with XInput, keyboard and mouse. Without SDL nothing
   changes. HOS_NO_SDL=1 leaves SDL out. The inputs go by position (HOS_PAD_*, SDL's own order: the bottom face button
   first, cross on a DualSense, A on an Xbox pad); nn_hid.c's controls make them the Switch's buttons.

   (hookshot) Motion: with the Insert menu's Motion controls on (the default), the open pad with a gyroscope and an
   accelerometer (Switch Pro and pads in Switch mode, DualSense, DualShock 4, others) that was used last has them read
   each poll and turned into the six-axis state OoT aims with (host/hos_motion.h), which nn_hid.c's
   GetSixAxisSensorStates gives the game; Motion sensitivity X/Y and Invert motion X/Y apply there. Off, or with no such pad, the game sees the pad
   lying still, as before. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "hos_sdl.h"
#include "hd_rumble.h"
#include "hos_motion.h"
#include "../gpu/vk/vk_api.h"   /* (HOS_PAD_*) */

void hos_host_thread_register_self(const char *name);   /* hos_prof.c */

/* (BTN_A ... BTN_RIGHT are HOS_PAD_SOUTH ... HOS_PAD_RIGHT) */
enum { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_BACK, BTN_GUIDE, BTN_START, BTN_LSTICK, BTN_RSTICK, BTN_LSHOULDER, BTN_RSHOULDER,
       BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_MISC1, BTN_TOUCHPAD = 20, BTN_N = 21 };
enum { AX_LX, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT };

#define MAX_PADS 8
/* (motion: whether it has the sensors, whether they are on, when to try again after they would not turn on; used_ns:
   its last button or stick change; serial: which opening this is, so a pad reconnecting into the slot is a new one;
   bias: its gyroscope's learned resting rate, kept while another pad has motion) */
static struct {
    void *gc;
    int32_t id;
    uint16_t rumble_lo, rumble_hi;
    int64_t rumble_ns;
    uint8_t hd, hd_kind, hd_ever;   /* HD rumble (hd_rumble.h): HD_RUMBLE_UNKNOWN / ON / OFF, the pad's kind, sent once */
    uint8_t hd_sent[HD_RUMBLE_PACKET_SIZE];
    int64_t hd_ns;
    int motion, motion_on;
    int64_t motion_retry_ns, used_ns;
    uint64_t input_sig;
    unsigned rested;   /* (hookshot) the sticks' axes seen at rest since it was opened, a bit each: only those steer */
    signed char held[4];   /* (the way each axis was last held, -1, 0 or 1: one held the other way has passed its rest) */
    uint32_t serial;
    float bias[3], bias_t;
    int has_bias;
    uint32_t led_rgb;   /* (local) the light bar colour last sent */
    int64_t led_ns, hit_until;   /* (local) hit_until: a hit's rumble plays until then */
    int ds5;   /* (local) a DualSense: -1 not known yet, 0 no, 1 yes */
    uint32_t ds5_sent;   /* (local) player lights and trigger last sent, 0xffffffff none */
    int64_t ds5_ns;
} pads[MAX_PADS];

/* (local) The light bar from Link's hearts (hos_oot_hud): green when full, through yellow to red; under a quarter it
   pulses red, and a hit flashes it white-red for a moment. Outside play (title, menus, loading) a dim blue.
   HOS_LIGHTBAR=0: left alone. */
int rx_option(int id, int otherwise);
/* (local) the Insert menu's controller extras (gpu/vk/vk_api.h HOS_OPT_LIGHT_BAR and on) */
enum { OPT_LIGHT_BAR = 60, OPT_HIT_RUMBLE, OPT_PLAYER_LIGHTS, OPT_PLAYER_LIGHT_BRIGHTNESS, OPT_LOW_MAGIC_BLINK, OPT_TRIGGER_RESISTANCE };
static uint32_t light_bar_colour(int64_t now) {
    HosOotHud s;
    if (!hos_oot_hud(&s) || s.max_life <= 0) return 0x000020;
    float f = (float)s.life / (float)s.max_life;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    float r, g, b = 0;
    if (f >= 0.5f) r = (1.0f - f) * 2.0f, g = 1.0f;   /* green -> yellow */
    else r = 1.0f, g = f * 2.0f;                     /* yellow -> red */
    /* (the light bar's green is far brighter than its red: squared, the mixes stay red-orange, not washed out) */
    r = r * r, g = g * g * (f >= 0.5f ? 1.0f : 0.6f);
    float level = 1.0f;
    if (s.life > 0 && f <= 0.25f) {   /* a heartbeat: 1.4 beats a second, never fully dark */
        const double t = (double)(now % 714000000LL) / 714000000.0;
        level = 0.25f + 0.75f * (float)(t < 0.15 ? t / 0.15 : t < 0.5 ? 1.0 - (t - 0.15) / 0.35 : 0.0);
        g = 0;   /* (pure red) */
    }
    if (s.hurt_ns && now - s.hurt_ns < 250000000LL) r = 1.0f, g = 0, b = 0, level = 1.0f;   /* a hit: full red */
    const int R = (int)(r * level * 255.0f + 0.5f), G = (int)(g * level * 255.0f + 0.5f), B = (int)(b * level * 255.0f + 0.5f);
    return (uint32_t)(R << 16 | G << 8 | B);
}

static void light_bar_poll(int i, void *gc, int64_t now) {
    static int off = -1;
    if (off < 0) off = getenv("HOS_LIGHTBAR") && atoi(getenv("HOS_LIGHTBAR")) == 0;
    if (off) return;
    /* (the menu's Light bar Off: the system's blue, as on exit) */
    const uint32_t rgb = rx_option(OPT_LIGHT_BAR, 1) ? light_bar_colour(now) : 0x000040;
    /* (a change at most every 40 ms, the same colour again every 2 s: a pad reconnecting keeps it) */
    if (now - pads[i].led_ns < (rgb == pads[i].led_rgb ? 2000000000LL : 40000000LL)) return;
    if (hos_sdl_pad_led(gc, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb)) {
        static int said;
        if (!said) said = 1, hos_log("hid: light bar follows Link's hearts (HOS_LIGHTBAR=0: off)");
        pads[i].led_rgb = rgb;
    }
    pads[i].led_ns = now;
}

/* (local) A jolt on each hit, and a soft heartbeat under a quarter of the hearts, timed with the light bar's pulse. Scaled
   by the Insert menu's Rumble strength (0: none). While one plays, the game's own rumble waits (pads_poll). HOS_HIT_RUMBLE=0: off */
/* (local) DualSense extras through SDL's raw effect (SDL_SendGamepadEffect, the DS5 output effects block, 47 bytes):
   the five player lights under the touchpad as a meter (Link's magic, or his hearts until the magic is known), and the
   right trigger's resistance while he aims in first person (a bowstring: stiff past a third of the pull, giving at the end).
   SDL keeps its own player light off (pads_start's hint). HOS_DS5_EXTRAS=0: off */
static void ds5_extras_poll(int i, void *gc, int64_t now) {
    static int off = -1;
    if (off < 0) off = getenv("HOS_DS5_EXTRAS") && atoi(getenv("HOS_DS5_EXTRAS")) == 0;
    if (off) return;
    if (pads[i].ds5 < 0) {
        const char *name = hos_sdl_pad_name(gc);
        pads[i].ds5 = name && strstr(name, "DualSense") != NULL;
        pads[i].ds5_sent = 0xffffffffu;
    }
    if (!pads[i].ds5) return;
    HosOotHud s;
    const int live = hos_oot_hud(&s);
    unsigned lights = 0x04;   /* (outside play: the middle one, as player 1) */
    static unsigned kept = 0x04;
    static int kept_low;
    static int64_t kept_ns;
    const int mode = rx_option(OPT_PLAYER_LIGHTS, 1);   /* (the menu's Player lights: 0 off, 1 magic, 2 hearts) */
    int low = 0;   /* (magic nearly gone: the last light blinks) */
    if (live && s.max_life > 0) {
        const int magic = mode == 1 && s.max_magic > 0;
        const int have = magic ? s.magic : s.life, max = magic ? s.max_magic : s.max_life;
        /* (a share of the meter, so a double meter works the same: each light a fifth, to the nearest, and one light while
           anything is left) */
        int n = have <= 0 ? 0 : (have * 10 + max) / (2 * max);
        if (have > 0 && n < 1) n = 1;
        lights = (1u << (n > 5 ? 5 : n)) - 1;
        low = magic && n == 1;
        kept = lights, kept_low = low, kept_ns = now;
    } else if (now - kept_ns < 20000000000LL) {
        lights = kept, low = kept_low;   /* (Link pauses his update while a spell plays, a door opens or a menu is up: the meter stays) */
    }
    if (low && rx_option(OPT_LOW_MAGIC_BLINK, 1) && now % 600000000LL >= 350000000LL) lights = 0;   /* (on 350 ms, off 250) */
    if (!mode) lights = 0;
    int bright = rx_option(OPT_PLAYER_LIGHT_BRIGHTNESS, 0);   /* (0 high, 1 medium, 2 low, as the controller has them) */
    if (bright < 0 || bright > 2) bright = 0;
    /* (the menu's Trigger resistance, off unless chosen; HOS_DS5_TRIGGER=1 as well) */
    static int bow_env = -1;
    if (bow_env < 0) bow_env = getenv("HOS_DS5_TRIGGER") && atoi(getenv("HOS_DS5_TRIGGER")) == 1;
    const unsigned aim = (bow_env || rx_option(OPT_TRIGGER_RESISTANCE, 0)) && live && s.aiming;
    const uint32_t want = lights | aim << 8 | (uint32_t)bright << 9;
    if (want == pads[i].ds5_sent && now - pads[i].ds5_ns < 3000000000LL) return;   /* (again every 3 s: a reconnect keeps it) */
    if (want != pads[i].ds5_sent && now - pads[i].ds5_ns < 30000000LL) return;
    uint8_t e[47] = {0};
    e[0] = 0x04;   /* right trigger effect */
    e[1] = 0x10;   /* player lights */
    if (aim) {     /* "weapon": resistance from zone 3 to zone 8 of 10, strength 6 of 8, then it snaps free */
        e[10] = 0x25;
        const unsigned zones = 1u << 3 | 1u << 8;
        e[11] = (uint8_t)zones, e[12] = (uint8_t)(zones >> 8);
        e[13] = 5;
    } else {
        e[10] = 0x05;   /* off: a free trigger */
    }
    e[43] = (uint8_t)lights;
    e[38] = 0x01;   /* the player lights' brightness, */
    e[42] = (uint8_t)bright;
    if (hos_sdl_pad_effect(gc, e, sizeof e)) {
        if (want != pads[i].ds5_sent) {
            static int said;
            if (!said) said = 1, hos_log("hid: DualSense player lights and trigger follow Link (HOS_DS5_EXTRAS=0: off)");
        }
        pads[i].ds5_sent = want;
    }
    pads[i].ds5_ns = now;
}

/* (local) Leaving: the controllers as the system shows them, a blue light bar, the middle player light, free triggers, no
   rumble. The poll thread stops sending its own first (pads_leaving). Called by host/hos_relaunch.c and hos_svc.c on exit. */
static volatile LONG pads_leaving;
void hos_pads_restore(void) {
    if (InterlockedExchange(&pads_leaving, 1)) return;
    Sleep(10);   /* (a poll in flight finishes) */
    int n = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        void *gc = pads[i].gc;
        if (!gc) continue;
        hos_sdl_pad_rumble(gc, 0, 0, 0);
        hos_sdl_pad_led(gc, 0, 0, 64);
        if (pads[i].ds5 > 0) {
            uint8_t e[47] = {0};
            e[0] = 0x04 | 0x08, e[1] = 0x10, e[10] = 0x05, e[21] = 0x05, e[43] = 0x04, e[38] = 0x01, e[42] = 0;   /* (bright again) */
            hos_sdl_pad_effect(gc, e, sizeof e);
        }
        n++;
    }
    if (n) hos_log("hid: controller lights and triggers put back for the system (%d controller%s)", n, n == 1 ? "" : "s");
}

static void rumble_strength(unsigned heavy, unsigned light, int percent, uint16_t out[2]);
static void hit_rumble_poll(int i, void *gc, int64_t now) {
    int rx_option(int id, int otherwise);
    static int off = -1;
    static unsigned seen[MAX_PADS];
    static int64_t beat_at[MAX_PADS];
    if (off < 0) off = getenv("HOS_HIT_RUMBLE") && atoi(getenv("HOS_HIT_RUMBLE")) == 0;
    HosOotHud s;
    const int live = hos_oot_hud(&s);
    const int mode = rx_option(OPT_HIT_RUMBLE, 2);   /* (the menu's Hit rumble: 0 off, 1 hits, 2 and the low-hearts beat) */
    if (off || !live || !mode) { seen[i] = s.hurt_seq; return; }
    const int strength = rx_option(9, 100);
    if (strength <= 0) { seen[i] = s.hurt_seq; return; }
    uint16_t m[2];
    if (s.hurt_seq != seen[i]) {
        seen[i] = s.hurt_seq;
        rumble_strength(0xD000, 0xA000, strength, m);
        hos_sdl_pad_rumble(gc, m[0], m[1], 220);
        pads[i].hit_until = now + 220000000LL;
        return;
    }
    if (mode >= 2 && s.max_life > 0 && s.life > 0 && s.life * 4 <= s.max_life && now >= pads[i].hit_until) {
        const int64_t beat = now - now % 714000000LL;   /* (the light bar's beat) */
        if (beat != beat_at[i]) {
            beat_at[i] = beat;
            rumble_strength(0x5000, 0x2000, strength, m);
            hos_sdl_pad_rumble(gc, m[0], m[1], 90);
            pads[i].hit_until = now + 90000000LL;
        }
    }
}
static uint32_t pads_opened;
static SRWLOCK state_lock = SRWLOCK_INIT;
static uint64_t state_buttons;
static int32_t state_stick[4];
static int state_count;
static char state_name[128];   /* the first controller's name (host/prompts.c: the button prompts' Auto) */
static volatile LONG rumble_low, rumble_high;   /* 0..65535, set by the vibration HLE */
/* the game's own vibration values per side (0 left, 1 right: {amp_low, freq_low, amp_high, freq_high}), for the pads
   that play HD rumble; silence until the game sends one */
static SRWLOCK hd_lock = SRWLOCK_INIT;
static float hd_values[2][4] = {{0.0f, 160.0f, 0.0f, 320.0f}, {0.0f, 160.0f, 0.0f, 320.0f}};
/* (motion) the pad read and its filter (the polling thread's own), then what the game is handed (under state_lock): the
   last HOS_MOTION_HISTORY readings, newest at motion_hist[motion_head]; gyro_status for the menu */
static HosMotion motion;
static int64_t motion_ns, motion_reading_ns, motion_log_ns, motion_readings, motion_start_ns;
static int motion_warned;
static int motion_pad = -1;
static uint32_t motion_serial;
static HosMotionSample motion_hist[HOS_MOTION_HISTORY];
static int motion_head, motion_count, motion_live;
static volatile LONG gyro_status;   /* 0 no controller, 1 controllers but none with a gyro, 2 one with a gyro */

int32_t hos_stick_axis(int16_t v, int zone);   /* nn_hid.c: the menu's Stick dead zone, as XInput's pads */

static int32_t stick_value(int16_t v, int flip, int zone) {
    const int32_t a = hos_stick_axis(v, zone);
    return flip ? -a : a;
}

/* SDL's controller support started; 0 when it could not be */
static int pads_start(void) {
    /* poll in the background, whatever window is in front (nn_hid.c decides when input counts) */
    hos_sdl_hint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    /* (SDL2: PlayStation pads' rumble asked for; SDL3 turns it on when a rumble is first sent) */
    hos_sdl_hint("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", "1");
    hos_sdl_hint("SDL_JOYSTICK_HIDAPI_PS5_PLAYER_LED", "0");   /* (local: the player lights are Link's meter, ds5_extras_poll) */
    hos_sdl_hint("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "1");
    /* (the map below is by position; SDL2 reports Nintendo pads by their labels unless told not to, which put a Switch Pro
       Controller's A, on the right, at the bottom button's place: A acted as B and X as Y: SkyboundTotk issue 12. SDL3's
       are by position always) */
    hos_sdl_hint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");
    /* (with no window nothing reads SDL's quit event: SDL's own SIGINT/SIGTERM handlers turned a timeout's or a service
       manager's SIGTERM into that event, and the run went on until SIGKILL. Without them the signals end it as usual) */
    if (getenv("HOS_HEADLESS")) hos_sdl_hint("SDL_NO_SIGNAL_HANDLERS", "1");
    if (!hos_sdl_init(HOS_SDL_GAMEPAD)) {
        hos_log("hid: SDL could not start its controller support: %s", hos_sdl_error());
        return 0;
    }
    hos_log("hid: SDL controller support ready (DualSense, DualShock 4, Switch Pro, Xbox and others)");
    return 1;
}

/* SDL_GameControllerRumble runs only for the duration it is given, then stops, and a repeat of the same numbers was
   skipped. A rumble the game holds, and one that was already playing when this controller opened, never reached it. */
static int rumble_due(unsigned lo, unsigned hi, unsigned sent_lo, unsigned sent_hi, long long now, long long sent_ns) {
    if (lo != sent_lo || hi != sent_hi) return 1;
    if ((lo || hi) && now - sent_ns >= 500000000LL) return 1;   /* (half of the 1 s effect, so it does not lapse) */
    return 0;
}

/* The Insert menu's Rumble strength (percent; 0 Off) on the motors' 0..65535, heavy and light together: up to 100 each
   motor is scaled on its own (100 sends them as they are); above, when the stronger would pass the motor's top, both
   are scaled by the same factor so that it just reaches it, keeping the balance between the motors that the frequency
   split chose. Off stops a rumble that is playing (the poll sends the change). */
static void rumble_strength(unsigned heavy, unsigned light, int percent, uint16_t out[2]) {
    if (percent <= 0) {
        out[0] = out[1] = 0;
        return;
    }
    unsigned long long h = (unsigned long long)heavy * (unsigned)percent / 100u;
    unsigned long long l = (unsigned long long)light * (unsigned)percent / 100u;
    if (h > 0xFFFFu || l > 0xFFFFu) {   /* (from the motors' own values: the same factor for both, the stronger at the top) */
        const unsigned long long top = heavy > light ? heavy : light;
        h = heavy * 0xFFFFull / top;
        l = light * 0xFFFFull / top;
    }
    out[0] = (uint16_t)h;
    out[1] = (uint16_t)l;
}

/* A pad SDL just opened: HD rumble when it is a Switch Pro Controller or a Joy-Con and the SDL has the effect call
   (HOS_HD_RUMBLE=0: never); whether SDL's driver takes it shows with the first packet. */
static void hd_open(int i, void *gc) {
    static int env_off = -1;
    if (env_off < 0) {
        const char *e = getenv("HOS_HD_RUMBLE");
        env_off = e && !strcmp(e, "0");
    }
    pads[i].hd_kind = (uint8_t)hd_rumble_kind(hos_sdl_load(), hos_sdl_pad_type(gc));
    pads[i].hd = (uint8_t)hd_rumble_start(pads[i].hd_kind, hos_sdl_pad_has_effect(), env_off);
    pads[i].hd_ever = 0;
    pads[i].hd_ns = 0;
    const uint8_t n[4] = {0x00, 0x01, 0x40, 0x40};
    hd_rumble_packet(HD_RUMBLE_PRO, n, n, pads[i].hd_sent);
}

/* A pad's HD rumble, each poll: its packet when due (hd_rumble_due). 1 when the pad plays HD rumble (the motor rumble
   is not sent to it); 0 when it does not, or SDL just refused it (then it gets the motor rumble from now on). side:
   the left side's bytes, the right's, and both merged (for a single Joy-Con, which has only one). */
static int hd_poll(int i, const uint8_t side[3][4], long long now) {
    if (pads[i].hd == HD_RUMBLE_OFF) return 0;
    uint8_t packet[HD_RUMBLE_PACKET_SIZE];
    const int single = pads[i].hd_kind == HD_RUMBLE_JOYCON_LEFT || pads[i].hd_kind == HD_RUMBLE_JOYCON_RIGHT;
    hd_rumble_packet(pads[i].hd_kind, single ? side[2] : side[0], single ? side[2] : side[1], packet);
    if (!hd_rumble_due(packet, pads[i].hd_sent, pads[i].hd_ever, now, pads[i].hd_ns)) return 1;
    const int was = pads[i].hd;
    pads[i].hd = (uint8_t)hd_rumble_after_send(hos_sdl_pad_effect(pads[i].gc, packet, sizeof packet));
    if (pads[i].hd == HD_RUMBLE_OFF) {   /* (refused: HD rumble off for this pad until it is closed and opened again.
                                           SDL3 queues the write, so false means the driver does not support the effect,
                                           not a write that failed once) */
        hos_log("hid: \"%s\" did not take HD rumble (%s): its rumble goes to its motors", hos_sdl_pad_name(pads[i].gc),
                hos_sdl_error());
        pads[i].rumble_lo = pads[i].rumble_hi = 0;   /* (the motor path sends what is playing at once) */
        pads[i].rumble_ns = 0;
        return 0;
    }
    if (was == HD_RUMBLE_UNKNOWN) hos_log("hid: HD rumble on \"%s\"", hos_sdl_pad_name(pads[i].gc));
    memcpy(pads[i].hd_sent, packet, sizeof packet);
    pads[i].hd_ns = now;
    pads[i].hd_ever = 1;
    return 1;
}

/* (motion) the sensors on while Motion controls is on (HOS_OPT_GYRO, on without the menu), off otherwise; the pad with
   them that was used last read into the filter. count: the pads open */
static void motion_poll(int64_t now, int count) {
    int rx_option(int id, int otherwise);
    static int debug = -1;   /* (HOS_MOTION_DEBUG, read once) */
    if (debug < 0) debug = getenv("HOS_MOTION_DEBUG") != NULL;
    const int want = rx_option(HOS_OPT_GYRO, 1) != 0;
    int pick = -1, any = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pads[i].gc || !pads[i].motion) continue;
        any = 1;
        if (pads[i].motion_on != want && now >= pads[i].motion_retry_ns) {
            const int ok = hos_sdl_pad_sensor_enable(pads[i].gc, HOS_SDL_SENSOR_ACCEL, want) &
                           hos_sdl_pad_sensor_enable(pads[i].gc, HOS_SDL_SENSOR_GYRO, want);
            if (want && !ok) {   /* (both off again, and another try in two seconds: a pad over Bluetooth may still be
                                     starting) */
                hos_sdl_pad_sensor_enable(pads[i].gc, HOS_SDL_SENSOR_ACCEL, 0);
                hos_sdl_pad_sensor_enable(pads[i].gc, HOS_SDL_SENSOR_GYRO, 0);
                pads[i].motion_on = 0;
                if (!pads[i].motion_retry_ns)
                    hos_log("hid: \"%s\": its motion sensors could not be turned on yet, trying again",
                            hos_sdl_pad_name(pads[i].gc));
                pads[i].motion_retry_ns = now + 2000000000LL;
            } else {
                pads[i].motion_on = want;
                pads[i].motion_retry_ns = 0;
            }
        }
        if (want && pads[i].motion_on && (pick < 0 || pads[i].used_ns > pads[pick].used_ns)) pick = i;
    }
    InterlockedExchange(&gyro_status, any ? 2 : count ? 1 : 0);
    float a[3], g[3];
    const int read = pick >= 0 && hos_sdl_pad_sensor(pads[pick].gc, HOS_SDL_SENSOR_ACCEL, a, 3) &&
                     hos_sdl_pad_sensor(pads[pick].gc, HOS_SDL_SENSOR_GYRO, g, 3);
    /* (the pad that had motion keeps what its filter learned of its gyroscope's bias, for when it has motion again) */
    if (motion_pad >= 0 && (!read || pick != motion_pad) && pads[motion_pad].gc && pads[motion_pad].serial == motion_serial &&
        motion.learned_t > 0) {
        memcpy(pads[motion_pad].bias, motion.bias, sizeof motion.bias);
        pads[motion_pad].bias_t = motion.learned_t, pads[motion_pad].has_bias = 1;
    }
    if (!read) {
        motion_pad = -1;
        AcquireSRWLockExclusive(&state_lock);
        motion_live = 0, motion_count = 0;
        ReleaseSRWLockExclusive(&state_lock);
        return;
    }
    int fresh = 0;
    if (pick != motion_pad || pads[pick].serial != motion_serial) {   /* (another pad, or one reconnected: a fresh filter) */
        hos_motion_reset(&motion);
        if (pads[pick].has_bias)
            memcpy(motion.bias, pads[pick].bias, sizeof motion.bias), motion.learned_t = pads[pick].bias_t;
        motion_pad = pick, motion_serial = pads[pick].serial, motion_ns = now;
        fresh = 1;
        motion_start_ns = now, motion_warned = 0;
        hos_log("hid: motion controls from \"%s\"", hos_sdl_pad_name(pads[pick].gc));
    }
    /* the filter runs outside the lock (the game's input calls take it); a repeat of the last reading is no reading */
    const int64_t dt_ns = now - motion_ns;
    motion_ns = now;
    {   /* (the Motion deadzone row: the console's tight, standard and loose zero-drift thresholds, turns a second) */
        static const float zone[3] = {HOS_MOTION_THRESHOLD_TIGHT, HOS_MOTION_THRESHOLD, HOS_MOTION_THRESHOLD_LOOSE};
        const int z = rx_option(HOS_OPT_GYRO_DEADZONE, 1);
        motion.threshold = zone[z < 0 ? 0 : z > 2 ? 2 : z];
    }
    if (!hos_motion_update(&motion, a, g, (float)(dt_ns / 1e9), rx_option(HOS_OPT_GYRO_SENS_X, 100) / 100.0f,
                           rx_option(HOS_OPT_GYRO_SENS_Y, 100) / 100.0f, rx_option(HOS_OPT_GYRO_INV_X, 0),
                           rx_option(HOS_OPT_GYRO_INV_Y, 0)) && !fresh)
        return;
    if (!motion_warned && motion.learned_t <= 0 && now - motion_start_ns > 30000000000LL) {
        motion_warned = 1;   /* (a pad noisier than the stillness test allows: its drift is never taken off) */
        hos_log("hid: motion: \"%s\" has not been set down yet, so its gyroscope's drift is not learned: set it on a "
                "table for a second (if it is on a table and this stays, its sensor may be noisier than the stillness "
                "test allows)", hos_sdl_pad_name(pads[pick].gc));
    }
    if (debug && now >= motion_log_ns) {   /* (twice a second: what the game gets) */
        motion_log_ns = now + 500000000LL;
        hos_log("hid: motion: accel (%+.2f, %+.2f, %+.2f) G, gyro (%+.3f, %+.3f, %+.3f) turns/s, direction rows "
                "(%+.2f %+.2f %+.2f / %+.2f %+.2f %+.2f / %+.2f %+.2f %+.2f), bias (%+.4f, %+.4f, %+.4f)", motion.out.accel[0],
                motion.out.accel[1], motion.out.accel[2], motion.out.gyro[0], motion.out.gyro[1], motion.out.gyro[2],
                motion.out.direction[0], motion.out.direction[1], motion.out.direction[2], motion.out.direction[3],
                motion.out.direction[4], motion.out.direction[5], motion.out.direction[6], motion.out.direction[7],
                motion.out.direction[8], motion.bias[0], motion.bias[1], motion.bias[2]);
    }
    HosMotionSample r;
    r.st = motion.out;
    r.n = ++motion_readings;
    /* (the time since the reading before, capped as the filter caps it: a stalled pad's next reading is no jump) */
    r.dt_ns = fresh ? 5000000 : now - motion_reading_ns;
    if (r.dt_ns > (int64_t)(HOS_MOTION_DT_MAX * 1e9)) r.dt_ns = (int64_t)(HOS_MOTION_DT_MAX * 1e9);
    motion_reading_ns = now;
    AcquireSRWLockExclusive(&state_lock);
    motion_head = (motion_head + 1) % HOS_MOTION_HISTORY;
    motion_hist[motion_head] = r;
    if (fresh) motion_count = 0;
    if (motion_count < HOS_MOTION_HISTORY) motion_count++;
    motion_live = 1;
    ReleaseSRWLockExclusive(&state_lock);
}

/* one poll: controllers plugged in or gone (each second), then every open one's buttons, sticks and rumble */
static void pads_poll(void) {
    static int64_t next_scan = 0;
    hos_sdl_pads_update();
        const int64_t now = hos_now_ns();
        if (now >= next_scan) {   /* new controllers (and ones that went away) */
            next_scan = now + 1000000000LL;
            for (int i = 0; i < MAX_PADS; i++)
                if (pads[i].gc && !hos_sdl_pad_connected(pads[i].gc)) {
                    hos_log("hid: controller \"%s\" disconnected (SDL)", hos_sdl_pad_name(pads[i].gc));
                    hos_sdl_pad_close(pads[i].gc);
                    pads[i].gc = NULL;
                    pads[i].rumble_lo = pads[i].rumble_hi = 0;
                    pads[i].rumble_ns = 0;
                    pads[i].motion = pads[i].motion_on = 0;
                }
            int32_t ids[32];
            int opens[32];
            const int nj = hos_sdl_pads_list(ids, opens, 32);
            for (int j = 0; j < nj; j++) {
                const int32_t id = ids[j];
                int have = 0, slot = -1;
                for (int i = 0; i < MAX_PADS; i++) {
                    if (pads[i].gc && pads[i].id == id) have = 1;
                    if (!pads[i].gc && slot < 0) slot = i;
                }
                if (have || slot < 0) continue;
                void *gc = hos_sdl_pad_open(opens[j]);
                if (!gc) continue;
                pads[slot].gc = gc;
                pads[slot].id = hos_sdl_pad_id(gc);
                pads[slot].rumble_lo = pads[slot].rumble_hi = 0;
                pads[slot].rumble_ns = 0;
                hd_open(slot, gc);
                pads[slot].ds5 = -1;   /* (local: ds5_extras_poll looks at its name) */
                pads[slot].motion = hos_sdl_pad_has_sensor(gc, HOS_SDL_SENSOR_GYRO) && hos_sdl_pad_has_sensor(gc, HOS_SDL_SENSOR_ACCEL);
                pads[slot].motion_on = 0;
                pads[slot].motion_retry_ns = 0;
                pads[slot].used_ns = now, pads[slot].input_sig = 0;   /* (just connected: the one about to be used) */
                pads[slot].rested = 0;
                memset(pads[slot].held, 0, sizeof pads[slot].held);
                pads[slot].serial = ++pads_opened;
                pads[slot].has_bias = 0;
                hos_log("hid: controller \"%s\" connected (SDL)%s", hos_sdl_pad_name(gc),
                        pads[slot].motion ? ", with a gyroscope" : "");
            }
        }
        uint64_t b = 0;
        int32_t s[4] = {0};
        int count = 0;
        int rx_option(int id, int otherwise);
        const int strength = rx_option(9, 100);   /* (the menu's Rumble strength: the motors' and HD rumble's) */
        uint16_t motors[2];
        rumble_strength((uint16_t)rumble_low, (uint16_t)rumble_high, strength, motors);
        const uint16_t lo = motors[0], hi = motors[1];
        uint8_t side[3][4];   /* (the HD rumble pads': each side's bytes, then both merged for a single Joy-Con) */
        {
            float hv[2][4], felt[3][4];
            AcquireSRWLockShared(&hd_lock);
            memcpy(hv, hd_values, sizeof hv);
            ReleaseSRWLockShared(&hd_lock);
            hd_rumble_felt(hv[0], felt[0]);   /* (the game's small amplitudes, as the motors get them) */
            hd_rumble_felt(hv[1], felt[1]);
            hd_rumble_encode(felt[0], strength, side[0]);
            hd_rumble_encode(felt[1], strength, side[1]);
            hd_rumble_merge(felt[0], felt[1], felt[2]);
            hd_rumble_encode(felt[2], strength, side[2]);
        }
        for (int i = 0; i < MAX_PADS; i++) {
            void *gc = pads[i].gc;
            if (!gc) continue;
            count++;
            for (int k = BTN_A; k <= BTN_RIGHT; k++)   /* (SDL3's gamepad buttons are in SDL2's order) */
                if (hos_sdl_pad_button(gc, k)) b |= 1ULL << k;
            /* (hookshot) a DualShock 4's or DualSense's touchpad click: it opens the Insert menu as F1 does (vk_menu.cpp).
               SDL numbers it 20, past the buttons above; an SDL without it answers 0 */
            if (hos_sdl_pad_button(gc, BTN_TOUCHPAD)) b |= 1ULL << HOS_PAD_TOUCHPAD;
            if (hos_sdl_pad_axis(gc, AX_LT) > 8000) b |= 1ULL << HOS_PAD_LTRIGGER;
            {   /* (local) while the DualSense's trigger has its bowstring (ds5_extras_poll), R2 counts as pressed only past the
                   point where it gives (zone 8 of 10, ~80%), and lets go under ~60%: a half pull holds the shot */
                static int held[MAX_PADS];
                const int rt = hos_sdl_pad_axis(gc, AX_RT);
                const int bow = pads[i].ds5 > 0 && pads[i].ds5_sent != 0xffffffffu && (pads[i].ds5_sent >> 8 & 1);
                held[i] = bow ? (rt > 26000 || (held[i] && rt > 19000)) : rt > 8000;
                if (held[i]) b |= 1ULL << HOS_PAD_RTRIGGER;
            }
            const int inv_x = rx_option(16, 0);
            const int inv_y = rx_option(17, 0);
            const int z = rx_option(11, 0);   /* (the menu's Stick dead zone: nn_hid.c's hos_stick_axis) */
            int32_t v[4] = {stick_value(hos_sdl_pad_axis(gc, AX_LX), 0, z),
                            stick_value(hos_sdl_pad_axis(gc, AX_LY), 1, z),
                            stick_value(hos_sdl_pad_axis(gc, AX_RX), inv_x, z),
                            stick_value(hos_sdl_pad_axis(gc, AX_RY), inv_y ? 0 : 1, z)};
            /* (hookshot) An axis steers once it has been seen at rest (inside the dead zone) since the pad was opened. The sticks are
               the strongest of all the pads', so a device whose axis reads held for good (a mapping that puts a trigger or an
               unused axis on a stick, a second virtual pad) turned the camera or walked Link in circles on its own, with the
               player's pad at rest (#1673: Xbox 360 pad, CachyOS; #1661: GameSir 2 Lite, wired). A real stick rests at the start. */
            /* (a worn stick resting just past a small dead zone counts once it is pushed the other way: it went past its rest) */
            for (int k = 0; k < 4; k++) {
                const signed char way = (signed char)(v[k] > 0 ? 1 : v[k] < 0 ? -1 : 0);
                if (!way || (pads[i].held[k] && way != pads[i].held[k])) pads[i].rested |= 1u << k;
                pads[i].held[k] = way;
                if (!(pads[i].rested >> k & 1)) v[k] = 0;
            }
            for (int k = 0; k < 4; k++)
                if ((v[k] < 0 ? -v[k] : v[k]) > (s[k] < 0 ? -s[k] : s[k])) s[k] = v[k];
            {   /* (motion: the pad last used; a stick counts by which way it is pushed, not by how far) */
                uint64_t sig = 0;
                for (int k = BTN_A; k <= BTN_RIGHT; k++)
                    if (hos_sdl_pad_button(gc, k)) sig |= 1ULL << k;
                for (int k = 0; k < 4; k++) sig |= (uint64_t)(v[k] > 0 ? 1 : v[k] < 0 ? 2 : 0) << (32 + 2 * k);
                if (sig != pads[i].input_sig) pads[i].input_sig = sig, pads[i].used_ns = now;
            }
            if (pads_leaving) continue;   /* (local: hos_pads_restore has the controllers) */
            light_bar_poll(i, gc, now);
            hit_rumble_poll(i, gc, now);
            ds5_extras_poll(i, gc, now);
            if (now < pads[i].hit_until) {   /* (local) a hit's rumble playing: the game's waits, then is sent again */
                pads[i].rumble_lo = pads[i].rumble_hi = 0xFFFF;
                continue;
            }
            if (hd_poll(i, side, now)) continue;
            if (rumble_due(lo, hi, pads[i].rumble_lo, pads[i].rumble_hi, now, pads[i].rumble_ns)) {
                hos_sdl_pad_rumble(gc, lo, hi, lo || hi ? 1000 : 0);
                pads[i].rumble_lo = lo;
                pads[i].rumble_hi = hi;
                pads[i].rumble_ns = now;
            }
        }
        motion_poll(now, count);
        AcquireSRWLockExclusive(&state_lock);
        state_buttons = b;
        memcpy(state_stick, s, sizeof s);
        state_count = count;
        state_name[0] = 0;
        for (int i = 0; i < MAX_PADS; i++)
            if (pads[i].gc) {
                const char *name = hos_sdl_pad_name(pads[i].gc);
                snprintf(state_name, sizeof state_name, "%s", name ? name : "");
                break;
            }
        ReleaseSRWLockExclusive(&state_lock);
}

static DWORD WINAPI pad_main(void *arg) {
    (void)arg;
    hos_host_thread_register_self("gamepads (SDL)");
    if (!pads_start()) return 0;
    for (;;) {
        pads_poll();
        Sleep(4);
    }
}
#ifndef _WIN32
int hos_display_owns_sdl(void);   /* host/posix/display_sdl.c */
/* (linux) SDL also runs the window (host/posix/display_sdl.c), and its event queue is not for two threads. Polled
   from the window's thread, between its events. Two threads pumping it corrupted the heap once a controller was
   attached. */
static volatile LONG pads_loaded;
void hos_sdl_pads_pump(void) {
    static int state;   /* 0 not started, 1 polling, -1 not available */
    if (!state && pads_loaded) state = pads_start() ? 1 : -1;
    if (state > 0) pads_poll();
}
#endif

/* The controllers' merged state (nn_hid.c): ORs their inputs in (1 << HOS_PAD_*), keeps the larger stick
   deflections; returns how many controllers SDL has open. Starts the SDL thread on the first call. */
int hos_sdl_pads(uint64_t *buttons, int32_t stick[4]) {
    static volatile LONG started;
    if (!started && !InterlockedExchange(&started, 1)) {
        if (getenv("HOS_NO_SDL")) return 0;
        if (!hos_sdl_load()) {
            hos_log("hid: no SDL beside the exe or on the system: only XInput controllers (Xbox pads) work");
            return 0;
        }
#ifdef _WIN32
        CreateThread(NULL, 0, pad_main, NULL, 0, NULL);
#else
        if (hos_display_owns_sdl())
            InterlockedExchange(&pads_loaded, 1);   /* (the window's thread polls: hos_sdl_pads_pump) */
        else
            CreateThread(NULL, 0, pad_main, NULL, 0, NULL);   /* (no window: nothing else pumps SDL) */
#endif
    }
    AcquireSRWLockShared(&state_lock);
    const int n = state_count;
    *buttons |= state_buttons;
    for (int k = 0; k < 4; k++)
        if ((state_stick[k] < 0 ? -state_stick[k] : state_stick[k]) > (stick[k] < 0 ? -stick[k] : stick[k]))
            stick[k] = state_stick[k];
    ReleaseSRWLockShared(&state_lock);
    return n;
}

/* (motion) the latest readings for nn_hid.c's GetSixAxisSensorStates, newest first: up to max, how many there are; 0
   when there are none (Motion controls off, or no pad with a gyroscope) */
int hos_sdl_motion_history(HosMotionSample *out, int max) {
    AcquireSRWLockShared(&state_lock);
    const int n = !motion_live ? 0 : motion_count < max ? motion_count : max;
    for (int i = 0; i < n; i++) out[i] = motion_hist[(motion_head - i + HOS_MOTION_HISTORY) % HOS_MOTION_HISTORY];
    ReleaseSRWLockShared(&state_lock);
    return n;
}
/* (the Insert menu's Motion controls row) 0 no controller, 1 controllers with no gyroscope, 2 one with a gyroscope */
int hos_sdl_gyro_status(void) { return (int)gyro_status; }
/* The first open controller's name into out ("" with none) */
void hos_sdl_pad_first_name(char *out, size_t cap) {
    AcquireSRWLockShared(&state_lock);
    snprintf(out, cap, "%s", state_name);
    ReleaseSRWLockShared(&state_lock);
}

/* Vibration from the game (0..1 amplitudes of the low and high bands), for the pads that rumble. */
void hos_sdl_rumble(float low, float high) {
    if (low < 0) low = 0;
    if (low > 1) low = 1;
    if (high < 0) high = 0;
    if (high > 1) high = 1;
    InterlockedExchange(&rumble_low, (LONG)(low * 65535));
    InterlockedExchange(&rumble_high, (LONG)(high * 65535));
}

/* The game's own vibration value for one side (0 left, 1 right; {amp_low, freq_low, amp_high, freq_high}), for the
   pads that play HD rumble (hd_rumble.h); nn_hid.c's vibration_send, beside hos_sdl_rumble. */
void hos_sdl_rumble_hd(int side, const float value[4]) {
    AcquireSRWLockExclusive(&hd_lock);
    memcpy(hd_values[side & 1], value, sizeof hd_values[0]);
    ReleaseSRWLockExclusive(&hd_lock);
}
