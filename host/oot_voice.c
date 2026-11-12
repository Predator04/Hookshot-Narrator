/* (local) Read-aloud: the lines characters say in their dialogue box, spoken with Windows' own voice (SAPI), for a player
   who cannot read yet.

   The message on show: found on 2026-11-11 with the Master Craftsman's box up, the guest pointer at 0x84e990b8 (main's
   data) holds an object whose +0x58 is the message's text (UTF-8) and +0x60 its length in bytes. The text carries tags,
   0x0E u16 group, u16 tag, u16 size, then size bytes of parameters (the first ones are the box's mood, "Greeting" and so
   on), which are left out, as are newlines. A thread looks every 100 ms; a new message is spoken after 0.4 s, unless the
   game opened a voiced line's stream (host/nn_fs.c, hos_voiced_line_ns) around it: then its actor speaks instead.

   HOS_VOICE=0: off. HOS_VOICE_RATE=-10..10 (default -1, a little slow). */
#include "hos.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

volatile long long hos_voiced_line_ns;   /* (host/nn_fs.c: when the game last opened a voiced line's stream) */

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <sapi.h>
#include <mmsystem.h>
#include <stdio.h>

static const GUID my_CLSID_SpVoice = {0x96749377, 0x3391, 0x11d2, {0x9e, 0xe3, 0x00, 0xc0, 0x4f, 0x79, 0x73, 0x96}};
static const GUID my_IID_ISpVoice = {0x6c44df74, 0x72b9, 0x4992, {0xa1, 0xec, 0xef, 0x99, 0x6e, 0x04, 0x22, 0xd4}};

enum { MAXT = 2048 };
#define MSG_HOLDER 0x84e990b8ULL
#define MSG_TEXT 0x58
#define MSG_LEN 0x60
#define MSG_PAGE 0x48

static int in_heap(uint64_t p) {   /* (readable guest memory, asked of Windows: the text may live outside the heap) */
    if (p < 0x10000 || p >= 0x10000000000ULL) return 0;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(A64_PTR(p), &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const char *)mi.BaseAddress + mi.RegionSize - (const char *)A64_PTR(p) >= 16;
}
static uint64_t g64(uint64_t a) { uint64_t v; memcpy(&v, A64_PTR(a), 8); return v; }
static uint32_t g32(uint64_t a) { uint32_t v; memcpy(&v, A64_PTR(a), 4); return v; }

/* the message as words: tags out, newlines to spaces; 0 when it has no letters */
static int message_words(uint64_t text, uint32_t len, wchar_t *out, int max) {
    unsigned char b[MAXT];
    if (len >= sizeof b) len = sizeof b - 1;
    memcpy(b, A64_PTR(text), len);
    b[len] = 0;
    char plain[MAXT];
    int n = 0, letters = 0;
    for (uint32_t i = 0; i < len && b[i]; i++) {
        if (b[i] == 0x0e && i + 6 < len) {   /* tag: group, tag, size, parameters */
            const unsigned size = b[i + 5] | b[i + 6] << 8;
            i += 6 + size;
            continue;
        }
        if (b[i] == 0x0f && i + 4 < len) { i += 4; continue; }   /* (a closing tag: group, tag) */
        unsigned char ch = b[i] == '\n' || b[i] == '\r' ? ' ' : b[i];
        if (ch < 0x20) continue;
        if (ch == ' ' && n > 0 && plain[n - 1] == ' ') continue;
        if ((ch | 0x20) >= 'a' && (ch | 0x20) <= 'z') letters++;
        if (n < (int)sizeof plain - 1) plain[n++] = (char)ch;
    }
    plain[n] = 0;
    if (letters < 2) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, plain, -1, out, max) > 0;
}

int rx_option(int id, int otherwise);
enum { OPT_VOICE_ON = 70, OPT_VOICE_KIND, OPT_VOICE_SPEED, OPT_VOICE_GLOW };   /* (gpu/vk/vk_api.h HOS_OPT_VOICE_*) */

/* Piper (piper\ beside the exe: piper.exe and en_US-lessac-medium.onnx), a natural voice run on this PC: one process kept
   running, a line of text in on its stdin, the path of the .wav it made out on its stdout; played with PlaySound */
typedef struct { HANDLE in, out, proc; int speed; const wchar_t *model; } Piper;
static Piper piper_main = {0, 0, 0, 0, L"en_US-lessac-medium.onnx"}, piper_cast = {0, 0, 0, 0, L"en_US-libritts_r-medium.onnx"};
static int piper_failed;
static wchar_t piper_dir[MAX_PATH];
static void piper_stop_process(Piper *pp) {
    if (pp->proc) TerminateProcess(pp->proc, 0), CloseHandle(pp->proc);
    if (pp->in) CloseHandle(pp->in);
    if (pp->out) CloseHandle(pp->out);
    pp->proc = pp->in = pp->out = NULL;
}
static int piper_start(Piper *pp, int speed) {
    if (pp->proc && speed == pp->speed) return 1;
    piper_stop_process(pp);
    if (piper_failed) return 0;
    DWORD n = GetModuleFileNameW(NULL, piper_dir, MAX_PATH);
    while (n && piper_dir[n - 1] != L'\\') n--;
    piper_dir[n] = 0;
    wcscat(piper_dir, L"piper");
    wchar_t exe[MAX_PATH], cmd[1024], out[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\piper.exe", piper_dir);
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        piper_failed = 1;
        hos_log("voice: no piper\\piper.exe beside the game: the Windows voice instead");
        return 0;
    }
    swprintf(out, MAX_PATH, L"%ls\\%ls", piper_dir, pp->model);
    if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) {
        hos_log("voice: no piper\\%ls", pp->model);
        return 0;
    }
    swprintf(out, MAX_PATH, L"%ls\\out", piper_dir);
    CreateDirectoryW(out, NULL);
    swprintf(cmd, 1024, L"\"%ls\" --model %ls --output_dir out --json-input --length_scale %.2f --sentence_silence 0.15 -q",
             exe, pp->model, 100.0 / (speed > 0 ? speed : 100));
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE in_r, in_w, out_r, out_w;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return piper_failed = 1, 0;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return CloseHandle(in_r), CloseHandle(in_w), piper_failed = 1, 0;
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r, si.hStdOutput = out_w;
    si.hStdError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    PROCESS_INFORMATION pi;
    const BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, piper_dir, &si, &pi);
    CloseHandle(in_r), CloseHandle(out_w), CloseHandle(si.hStdError);
    if (!ok) {
        hos_log("voice: piper did not start (error %lu): the Windows voice instead", GetLastError());
        CloseHandle(in_w), CloseHandle(out_r);
        return piper_failed = 1, 0;
    }
    CloseHandle(pi.hThread);
    pp->proc = pi.hProcess, pp->in = in_w, pp->out = out_r, pp->speed = speed;
    hos_log("voice: natural voice (piper, %ls) ready at %d%% speed", pp->model, speed);
    return 1;
}
/* speaks the words; returns how long the clip is in ns, 0 when piper failed */
/* one sentence through piper: its samples (16-bit mono 22050 Hz) added to *pcm; 0 when piper failed */
static int piper_sentence(Piper *pp, const char *u8, int speaker, short **pcm, int *n, int *cap) {
    static char line[MAXT * 6];
    int m = snprintf(line, 32, "{\"text\": \"");
    for (const char *c = u8; *c && m < (int)sizeof line - 48; c++) {   /* (JSON: quotes and backslashes escaped) */
        if (*c == '"' || *c == '\\') line[m++] = '\\';
        line[m++] = *c;
    }
    m += speaker >= 0 ? snprintf(line + m, 48, "\", \"speaker_id\": %d}\n", speaker) : snprintf(line + m, 48, "\"}\n");
    DWORD wrote;
    if (!WriteFile(pp->in, line, (DWORD)m, &wrote, NULL)) { piper_stop_process(pp); return 0; }
    char path[MAX_PATH * 2];
    int k = 0;
    for (;;) {   /* (one line back: the .wav's path) */
        char ch;
        DWORD got;
        if (!ReadFile(pp->out, &ch, 1, &got, NULL) || !got) { hos_log("voice: piper stopped"); piper_stop_process(pp); return 0; }
        if (ch == '\n') break;
        if (ch != '\r' && k < (int)sizeof path - 1) path[k++] = ch;
    }
    path[k] = 0;
    wchar_t wav[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wav, MAX_PATH)) return 0;
    if (wav[0] && wav[1] != L':') {   /* (relative to piper's folder) */
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\%ls", piper_dir, wav);
        wcscpy(wav, full);
    }
    FILE *f = _wfopen(wav, L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 44, SEEK_SET);
    const int got = size > 44 ? (int)((size - 44) / 2) : 0;
    if (*n + got > *cap) {
        *cap = (*n + got) * 2 + 22050;
        short *p2 = realloc(*pcm, (size_t)*cap * sizeof(short));
        if (!p2) return fclose(f), DeleteFileW(wav), 0;
        *pcm = p2;
    }
    const int r = (int)fread(*pcm + *n, 2, (size_t)got, f);
    fclose(f);
    DeleteFileW(wav);
    *n += r;
    return 1;
}

/* the sentence's tone, by how it ends: "!" a little faster, higher and louder; "?" its last part rising; "..." a longer
   pause after. Done on the samples from `from` on: resampled at a rate (above 1 = shorter and higher) */
static void tone(short *pcm, int from, int *n, int excited, int asks) {
    const int len = *n - from;
    if (len <= 0 || (!excited && !asks)) return;
    short *src = malloc((size_t)len * sizeof(short));
    if (!src) return;
    memcpy(src, pcm + from, (size_t)len * sizeof(short));
    const float gain = excited ? 1.3f : 1.0f;
    double pos = 0;
    int o = 0;
    while (pos < len - 1 && o < len) {
        const double f = pos / len;
        double rate = excited ? 1.07 : 1.0;
        if (asks && f > 0.55) rate *= 1.0 + 0.16 * (f - 0.55) / 0.45;   /* (the end rising, as a question) */
        const int i = (int)pos;
        const double t = pos - i;
        float v = (float)((1 - t) * src[i] + t * src[i + 1]) * gain;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        pcm[from + o++] = (short)v;
        pos += rate;
    }
    *n = from + o;
    free(src);
}

/* speaks the words, a sentence at a time; returns how long it is in ns, 0 when piper failed */
static int64_t piper_say(Piper *pp, const wchar_t *words, int speaker) {
    static char u8[MAXT * 3];
    if (WideCharToMultiByte(CP_UTF8, 0, words, -1, u8, sizeof u8, NULL, NULL) <= 1) return 0;
    static short *pcm;
    static int cap;
    int n = 0;
    PlaySoundW(NULL, NULL, 0);   /* (before pcm is written again: PlaySound reads it while it plays) */
    char *c = u8;
    while (*c) {
        while (*c == ' ') c++;
        if (!*c) break;
        char *e = c;   /* (to the end of the sentence: its . ! ? and any more of them, then a space or the end) */
        while (*e && !(strchr(".!?", *e) && (!e[1] || e[1] == ' ' || strchr(".!?\"'", e[1])))) e++;
        while (*e && strchr(".!?\"'", *e)) e++;
        const char keep = *e;
        *e = 0;
        const int excited = strchr(c, '!') != NULL, asks = strchr(c, '?') != NULL, trails = strstr(c, "...") != NULL;
        const int from = n;
        if (!piper_sentence(pp, c, speaker, &pcm, &n, &cap)) return 0;
        tone(pcm, from, &n, excited, asks);
        const int pause = trails ? 22050 * 45 / 100 : 0;   /* (piper ends each sentence with its own short pause) */
        if (pause && n + pause <= cap) memset(pcm + n, 0, (size_t)pause * sizeof(short)), n += pause;
        *e = keep;
        c = e;
    }
    if (!n) return 0;
    static unsigned char *wav;
    static size_t wav_cap;
    const size_t bytes = (size_t)n * 2;
    if (bytes + 44 > wav_cap) {
        unsigned char *w2 = realloc(wav, bytes + 44);
        if (!w2) return 0;
        wav = w2, wav_cap = bytes + 44;
    }
    const uint32_t rate = 22050, riff = (uint32_t)bytes + 36, fmt = 16, br = rate * 2, data = (uint32_t)bytes;
    const uint16_t pcm_fmt = 1, ch = 1, align = 2, bits = 16;
    memcpy(wav, "RIFF", 4), memcpy(wav + 4, &riff, 4), memcpy(wav + 8, "WAVEfmt ", 8), memcpy(wav + 16, &fmt, 4);
    memcpy(wav + 20, &pcm_fmt, 2), memcpy(wav + 22, &ch, 2), memcpy(wav + 24, &rate, 4), memcpy(wav + 28, &br, 4);
    memcpy(wav + 32, &align, 2), memcpy(wav + 34, &bits, 2), memcpy(wav + 36, "data", 4), memcpy(wav + 40, &data, 4);
    memcpy(wav + 44, pcm, bytes);
    if (!PlaySoundW((LPCWSTR)wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
        hos_log("voice: PlaySound could not play the line");
        return 0;
    }
    return (int64_t)n * 1000000000LL / 22050;
}
#include "oot_voice_cast.h"
/* who speaks: the message object's +0x28 is the event's actors, slots 0x60 apart from +0x40, each with its name at +0x20
   ("Npc_Hylia_Soldier_001"; found 2026-11-12 with a soldier's box up); the first Npc_ one */
static int speaker_name(uint64_t obj, char *out, int max) {
    const uint64_t arr = g64(obj + 0x28);
    if (!in_heap(arr)) return 0;
    for (int i = 0; i < 8; i++) {
        const uint64_t slot = g64(arr + 0x40 + 0x60 * i);
        if (!in_heap(slot)) continue;
        const uint64_t nm = g64(slot + 0x20);
        if (!in_heap(nm)) continue;
        int k = 0;
        for (; k < max - 1; k++) {
            const char ch = *(const char *)A64_PTR(nm + k);
            if (!ch) break;
            out[k] = ch;
        }
        out[k] = 0;
        if (!strncmp(out, "Npc_", 4)) return 1;
    }
    return 0;
}
/* the voice for a speaker: m man, f woman, b boy, g girl; 0 unknown (the main voice) */
static char speaker_kind(const char *name) {
    static const struct { const char *part; char kind; } named[] = {
        {"Saria", 'g'}, {"Fado", 'g'}, {"Malon_Child", 'g'}, {"Ruto_Child", 'g'}, {"Zelda_Child", 'g'}, {"Mido", 'b'},
        {"Malon", 'f'}, {"Impa", 'f'}, {"Zelda", 'f'}, {"Ruto", 'f'}, {"Nabooru", 'f'}, {"Sheik", 'f'}, {"GreatFairy", 'f'},
        {"Twinrova", 'f'}, {"Navi", 'f'}, {"Ganondorf", 'm'}, {"Darunia", 'm'}, {"Talon", 'm'}, {"Ingo", 'm'},
        {"Dampe", 'm'}, {"KingZora", 'm'}, {"Kaepora", 'm'}, {"Rauru", 'm'}, {"Carpenter", 'm'}, {"Soldier", 'm'},
        {"SkullKid", 'b'}, {"SkullChild", 'b'}, {"DekuTree", 'b'}, {"Pierre", 'm'}, {"GuidePoe", 'm'}};
    for (size_t i = 0; i < sizeof named / sizeof *named; i++)
        if (strstr(name, named[i].part)) return named[i].kind;
    size_t best = 0;
    char kind = 0;
    for (size_t i = 0; i < sizeof cast / sizeof *cast; i++) {   /* (the longest table name that starts the speaker's) */
        const size_t l = strlen(cast[i].name);
        if (l > best && !strncmp(name, cast[i].name, l)) best = l, kind = cast[i].kind;
    }
    if (kind) return kind;
    if (!strncmp(name, "Npc_Goron", 9) || !strncmp(name, "Npc_Zora", 8)) return 'm';
    if (!strncmp(name, "Npc_Gerudo", 10)) return 'f';
    if (!strncmp(name, "Npc_Kokiri", 10)) return 'b';
    return 0;
}
/* a voice of the many-voiced model for this speaker: the same one each time, from voices measured by pitch */
static int speaker_voice(const char *name, char kind) {
    static const short man[] = {8, 36, 48, 52, 124, 132, 156, 248, 272, 288, 296, 332, 380, 388, 400, 428, 432, 452, 496, 500,
                                512, 520, 528, 544, 552, 564, 568, 596, 636, 660, 680, 684, 700, 704, 720, 724, 736, 740};
    static const short woman[] = {4, 12, 40, 76, 108, 128, 164, 180, 188, 192, 228, 252, 280, 316, 336, 340, 344, 348, 352,
                                  372, 384, 412, 420, 424, 444, 464, 532, 560, 576, 580, 588, 628, 656, 708, 764, 820};
    static const short girl[] = {20, 60, 68, 100, 140, 148, 172, 396, 408, 448, 488, 548, 556, 624, 664, 796, 840};
    static const short boy[] = {0, 56, 64, 80, 96, 112, 120, 144, 160, 184, 196, 220, 224, 236, 240, 244, 260, 268, 276, 292};
    uint32_t h = 2166136261u;
    for (const char *c = name; *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
    switch (kind) {
    case 'm': return man[h % (sizeof man / sizeof *man)];
    case 'f': return woman[h % (sizeof woman / sizeof *woman)];
    case 'g': return girl[h % (sizeof girl / sizeof *girl)];
    default: return boy[(h >> 8) % (sizeof boy / sizeof *boy)];
    }
}

static void hush(ISpVoice *v) {
    ISpVoice_Speak(v, L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL);
    PlaySoundW(NULL, NULL, 0);
}

/* the renderer's "press next" rainbow frame (gpu/vk/vk_menu.cpp, mkwvk_voice_glow) */
static volatile int *glow_flag;
static int64_t glow_since;
static void glow(int on) {
    if (!glow_flag) {
        HMODULE m = GetModuleHandleA("hookshot_vk.dll");
        if (m) glow_flag = (volatile int *)GetProcAddress(m, "mkwvk_voice_glow");
        if (!glow_flag) return;
    }
    if (on && !*glow_flag) glow_since = hos_now_ns();
    *glow_flag = on;
}

static DWORD WINAPI voice_thread(void *arg) {
    (void)arg;
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 0;
    ISpVoice *v = NULL;
    if (FAILED(CoCreateInstance(&my_CLSID_SpVoice, NULL, CLSCTX_ALL, &my_IID_ISpVoice, (void **)&v)) || !v) {
        hos_log("voice: no Windows voice (SAPI) here; read-aloud off");
        return 0;
    }
    const char *r = getenv("HOS_VOICE_RATE");
    ISpVoice_SetRate(v, r && *r ? atoi(r) : -1);
    hos_log("voice: read-aloud ready (Windows voice, the dialogue box's message; HOS_VOICE=0 turns it off)");
    uint64_t last_text = 0;
    uint32_t last_hash = 0;
    int64_t pending_at = 0;
    int speaking = 0;   /* (1 the Windows voice, 2 piper until piper_until) */
    int64_t piper_until = 0;
    wchar_t words[MAXT];
    for (;;) {
        Sleep(100);
        const int64_t now = hos_now_ns();
        const uint64_t obj = g64(MSG_HOLDER);
        static uint64_t seen_obj = 1;
        static int diag;
        if (obj != seen_obj && diag < 40) {
            diag++, seen_obj = obj;
            hos_log("voice: message object %#llx text %#llx len %u", (unsigned long long)obj,
                    in_heap(obj) ? (unsigned long long)g64(obj + MSG_TEXT) : 0ULL, in_heap(obj) ? g32(obj + MSG_LEN) : 0u);
        }
        if (in_heap(obj)) {
            /* (the message's pages are a ring of nodes {prev, next, flags, len, text...}; +0x58 is the first page's
               text, +0x48 the page on show, 1 for the first: walk to it) */
            uint64_t text = g64(obj + MSG_TEXT);
            uint32_t len = g32(obj + MSG_LEN);
            const uint32_t page = g32(obj + MSG_PAGE);
            if (page > 1 && page < 64 && in_heap(text - 0x20)) {
                const uint64_t first = text - 0x20, head = g64(first);
                uint64_t node = first;
                for (uint32_t k = 1; k < page && node; k++) {
                    node = in_heap(node) ? g64(node + 8) : 0;
                    if (node == head || node == first || !in_heap(node)) node = 0;
                }
                if (node) text = node + 0x20, len = g32(node + 0x18);
            }
            if (in_heap(text) && len > 0 && len < 8192) {
                uint32_t h = 2166136261u;
                for (uint32_t i = 0; i < len && i < 4096; i++) h = (h ^ *(const unsigned char *)A64_PTR(text + i)) * 16777619u;
                if (text != last_text || h != last_hash) {
                    if (diag < 40) diag++, hos_log("voice: new text at %#llx, %u bytes", (unsigned long long)text, len);
                    static int first = 1;   /* (the message already there when the thread first looks is not new) */
                    last_text = text, last_hash = h;
                    glow(0);
                    if (speaking) hush(v), speaking = 0;   /* (a new page: the last one's voice stops) */
                    /* (closing the box drops the page back to 1: the same message's first page again is not new) */
                    static uint32_t page1_hash, last_page;
                    const uint32_t pg = page < 64 ? page : 1;
                    const int back_to_start = pg <= 1 && last_page > 1 && h == page1_hash;
                    if (pg <= 1) page1_hash = h;
                    last_page = pg;
                    if (back_to_start) {
                        if (diag < 40) diag++, hos_log("voice: back to page 1 of the same message (box closed), not read again");
                        pending_at = 0;
                    } else if (message_words(text, len, words, MAXT)) pending_at = now;
                    first = 0;
                }
            }
        }
        if (!rx_option(OPT_VOICE_ON, 1)) {   /* (the Insert menu's Read aloud row off) */
            pending_at = 0;
            if (speaking) hush(v), speaking = 0;
            glow(0);
        }
        if (pending_at && now - pending_at > 300000000LL) {
            pending_at = 0;
            static int said;
            if (hos_voiced_line_ns > now - 2400000000LL) {
                if (said < 30) said++, hos_log("voice: skipped, the game voices it: \"%.60ls\"", words);
            } else {
                const int kind = rx_option(OPT_VOICE_KIND, 0), speed = rx_option(OPT_VOICE_SPEED, 100);
                int64_t dur = 0;
                char who[96] = "";
                char sk = 0;
                const uint64_t o2 = g64(MSG_HOLDER);
                if (in_heap(o2) && speaker_name(o2, who, sizeof who)) sk = speaker_kind(who);
                if (kind == 0 && sk && piper_start(&piper_cast, speed)) dur = piper_say(&piper_cast, words, speaker_voice(who, sk));
                if (kind == 0 && dur <= 0 && piper_start(&piper_main, speed)) dur = piper_say(&piper_main, words, -1);
                if (dur > 0) {
                    if (said < 30) said++, hos_log("voice (natural, %s %c): \"%.70ls\"", who[0] ? who : "?", sk ? sk : '-', words);
                    piper_until = hos_now_ns() + dur + 200000000LL, speaking = 2;
                } else {
                    ISpVoice_SetRate(v, (speed - 100) / 10);
                    const HRESULT hr = ISpVoice_Speak(v, words, SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, NULL);
                    if (said < 30) said++, hos_log("voice (Windows, %#lx): \"%.80ls\"", (unsigned long)hr, words);
                    speaking = 1;
                }
            }
        }
        if (speaking && hos_voiced_line_ns > now - 150000000LL) {   /* (the game began a voiced line: ours stops) */
            hush(v);
            speaking = 0;
        }
        if (speaking == 1 && !pending_at) {   /* (the page read to its end: the rainbow says "press next") */
            SPVOICESTATUS st;
            if (SUCCEEDED(ISpVoice_GetStatus(v, &st, NULL)) && st.dwRunningState == SPRS_DONE)
                speaking = 0, glow(rx_option(OPT_VOICE_GLOW, 1));
        }
        if (speaking == 2 && !pending_at && now > piper_until) speaking = 0, glow(rx_option(OPT_VOICE_GLOW, 1));
        if (glow_flag && *glow_flag && now - glow_since > 60000000000LL) glow(0);   /* (a minute: whatever it was, gone) */
    }
}

void hos_oot_voice_start(void) {
    static volatile LONG started;
    if (InterlockedExchange(&started, 1)) return;
    const char *e = getenv("HOS_VOICE");
    if (e && *e && atoi(e) == 0) return;
    CloseHandle(CreateThread(NULL, 0, voice_thread, NULL, 0, NULL));
}
void hos_oot_voice_pane(Ctx *c, uint64_t pane) { (void)c, (void)pane; hos_oot_voice_start(); }
#else
void hos_oot_voice_start(void) {}
void hos_oot_voice_pane(Ctx *c, uint64_t pane) { (void)c, (void)pane; }
#endif
