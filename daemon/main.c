#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <poll.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <strings.h>

/* ── devices ── */
#define DEV_HIDRAW_SYMLINK "/dev/input/by-id/usb-Logitech_Gaming_Mouse_G600_4FD9CE8DE3650017-if01-hidraw"
#define G600_NAME          "Logitech Gaming Mouse G600"
#define G600_VID           0x046d
#define G600_PID           0xc24a

/* ── config ── */
#define CFG_DIR  "/.config/g600d"
#define CFG_FILE "/.config/g600d/g600d.conf"

/* ── limits ── */
#define MAX_PROFILES    3
#define NUM_GKEYS      12
#define NUM_MKEYS       5
#define MAX_MACRO_STEPS 64

/* ── Special action codes ── */
#define SPECIAL_BASE         0x10000
#define SPECIAL_DPI_SHIFT    (SPECIAL_BASE + 0)
#define SPECIAL_PROFILE_1    (SPECIAL_BASE + 1)
#define SPECIAL_PROFILE_2    (SPECIAL_BASE + 2)
#define SPECIAL_PROFILE_3    (SPECIAL_BASE + 3)
#define SPECIAL_PROFILE_4    (SPECIAL_BASE + 4)
#define SPECIAL_PROFILE_NEXT (SPECIAL_BASE + 5)
#define SPECIAL_PROFILE_PREV (SPECIAL_BASE + 6)
#define SPECIAL_DPI_UP       (SPECIAL_BASE + 7)
#define SPECIAL_DPI_DOWN     (SPECIAL_BASE + 8)

/* ── HID ── */
#define G600_REPORT_SIZE 154
#define REPORT_ID_P0     0xF3
#define REPORT_ID_P1     0xF4
#define REPORT_ID_P2     0xF5
#define LED_SOLID   0
#define LED_BREATHE 1
#define LED_CYCLE   2

/* ──────────────────────────────────────────────
   Key name table
   ────────────────────────────────────────────── */
typedef struct { const char *name; int code; } KeyEntry;
static const KeyEntry KEY_TABLE[] = {
    {"F1",KEY_F1},{"F2",KEY_F2},{"F3",KEY_F3},{"F4",KEY_F4},
    {"F5",KEY_F5},{"F6",KEY_F6},{"F7",KEY_F7},{"F8",KEY_F8},
    {"F9",KEY_F9},{"F10",KEY_F10},{"F11",KEY_F11},{"F12",KEY_F12},
    {"F13",KEY_F13},{"F14",KEY_F14},{"F15",KEY_F15},{"F16",KEY_F16},
    {"F17",KEY_F17},{"F18",KEY_F18},{"F19",KEY_F19},{"F20",KEY_F20},
    {"F21",KEY_F21},{"F22",KEY_F22},{"F23",KEY_F23},{"F24",KEY_F24},
    {"LEFTCTRL",KEY_LEFTCTRL},{"RIGHTCTRL",KEY_RIGHTCTRL},
    {"LEFTSHIFT",KEY_LEFTSHIFT},{"RIGHTSHIFT",KEY_RIGHTSHIFT},
    {"LEFTALT",KEY_LEFTALT},{"RIGHTALT",KEY_RIGHTALT},
    {"LEFTMETA",KEY_LEFTMETA},{"RIGHTMETA",KEY_RIGHTMETA},
    {"UP",KEY_UP},{"DOWN",KEY_DOWN},{"LEFT",KEY_LEFT},{"RIGHT",KEY_RIGHT},
    {"HOME",KEY_HOME},{"END",KEY_END},{"PAGEUP",KEY_PAGEUP},{"PAGEDOWN",KEY_PAGEDOWN},
    {"INSERT",KEY_INSERT},{"DELETE",KEY_DELETE},{"BACKSPACE",KEY_BACKSPACE},
    {"TAB",KEY_TAB},{"ENTER",KEY_ENTER},{"ESC",KEY_ESC},{"SPACE",KEY_SPACE},
    {"VOLUMEUP",KEY_VOLUMEUP},{"VOLUMEDOWN",KEY_VOLUMEDOWN},{"MUTE",KEY_MUTE},
    {"PLAYPAUSE",KEY_PLAYPAUSE},{"NEXTSONG",KEY_NEXTSONG},{"PREVIOUSSONG",KEY_PREVIOUSSONG},
    {"STOPCD",KEY_STOPCD},
    {"PRINT",KEY_PRINT},{"SCROLLLOCK",KEY_SCROLLLOCK},{"PAUSE",KEY_PAUSE},
    {"CAPSLOCK",KEY_CAPSLOCK},{"NUMLOCK",KEY_NUMLOCK},
    {"A",KEY_A},{"B",KEY_B},{"C",KEY_C},{"D",KEY_D},{"E",KEY_E},
    {"F",KEY_F},{"G",KEY_G},{"H",KEY_H},{"I",KEY_I},{"J",KEY_J},
    {"K",KEY_K},{"L",KEY_L},{"M",KEY_M},{"N",KEY_N},{"O",KEY_O},
    {"P",KEY_P},{"Q",KEY_Q},{"R",KEY_R},{"S",KEY_S},{"T",KEY_T},
    {"U",KEY_U},{"V",KEY_V},{"W",KEY_W},{"X",KEY_X},{"Y",KEY_Y},{"Z",KEY_Z},
    {"0",KEY_0},{"1",KEY_1},{"2",KEY_2},{"3",KEY_3},{"4",KEY_4},
    {"5",KEY_5},{"6",KEY_6},{"7",KEY_7},{"8",KEY_8},{"9",KEY_9},
    {"BTN_LEFT",BTN_LEFT},{"BTN_RIGHT",BTN_RIGHT},{"BTN_MIDDLE",BTN_MIDDLE},
    {"BTN_SIDE",BTN_SIDE},{"BTN_EXTRA",BTN_EXTRA},
    {"DPI_SHIFT",    SPECIAL_DPI_SHIFT},
    {"PROFILE_1",    SPECIAL_PROFILE_1},
    {"PROFILE_2",    SPECIAL_PROFILE_2},
    {"PROFILE_3",    SPECIAL_PROFILE_3},
    {"PROFILE_NEXT", SPECIAL_PROFILE_NEXT},
    {"PROFILE_PREV", SPECIAL_PROFILE_PREV},
    {"DPI_UP",       SPECIAL_DPI_UP},
    {"DPI_DOWN",     SPECIAL_DPI_DOWN},
    {"NONE", 0},
    {NULL, -1}
};

static int keyname_to_code(const char *name) {
    char upper[64]; size_t i;
    for (i = 0; i < sizeof(upper)-1 && name[i]; i++)
        upper[i] = toupper((unsigned char)name[i]);
    upper[i] = '\0';
    for (const KeyEntry *e = KEY_TABLE; e->name; e++)
        if (strcmp(e->name, upper) == 0) return e->code;
    return -1;
}

/* ──────────────────────────────────────────────
   Macro structures
   ────────────────────────────────────────────── */
typedef enum { STEP_KEYS, STEP_DELAY } StepType;
typedef enum { TRIG_PRESS, TRIG_RELEASE } MacroTrigger;
typedef enum { HOLD_ONCE, HOLD_REPEAT, HOLD_TOGGLE } MacroHold;

#define MAX_COMBO_KEYS 4
typedef struct {
    StepType type;
    int      keys[MAX_COMBO_KEYS];
    int      nkeys;
    int      delay_ms;
} MacroStep;

typedef struct {
    int          is_macro;
    int          keycode;
    MacroTrigger trigger;
    MacroHold    hold;
    MacroStep    steps[MAX_MACRO_STEPS];
    int          nsteps;
} Binding;

#define HID_MOD_LCTRL  0x01
#define HID_MOD_LSHIFT 0x02
#define HID_MOD_LALT   0x04
#define HID_MOD_LMETA  0x08
#define HID_MOD_RCTRL  0x10
#define HID_MOD_RSHIFT 0x20
#define HID_MOD_RALT   0x40
#define HID_MOD_RMETA  0x80

typedef struct { int linux_code; uint8_t hid_code; uint8_t hid_mod; } HIDEntry;
static const HIDEntry HID_TABLE[] = {
    {KEY_A,0x04,0},{KEY_B,0x05,0},{KEY_C,0x06,0},{KEY_D,0x07,0},
    {KEY_E,0x08,0},{KEY_F,0x09,0},{KEY_G,0x0a,0},{KEY_H,0x0b,0},
    {KEY_I,0x0c,0},{KEY_J,0x0d,0},{KEY_K,0x0e,0},{KEY_L,0x0f,0},
    {KEY_M,0x10,0},{KEY_N,0x11,0},{KEY_O,0x12,0},{KEY_P,0x13,0},
    {KEY_Q,0x14,0},{KEY_R,0x15,0},{KEY_S,0x16,0},{KEY_T,0x17,0},
    {KEY_U,0x18,0},{KEY_V,0x19,0},{KEY_W,0x1a,0},{KEY_X,0x1b,0},
    {KEY_Y,0x1c,0},{KEY_Z,0x1d,0},
    {KEY_1,0x1e,0},{KEY_2,0x1f,0},{KEY_3,0x20,0},{KEY_4,0x21,0},
    {KEY_5,0x22,0},{KEY_6,0x23,0},{KEY_7,0x24,0},{KEY_8,0x25,0},
    {KEY_9,0x26,0},{KEY_0,0x27,0},
    {KEY_ENTER,0x28,0},{KEY_ESC,0x29,0},{KEY_BACKSPACE,0x2a,0},
    {KEY_TAB,0x2b,0},{KEY_SPACE,0x2c,0},{KEY_MINUS,0x2d,0},
    {KEY_EQUAL,0x2e,0},{KEY_LEFTBRACE,0x2f,0},{KEY_RIGHTBRACE,0x30,0},
    {KEY_BACKSLASH,0x31,0},{KEY_SEMICOLON,0x33,0},{KEY_APOSTROPHE,0x34,0},
    {KEY_GRAVE,0x35,0},{KEY_COMMA,0x36,0},{KEY_DOT,0x37,0},{KEY_SLASH,0x38,0},
    {KEY_CAPSLOCK,0x39,0},
    {KEY_F1,0x3a,0},{KEY_F2,0x3b,0},{KEY_F3,0x3c,0},{KEY_F4,0x3d,0},
    {KEY_F5,0x3e,0},{KEY_F6,0x3f,0},{KEY_F7,0x40,0},{KEY_F8,0x41,0},
    {KEY_F9,0x42,0},{KEY_F10,0x43,0},{KEY_F11,0x44,0},{KEY_F12,0x45,0},
    {KEY_PRINT,0x46,0},{KEY_SCROLLLOCK,0x47,0},{KEY_PAUSE,0x48,0},
    {KEY_INSERT,0x49,0},{KEY_HOME,0x4a,0},{KEY_PAGEUP,0x4b,0},
    {KEY_DELETE,0x4c,0},{KEY_END,0x4d,0},{KEY_PAGEDOWN,0x4e,0},
    {KEY_RIGHT,0x4f,0},{KEY_LEFT,0x50,0},{KEY_DOWN,0x51,0},{KEY_UP,0x52,0},
    {KEY_F13,0x68,0},{KEY_F14,0x69,0},{KEY_F15,0x6a,0},{KEY_F16,0x6b,0},
    {KEY_F17,0x6c,0},{KEY_F18,0x6d,0},{KEY_F19,0x6e,0},{KEY_F20,0x6f,0},
    {KEY_F21,0x70,0},{KEY_F22,0x71,0},{KEY_F23,0x72,0},{KEY_F24,0x73,0},
    {KEY_VOLUMEUP,0xe9,0},{KEY_VOLUMEDOWN,0xea,0},{KEY_MUTE,0xe2,0},
    {KEY_PLAYPAUSE,0xcd,0},{KEY_NEXTSONG,0xb5,0},{KEY_PREVIOUSSONG,0xb6,0},
    {KEY_STOPCD,0xb7,0},
    {KEY_LEFTCTRL,  0x00, HID_MOD_LCTRL},
    {KEY_LEFTSHIFT, 0x00, HID_MOD_LSHIFT},
    {KEY_LEFTALT,   0x00, HID_MOD_LALT},
    {KEY_LEFTMETA,  0x00, HID_MOD_LMETA},
    {KEY_RIGHTCTRL, 0x00, HID_MOD_RCTRL},
    {KEY_RIGHTSHIFT,0x00, HID_MOD_RSHIFT},
    {KEY_RIGHTALT,  0x00, HID_MOD_RALT},
    {KEY_RIGHTMETA, 0x00, HID_MOD_RMETA},
    {0, 0, 0}
};

static int linux_to_hid(int linux_code, uint8_t *hid_key, uint8_t *hid_mod) {
    for (const HIDEntry *e = HID_TABLE; e->linux_code; e++) {
        if (e->linux_code == linux_code) {
            *hid_key = e->hid_code;
            *hid_mod = e->hid_mod;
            return 0;
        }
    }
    return -1;
}

/* ──────────────────────────────────────────────
   Profile structures
   ────────────────────────────────────────────── */
static const int SRC_GKEYS[NUM_GKEYS] = {
    KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,
    KEY_7,KEY_8,KEY_9,KEY_0,KEY_MINUS,KEY_EQUAL,
};
static const int SRC_GKEYS_SHIFT[NUM_GKEYS] = {
    KEY_F1,KEY_F2,KEY_F3,KEY_F4,KEY_F5,KEY_F6,
    KEY_F7,KEY_F8,KEY_F9,KEY_F10,KEY_F11,KEY_F12,
};
static const char *GKEY_NAMES[NUM_GKEYS] = {
    "G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","G19","G20"
};

static const char *MKEY_NAMES[NUM_MKEYS] = { "BTN_LEFT","BTN_RIGHT","MIDDLE","TILT_LEFT","TILT_RIGHT" };

typedef struct {
    char     name[64];
    int      abs_misc_val;
    Binding  gkeys_normal[NUM_GKEYS];
    Binding  gkeys_gshift[NUM_GKEYS];
    Binding  mkeys[NUM_MKEYS];
    uint8_t  led_r, led_g, led_b;
    uint8_t  led_effect, led_duration;
    int      led_enabled;
    uint16_t dpi[4];
    uint16_t dpi_shift;
    uint8_t  dpi_default;
    int      dpi_enabled;
    Binding  onboard_ring;
    Binding  onboard_g7;
    Binding  onboard_g8;
    int      onboard_enabled;
} Profile;

typedef struct {
    Profile profiles[MAX_PROFILES];
    int     num_profiles;
} Config;

/* ──────────────────────────────────────────────
   Globals
   ────────────────────────────────────────────── */
static volatile int running = 1;
static Config       cfg;
static int          cur_profile = 0;
static int          cur_dpi_slot = 0;
static int          gshift_active = 0;

static int          hidraw_fd = -1;
static int          uinput_fd = -1;
static pthread_mutex_t uinput_lock = PTHREAD_MUTEX_INITIALIZER;

static void sig_handler(int s) { (void)s; running = 0; }
static Profile *active_profile(void) { return &cfg.profiles[cur_profile]; }

/* ──────────────────────────────────────────────
   Binding helpers
   ────────────────────────────────────────────── */
static void binding_set_key(Binding *b, int code) {
    b->is_macro = 0;
    b->keycode  = code;
}

static void profile_defaults(Profile *p) {
    for (int i = 0; i < NUM_GKEYS; i++) {
        binding_set_key(&p->gkeys_normal[i], KEY_F13 + i);
        binding_set_key(&p->gkeys_gshift[i], KEY_F1  + i);
    }
    binding_set_key(&p->mkeys[0], BTN_LEFT);
    binding_set_key(&p->mkeys[1], BTN_RIGHT);
    binding_set_key(&p->mkeys[2], BTN_MIDDLE);
    binding_set_key(&p->mkeys[3], BTN_SIDE);
    binding_set_key(&p->mkeys[4], BTN_EXTRA);
    p->abs_misc_val = -1;
    p->led_r = 255; p->led_g = 255; p->led_b = 255;
    p->led_effect = LED_SOLID; p->led_duration = 4;
    p->led_enabled = 0;
    p->dpi[0] = 1200; p->dpi[1] = 0; p->dpi[2] = 0; p->dpi[3] = 0;
    p->dpi_shift = 0; p->dpi_default = 1; p->dpi_enabled = 0;
}

static void config_defaults(Config *cfg) {
    cfg->num_profiles = 1;
    strncpy(cfg->profiles[0].name, "default", 64);
    profile_defaults(&cfg->profiles[0]);
}

static int binding_to_g600btn(const Binding *b, uint8_t *code, uint8_t *mod, uint8_t *key) {
    if (b->is_macro) return -1;
    int kc = b->keycode;
    if (kc == SPECIAL_DPI_SHIFT)    { *code=0x17; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_DPI_UP)       { *code=0x11; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_DPI_DOWN)     { *code=0x12; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_PROFILE_NEXT) { *code=0x14; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_PROFILE_PREV) { *code=0x14; *mod=0; *key=0; return 0; }
    if (kc == 0) { *code=0; *mod=0; *key=0; return 0; }
    if (kc == BTN_LEFT)   { *code=0x01; *mod=0; *key=0; return 0; }
    if (kc == BTN_RIGHT)  { *code=0x02; *mod=0; *key=0; return 0; }
    if (kc == BTN_MIDDLE) { *code=0x03; *mod=0; *key=0; return 0; }
    if (kc == BTN_SIDE)   { *code=0x04; *mod=0; *key=0; return 0; }
    if (kc == BTN_EXTRA)  { *code=0x05; *mod=0; *key=0; return 0; }
    uint8_t hk=0, hm=0;
    if (linux_to_hid(kc, &hk, &hm) == 0) { *code=0; *mod=hm; *key=hk; return 0; }
    return -1;
}

/* ──────────────────────────────────────────────
   Macro parser
   ────────────────────────────────────────────── */
static int parse_combo(const char *s, int keys[MAX_COMBO_KEYS]) {
    int n = 0;
    char buf[256]; strncpy(buf, s, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    char *tok = strtok(buf, "+");
    while (tok && n < MAX_COMBO_KEYS) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && *end == ' ') *end-- = '\0';
        int code = keyname_to_code(tok);
        if (code <= 0) { fprintf(stderr, "Unknown key in combo: %s\n", tok); return -1; }
        keys[n++] = code;
        tok = strtok(NULL, "+");
    }
    return n;
}

static int parse_macro_binding(Binding *b, const char *val) {
    char buf[1024]; strncpy(buf, val, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    char *p = buf;
    if (strncasecmp(p, "press:", 6) == 0)        { b->trigger = TRIG_PRESS;   p += 6; }
    else if (strncasecmp(p, "release:", 8) == 0) { b->trigger = TRIG_RELEASE; p += 8; }
    else { fprintf(stderr, "Macro: expected press:|release:\n"); return -1; }
    if (strncasecmp(p, "once:", 5) == 0)         { b->hold = HOLD_ONCE;   p += 5; }
    else if (strncasecmp(p, "repeat:", 7) == 0)  { b->hold = HOLD_REPEAT; p += 7; }
    else if (strncasecmp(p, "toggle:", 7) == 0)  { b->hold = HOLD_TOGGLE; p += 7; }
    else { fprintf(stderr, "Macro: expected once:|repeat:|toggle:\n"); return -1; }
    while (*p == ' ') p++;
    b->nsteps = 0;
    char *saveptr;
    char *tok = strtok_r(p, ",", &saveptr);
    while (tok && b->nsteps < MAX_MACRO_STEPS) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
        MacroStep *step = &b->steps[b->nsteps];
        char *ms = strstr(tok, "ms"); if (!ms) ms = strstr(tok, "MS");
        if (ms) {
            step->type     = STEP_DELAY;
            step->delay_ms = atoi(tok);
        } else {
            step->type = STEP_KEYS;
            int n = parse_combo(tok, step->keys);
            if (n <= 0) return -1;
            step->nkeys = n;
        }
        b->nsteps++;
        tok = strtok_r(NULL, ",", &saveptr);
    }
    b->is_macro = 1;
    return 0;
}

static int parse_binding(Binding *b, const char *val) {
    while (*val == ' ') val++;
    if (strncasecmp(val, "macro:", 6) == 0)
        return parse_macro_binding(b, val + 6);
    int code = keyname_to_code(val);
    if (code < 0) { fprintf(stderr, "Unknown key: %s\n", val); return -1; }
    binding_set_key(b, code);
    return 0;
}

/* ──────────────────────────────────────────────
   Config parser
   ────────────────────────────────────────────── */
static void trim(char *s) {
    char *p = s + strlen(s) - 1;
    while (p >= s && isspace((unsigned char)*p)) *p-- = '\0';
    char *q = s;
    while (*q && isspace((unsigned char)*q)) q++;
    if (q != s) memmove(s, q, strlen(q)+1);
}

static int parse_led_effect(const char *val) {
    char upper[32]; size_t i;
    for (i = 0; i < sizeof(upper)-1 && val[i]; i++) upper[i] = toupper((unsigned char)val[i]);
    upper[i] = '\0';
    if (strcmp(upper, "SOLID")   == 0) return LED_SOLID;
    if (strcmp(upper, "BREATHE") == 0) return LED_BREATHE;
    if (strcmp(upper, "CYCLE")   == 0) return LED_CYCLE;
    return -1;
}

static void config_load(Config *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Config not found, using defaults\n"); config_defaults(cfg); return; }
    cfg->num_profiles = 0;
    char line[1024];
    Profile *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#'); if (hash) *hash = '\0';
        trim(line); if (!line[0]) continue;
        if (line[0] == '[') {
            char *end = strchr(line, ']'); if (!end) continue; *end = '\0';
            if (cfg->num_profiles >= MAX_PROFILES) continue;
            cur = &cfg->profiles[cfg->num_profiles++];
            memset(cur, 0, sizeof(*cur));
            strncpy(cur->name, line+1, sizeof(cur->name)-1);
            profile_defaults(cur);
            continue;
        }
        if (!cur) continue;
        char *eq = strchr(line, '='); if (!eq) continue; *eq = '\0';
        char key[128], val[1024];
        strncpy(key, line, sizeof(key)-1); trim(key);
        strncpy(val, eq+1, sizeof(val)-1); trim(val);
        if (strcasecmp(key, "abs_misc") == 0)     { cur->abs_misc_val = atoi(val); goto next; }
        if (strcasecmp(key, "led_r") == 0)         { cur->led_r = (uint8_t)atoi(val); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "led_g") == 0)         { cur->led_g = (uint8_t)atoi(val); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "led_b") == 0)         { cur->led_b = (uint8_t)atoi(val); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "led_effect") == 0)    { int e = parse_led_effect(val); if (e >= 0) { cur->led_effect = e; cur->led_enabled = 1; } goto next; }
        if (strcasecmp(key, "led_duration") == 0)  { int d = atoi(val); cur->led_duration = (uint8_t)(d<1?1:d>15?15:d); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_1") == 0)         { cur->dpi[0] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_2") == 0)         { cur->dpi[1] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_3") == 0)         { cur->dpi[2] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_4") == 0)         { cur->dpi[3] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_shift") == 0)     { cur->dpi_shift   = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_default") == 0)   { cur->dpi_default = (uint8_t)atoi(val);  cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "ONBOARD_RING") == 0)  { parse_binding(&cur->onboard_ring, val); cur->onboard_enabled = 1; goto next; }
        if (strcasecmp(key, "ONBOARD_G7") == 0)    { parse_binding(&cur->onboard_g7,   val); cur->onboard_enabled = 1; goto next; }
        if (strcasecmp(key, "ONBOARD_G8") == 0)    { parse_binding(&cur->onboard_g8,   val); cur->onboard_enabled = 1; goto next; }
        for (int i = 0; i < NUM_GKEYS; i++) {
            if (strcasecmp(key, GKEY_NAMES[i]) == 0) { parse_binding(&cur->gkeys_normal[i], val); goto next; }
            char sh[32]; snprintf(sh, sizeof(sh), "SHIFT_%s", GKEY_NAMES[i]);
            if (strcasecmp(key, sh) == 0)             { parse_binding(&cur->gkeys_gshift[i], val); goto next; }
        }
        for (int i = 0; i < NUM_MKEYS; i++)
            if (strcasecmp(key, MKEY_NAMES[i]) == 0) { parse_binding(&cur->mkeys[i], val); goto next; }
        fprintf(stderr, "Unknown config key: %s\n", key);
        next:;
    }
    fclose(f);
    if (cfg->num_profiles == 0) config_defaults(cfg);
    printf("Loaded %d profile(s)\n", cfg->num_profiles);
}

static void config_write_default(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen config"); return; }
    const char *profile_names[3] = {"profile1", "profile2", "profile3"};
    const int   abs_misc_vals[3] = {32, 64, 128};
    fprintf(f,
        "# G600 Daemon Config — 3 profiles matching mouse onboard slots\n"
        "# Rebind:  G9 = F13\n"
        "# Macro:   G9 = macro:<press|release>:<once|repeat|toggle>: KEY, Nms, KEY\n"
        "# Onboard: ONBOARD_G7/G8/RING = key (stored in mouse flash)\n"
        "# Special: PROFILE_NEXT, DPI_UP, DPI_DOWN, DPI_SHIFT\n\n"
    );
    for (int p = 0; p < 3; p++) {
        fprintf(f, "[%s]\n", profile_names[p]);
        fprintf(f, "abs_misc = %d\n\n", abs_misc_vals[p]);
        fprintf(f, "# LED\n#led_r = 255\n#led_g = 255\n#led_b = 255\n#led_effect = solid\n#led_duration = 4\n\n");
        fprintf(f, "# DPI\n#dpi_1 = 1200\n#dpi_2 = 0\n#dpi_3 = 0\n#dpi_4 = 0\n#dpi_default = 1\n#dpi_shift = 0\n\n");
        fprintf(f, "ONBOARD_RING = BTN_MIDDLE\nONBOARD_G7   = BTN_MIDDLE\nONBOARD_G8   = PROFILE_NEXT\n\n");
        const char *gnames[] = {"G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","G19","G20"};
        const char *fn[]     = {"F13","F14","F15","F16","F17","F18","F19","F20","F21","F22","F23","F24"};
        const char *sfn[]    = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
        for (int i = 0; i < 12; i++) fprintf(f, "%s = %s\n", gnames[i], fn[i]);
        fprintf(f, "\n");
        for (int i = 0; i < 12; i++) fprintf(f, "SHIFT_%s = %s\n", gnames[i], sfn[i]);
        fprintf(f, "\nMIDDLE     = BTN_MIDDLE\nTILT_LEFT  = BTN_SIDE\nTILT_RIGHT = BTN_EXTRA\n\n");
    }
    fclose(f);
    printf("Wrote default config to %s\n", path);
}

/* ──────────────────────────────────────────────
   uinput
   ────────────────────────────────────────────── */
static void emit_locked(int type, int code, int val) {
    struct input_event ev = {0};
    ev.type = type; ev.code = code; ev.value = val;
    if (write(uinput_fd, &ev, sizeof(ev)) < 0) perror("uinput write");
}

static void emit(int type, int code, int val) {
    pthread_mutex_lock(&uinput_lock);
    emit_locked(type, code, val);
    pthread_mutex_unlock(&uinput_lock);
}

static int uinput_setup(void) {
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) { perror("open /dev/uinput"); return -1; }
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_SYN);
    /* EV_REL needed so compositors treat this device as a pointer and
       accept BTN_LEFT/RIGHT/MIDDLE from it */
    ioctl(uinput_fd, UI_SET_EVBIT, EV_REL);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_X);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_Y);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_WHEEL);
    for (const KeyEntry *e = KEY_TABLE; e->name; e++)
        if (e->code > 0 && e->code < SPECIAL_BASE) ioctl(uinput_fd, UI_SET_KEYBIT, e->code);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_SIDE);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_EXTRA);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_RIGHT);
    struct uinput_setup usetup = {0};
    strncpy(usetup.name, "G600 Macro Keys", UINPUT_MAX_NAME_SIZE);
    usetup.id.bustype = BUS_VIRTUAL; usetup.id.vendor = 0x046d; usetup.id.product = 0x0001;
    if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0) { perror("UI_DEV_SETUP"); return -1; }
    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0)          { perror("UI_DEV_CREATE"); return -1; }
    return 0;
}

/* ──────────────────────────────────────────────
   Macro execution
   ────────────────────────────────────────────── */
static void exec_step(const MacroStep *step) {
    if (step->type == STEP_DELAY) {
        struct timespec ts = { step->delay_ms / 1000, (step->delay_ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        return;
    }
    pthread_mutex_lock(&uinput_lock);
    for (int i = 0; i < step->nkeys; i++) emit_locked(EV_KEY, step->keys[i], 1);
    emit_locked(EV_SYN, SYN_REPORT, 0);
    for (int i = step->nkeys - 1; i >= 0; i--) emit_locked(EV_KEY, step->keys[i], 0);
    emit_locked(EV_SYN, SYN_REPORT, 0);
    pthread_mutex_unlock(&uinput_lock);
}

static void exec_sequence(const Binding *b) {
    for (int i = 0; i < b->nsteps; i++) exec_step(&b->steps[i]);
}

typedef struct {
    Binding  binding;
    volatile int running;
    volatile int held;
    pthread_t tid;
    int       active;
    int       detached; // new
} MacroThread;

#define MAX_MACRO_THREADS (NUM_GKEYS * 2 + NUM_MKEYS)
static MacroThread     macro_threads[MAX_MACRO_THREADS];
static pthread_mutex_t mt_lock = PTHREAD_MUTEX_INITIALIZER;

static void *macro_thread_fn(void *arg) {
    MacroThread *mt = arg;
    const Binding *b = &mt->binding;
    if (b->hold == HOLD_ONCE) {
        exec_sequence(b);
    } else if (b->hold == HOLD_REPEAT) {
        while (mt->held && mt->running) {
            exec_sequence(b);
            struct timespec ts = {0, 1000000}; /* 1ms yield */
            nanosleep(&ts, NULL);
        }
    } else if (b->hold == HOLD_TOGGLE) {
        while (mt->running) {
            exec_sequence(b);
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
        }
    }
    pthread_mutex_lock(&mt_lock);
    mt->active = 0;
    pthread_mutex_unlock(&mt_lock);
    return NULL;
}

static MacroThread *alloc_thread(void) {
    for (int i = 0; i < MAX_MACRO_THREADS; i++)
        if (!macro_threads[i].active) return &macro_threads[i];
    return NULL;
}

static void macro_on_press(const Binding *b, int slot) {
    pthread_mutex_lock(&mt_lock);
    for (int i = 0; i < MAX_MACRO_THREADS; i++) {
        MacroThread *mt = &macro_threads[i];
        if (mt->active && mt->binding.keycode == slot) {
            if (mt->binding.hold == HOLD_TOGGLE) {
                mt->running = 0;
                pthread_mutex_unlock(&mt_lock);
                pthread_join(mt->tid, NULL);
                return;
            }
            mt->held = 0;
            pthread_mutex_unlock(&mt_lock);
            return;
        }
    }
    if (b->trigger != TRIG_PRESS) { pthread_mutex_unlock(&mt_lock); return; }
    MacroThread *mt = alloc_thread();
    if (!mt) { pthread_mutex_unlock(&mt_lock); fprintf(stderr, "No macro thread slots\n"); return; }
    mt->binding = *b;
    mt->binding.keycode = slot;
    mt->running = 1; mt->held = 1; mt->active = 1;
    pthread_mutex_unlock(&mt_lock);
    fprintf(stderr, "SPAWN slot=%d is_macro=%d nsteps=%d first_key=%d trigger=%d hold=%d\n",
        slot, mt->binding.is_macro, mt->binding.nsteps,
        mt->binding.nsteps > 0 ? mt->binding.steps[0].keys[0] : -1,
        mt->binding.trigger, mt->binding.hold);
    pthread_create(&mt->tid, NULL, macro_thread_fn, mt);
    pthread_detach(mt->tid);
}

static void macro_on_release(const Binding *b, int slot) {
    pthread_mutex_lock(&mt_lock);
    for (int i = 0; i < MAX_MACRO_THREADS; i++) {
        MacroThread *mt = &macro_threads[i];
        if (mt->active && mt->binding.keycode == slot && mt->binding.hold == HOLD_REPEAT) {
            mt->held = 0;
            pthread_mutex_unlock(&mt_lock);
            return;
        }
    }
    if (b->trigger != TRIG_RELEASE) { pthread_mutex_unlock(&mt_lock); return; }
    MacroThread *mt = alloc_thread();
    if (!mt) { pthread_mutex_unlock(&mt_lock); return; }
    mt->binding = *b;
    mt->binding.keycode = slot;
    mt->running = 1; mt->held = 0; mt->active = 1;
    pthread_mutex_unlock(&mt_lock);
    pthread_create(&mt->tid, NULL, macro_thread_fn, mt);
}

static void stop_all_macros(void) {
    pthread_mutex_lock(&mt_lock);
    pthread_t tids[MAX_MACRO_THREADS];
    int n = 0;
    for (int i = 0; i < MAX_MACRO_THREADS; i++)
        if (macro_threads[i].active) {
            macro_threads[i].running = 0;
            macro_threads[i].held    = 0;
            tids[n++] = macro_threads[i].tid;
        }
    pthread_mutex_unlock(&mt_lock);
    for (int i = 0; i < n; i++) pthread_join(tids[i], NULL);
}

/* ──────────────────────────────────────────────
   HID / LED / DPI
   ────────────────────────────────────────────── */
static uint8_t profile_report_id(int idx) {
    switch (idx % 3) { case 0: return REPORT_ID_P0; case 1: return REPORT_ID_P1; default: return REPORT_ID_P2; }
}

static void set_active_resolution(int slot) {
    if (hidraw_fd < 0) return;
    /* matches logitech_g600_set_current_resolution() in kernel driver */
    uint8_t buf[4] = {0xF0, (uint8_t)(0x40 | (slot << 1)), 0x00, 0x00};
    if (ioctl(hidraw_fd, HIDIOCSFEATURE(4), buf) < 0)
        perror("HIDIOCSFEATURE (active resolution)");
}

static void apply_dpi_slot(int slot) {
    cur_dpi_slot = slot;
    set_active_resolution(slot);
    printf("DPI slot -> %d\n", slot + 1);
}

static void dpi_step(int dir) {
    Profile *p = active_profile();
    int slot = cur_dpi_slot;
    for (int i = 0; i < 4; i++) {
        slot = (slot + dir + 4) % 4;
        /* if dpi_enabled, skip disabled slots; otherwise just step */
        if (!p->dpi_enabled || p->dpi[slot] > 0) {
            apply_dpi_slot(slot);
            return;
        }
    }
}

/* Single read-modify-write for all profile settings.
   Offsets from struct logitech_g600_profile_report __attribute__((packed)):
     [0]     id
     [1]     led_red       [2] led_green    [3] led_blue
     [4]     led_effect    [5] led_duration
     [6-10]  unknown1[5]
     [11]    frequency
     [12]    dpi_shift     [13] dpi_default  [14-17] dpi[4]
     [18-30] unknown2[13]
     [31-90] buttons[20]  (3 bytes each: code, modifier, key)
     [91-93] g_shift_color[3]
     [94-153] g_shift_buttons[20]
*/
/* Drop-in replacement for profile_apply() in daemon/main.c */

static void profile_apply(const Profile *p, int idx) {
    if (hidraw_fd < 0) return;

    uint8_t buf[G600_REPORT_SIZE] = {0};
    uint8_t rid = profile_report_id(idx);

    buf[0] = rid;
    if (ioctl(hidraw_fd, HIDIOCGFEATURE(G600_REPORT_SIZE), buf) < 0) {
        fprintf(stderr, "HIDIOCGFEATURE slot %d (report 0x%02x): %s\n",
                idx, rid, strerror(errno));
        return;
    }
    buf[0] = rid;

    /* LED */
    buf[1] = p->led_r;
    buf[2] = p->led_g;
    buf[3] = p->led_b;
    buf[4] = p->led_effect;
    buf[5] = (p->led_effect == LED_SOLID) ? 0 : p->led_duration;

    /* DPI — always write something sane, even if dpi_enabled is false.
       Otherwise the mouse sits on DPI slot 0 which may be 0 (disabled). */
    if (p->dpi_enabled) {
        buf[12] = p->dpi_shift   ? (uint8_t)(p->dpi_shift  / 50) : 0;
        buf[13] = p->dpi_default ? p->dpi_default : 1;
        for (int i = 0; i < 4; i++)
            buf[14 + i] = p->dpi[i] ? (uint8_t)(p->dpi[i] / 50) : 0;
    } else {
        /* Minimal sane default: slot 1 = 1200 DPI, others disabled. */
        buf[12] = 0;
        buf[13] = 1;
        buf[14] = 1200 / 50;
        buf[15] = 0;
        buf[16] = 0;
        buf[17] = 0;
    }

    /* Mouse buttons (slots 0-4) */
    for (int i = 0; i < NUM_MKEYS; i++) {
        int off = 31 + i * 3;
        uint8_t code = 0, mod = 0, key = 0;
        if (binding_to_g600btn(&p->mkeys[i], &code, &mod, &key) == 0) {
            buf[off + 0] = code;
            buf[off + 1] = mod;
            buf[off + 2] = key;
        } else {
            buf[off + 0] = 0;
            buf[off + 1] = 0;
            buf[off + 2] = 0;
        }
    }

    /* Onboard (slots 5-7) */
    if (p->onboard_enabled) {
        const Binding *ob[3] = { &p->onboard_ring, &p->onboard_g7, &p->onboard_g8 };
        for (int i = 0; i < 3; i++) {
            int off = 31 + (5 + i) * 3;
            uint8_t code = 0, mod = 0, key = 0;
            if (binding_to_g600btn(ob[i], &code, &mod, &key) == 0) {
                buf[off + 0] = code;
                buf[off + 1] = mod;
                buf[off + 2] = key;
            } else {
                buf[off + 0] = 0;
                buf[off + 1] = 0;
                buf[off + 2] = 0;
            }
        }
    }

    /* G-keys (slots 8-19): ALWAYS write factory-default scancodes
       (1,2,3,4,5,6,7,8,9,0,-,= for normal; F1..F12 for g-shift).
       The daemon's handle_kbd() reads these scancodes from fd_kbd and
       dispatches to the real binding via uinput. DO NOT change these. */
    static const uint8_t gkey_hid_normal[NUM_GKEYS] = {
        0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,  /* 1,2,3,4,5,6  → G9..G14 */
        0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e   /* 7,8,9,0,-,=  → G15..G20 */
    };
    static const uint8_t gkey_hid_shift[NUM_GKEYS] = {
        0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,  /* F1..F6 */
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45   /* F7..F12 */
    };
    for (int i = 0; i < NUM_GKEYS; i++) {
        int btn = 8 + i;
        int off  = 31 + btn * 3;
        int off2 = 94 + btn * 3;
        buf[off + 0] = 0; buf[off + 1] = 0; buf[off + 2] = gkey_hid_normal[i];
        buf[off2 + 0] = 0; buf[off2 + 1] = 0; buf[off2 + 2] = gkey_hid_shift[i];
    }

    /* G-shift color mirror */
    buf[91] = p->led_r;
    buf[92] = p->led_g;
    buf[93] = p->led_b;

    fprintf(stderr, "APPLY profile %d (%s): slots 0-7 = ", idx, p->name);
    for (int i = 0; i < 8; i++)
        fprintf(stderr, "%02x:%02x:%02x ", buf[31+i*3], buf[31+i*3+1], buf[31+i*3+2]);
    fprintf(stderr, "\n");

    if (ioctl(hidraw_fd, HIDIOCSFEATURE(G600_REPORT_SIZE), buf) < 0)
        fprintf(stderr, "HIDIOCSFEATURE slot %d (report 0x%02x): %s\n",
                idx, rid, strerror(errno));
    else
        printf("Profile %d (%s): LED #%02x%02x%02x effect=%d — OK\n",
               idx, p->name, buf[1], buf[2], buf[3], buf[4]);
}

/* ──── Also fix write_all_profiles() — the sleep is broken ──── */

static void write_all_profiles(void) {
    printf("Writing all %d profiles to mouse flash...\n", cfg.num_profiles);
    for (int i = 0; i < cfg.num_profiles && i < 3; i++) {
        int abs = cfg.profiles[i].abs_misc_val;
        int slot = (abs == 32) ? 0 : (abs == 64) ? 1 : (abs == 128) ? 2 : i;
        profile_apply(&cfg.profiles[i], slot);
        /* 50 ms — tv_nsec must be < 1e9. Your previous 5e9 overflowed. */
        struct timespec ts = {5, 50000000};
        nanosleep(&ts, NULL);
    }
    printf("All profiles written.\n");
}


static void switch_profile(int idx) {
    if (idx == cur_profile) return;
    stop_all_macros();
    gshift_active = 0;
    struct timespec ts = {5, 50000000};
    nanosleep(&ts, NULL);

    cur_profile = idx;
    printf("Profile: %s\n", cfg.profiles[idx].name);
    int abs = cfg.profiles[idx].abs_misc_val;
    int slot = (abs == 32) ? 0 : (abs == 64) ? 1 : (abs == 128) ? 2 : idx;
    profile_apply(&cfg.profiles[idx], slot);
}

static void switch_profile_by_abs(int abs_val) {
    if (abs_val == 0) return; /* ignore DPI/reset events */
    for (int i = 0; i < cfg.num_profiles; i++) {
        if (cfg.profiles[i].abs_misc_val == abs_val) {
            switch_profile(i);
            return;
        }
    }
}

/* ──────────────────────────────────────────────
   Binding dispatch
   ────────────────────────────────────────────── */
static void dispatch(const Binding *b, int value, int slot) {
    if (!b->is_macro) {
        if (b->keycode == 0) return;
        if (value == 1) {
            if (b->keycode == SPECIAL_DPI_UP)        { dpi_step(+1); return; }
            if (b->keycode == SPECIAL_DPI_DOWN)       { dpi_step(-1); return; }
            if (b->keycode == SPECIAL_PROFILE_NEXT)   { switch_profile((cur_profile + 1) % cfg.num_profiles); return; }
            if (b->keycode == SPECIAL_PROFILE_PREV)   { switch_profile((cur_profile - 1 + cfg.num_profiles) % cfg.num_profiles); return; }
            if (b->keycode == SPECIAL_PROFILE_1)      { switch_profile(0); return; }
            if (b->keycode == SPECIAL_PROFILE_2)      { switch_profile(1); return; }
            if (b->keycode == SPECIAL_PROFILE_3)      { switch_profile(2); return; }
        }
        if (b->keycode >= SPECIAL_BASE) return;
        emit(EV_KEY, b->keycode, value);
        emit(EV_SYN, SYN_REPORT, 0);
    } else {
        if (value == 1) macro_on_press(b, slot);
        else if (value == 0) macro_on_release(b, slot);
    }
}

/* ──────────────────────────────────────────────
   Event handlers
   ────────────────────────────────────────────── */
static void handle_kbd(struct input_event *ev) {
    if (ev->type != EV_KEY) return;

    /* The G600 ring signals gshift in two ways depending on firmware/mode:
       1. ABS_MISC event on fd_prof  → gshift_active set by handle_prof()
       2. Legacy: hardware holds LSHIFT+B while ring is pressed             */
    static int lshift_down = 0, keyb_down = 0;
    if (ev->code == KEY_LEFTSHIFT) { lshift_down = (ev->value != 0); if (lshift_down && keyb_down) gshift_active = 1; else if (!lshift_down) gshift_active = 0; return; }
    if (ev->code == KEY_B)         { keyb_down   = (ev->value != 0); if (lshift_down && keyb_down) gshift_active = 1; else if (!keyb_down)   gshift_active = 0; return; }

    for (int i = 0; i < NUM_GKEYS; i++) {
        if (ev->code == SRC_GKEYS[i]) {
            int shifted = gshift_active;
            Binding *b = shifted ? &active_profile()->gkeys_gshift[i]
                                 : &active_profile()->gkeys_normal[i];
            dispatch(b, ev->value, shifted ? NUM_GKEYS + i : i);
            return;
        }

        if (ev->code == SRC_GKEYS_SHIFT[i]) {
            Binding *b = &active_profile()->gkeys_gshift[i];
            dispatch(b, ev->value, NUM_GKEYS + i);
            return;
        }
    }
}

static void handle_prof(struct input_event *ev) {
    if (ev->type != EV_ABS || ev->code != ABS_MISC) return;

    int val = ev->value;

    /* The G600 profile ring fires noisy transitional values (e.g. 96, 129,
       131, 3) as the physical ring moves between positions.  Only the clean
       power-of-two values (32, 64, 128) represent a stable profile slot;
       value 0 signals "ring released" (end of gshift).  Ignore everything
       else to prevent spurious profile switches mid-transition. */
    if (val != 0 && val != 32 && val != 64 && val != 128) return;

    if (val == 0) { gshift_active = 0; return; }

    for (int i = 0; i < cfg.num_profiles; i++) {
        if (cfg.profiles[i].abs_misc_val == val) {
            if (i == cur_profile)
                gshift_active = 1;   /* ring held in current profile → gshift */
            else
                switch_profile(i);   /* hardware profile switch               */
            return;
        }
    }
}

/* ──────────────────────────────────────────────
   Device discovery
   ────────────────────────────────────────────── */
static int g600_check_device(const char *path, int *has_rel, int *has_abs, int *has_gkeys) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;
    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) { close(fd); return 0; }
    if (strstr(name, "G600") == NULL) { close(fd); return 0; }
    struct input_id id;
    if (ioctl(fd, EVIOCGID, &id) < 0) { close(fd); return 0; }
    if (id.vendor != G600_VID || id.product != G600_PID) { close(fd); return 0; }
    uint8_t evbits[EV_MAX/8+1] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    *has_rel = (evbits[EV_REL/8] >> (EV_REL%8)) & 1;
    *has_abs = (evbits[EV_ABS/8] >> (EV_ABS%8)) & 1;
    uint8_t keybits[KEY_MAX/8+1] = {0};
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    int has_k1 = (keybits[KEY_1/8] >> (KEY_1%8)) & 1;
    int has_b  = (keybits[KEY_B/8] >> (KEY_B%8)) & 1;
    *has_gkeys = has_k1 && has_b && !(*has_rel);
    close(fd);
    return 1;
}

static int g600_find_devices(char *kbd, char *mouse, char *prof, size_t bufsz) {
    int found_kbd=0, found_mouse=0, found_prof=0;
    char path[64];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int has_rel=0, has_abs=0, has_gkeys=0;
        if (!g600_check_device(path, &has_rel, &has_abs, &has_gkeys)) continue;
        printf("Found G600 device: %s (rel=%d abs=%d gkeys=%d)\n", path, has_rel, has_abs, has_gkeys);
        if (has_rel && !found_mouse)       { strncpy(mouse, path, bufsz-1); found_mouse=1; }
        else if (has_gkeys && !found_kbd)  { strncpy(kbd,   path, bufsz-1); found_kbd=1;   }
        else if (has_abs && !found_prof)   { strncpy(prof,  path, bufsz-1); found_prof=1;  }
    }
    return found_kbd && found_mouse && found_prof;
}

static int g600_find_hidraw(char *out, size_t bufsz) {
    if (access(DEV_HIDRAW_SYMLINK, F_OK) == 0) { strncpy(out, DEV_HIDRAW_SYMLINK, bufsz-1); return 1; }
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/hidraw%d", i);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        struct hidraw_devinfo info = {0};
        if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
            (uint16_t)info.vendor == G600_VID && (uint16_t)info.product == G600_PID) {
            uint8_t buf[G600_REPORT_SIZE] = {0};
            buf[0] = REPORT_ID_P0;
            if (ioctl(fd, HIDIOCGFEATURE(G600_REPORT_SIZE), buf) == 0) {
                close(fd);
                strncpy(out, path, bufsz-1);
                return 1;
            }
        }
        close(fd);
    }
    return 0;
}

/* ──────────────────────────────────────────────
   Main
   ────────────────────────────────────────────── */
int main(void) {
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "HOME not set\n"); return 1; }
    char cfg_dir[512], cfg_path[512];
    snprintf(cfg_dir,  sizeof(cfg_dir),  "%s%s", home, CFG_DIR);
    snprintf(cfg_path, sizeof(cfg_path), "%s%s", home, CFG_FILE);

    mkdir(cfg_dir, 0755);
    if (access(cfg_path, F_OK) != 0) config_write_default(cfg_path);
    config_load(&cfg, cfg_path);

    char dev_kbd[64]={0}, dev_mouse[64]={0}, dev_prof[64]={0}, dev_hidraw[128]={0};
    printf("Scanning for G600 devices...\n");
    if (!g600_find_devices(dev_kbd, dev_mouse, dev_prof, sizeof(dev_kbd))) {
        fprintf(stderr, "Could not find all G600 event devices\n");
        return 1;
    }
    if (!g600_find_hidraw(dev_hidraw, sizeof(dev_hidraw)))
        fprintf(stderr, "Warning: Could not find G600 hidraw device (LED/DPI disabled)\n");

    printf("KBD:    %s\nMOUSE:  %s\nPROF:   %s\nHIDRAW: %s\n",
           dev_kbd, dev_mouse, dev_prof, dev_hidraw[0] ? dev_hidraw : "(none)");

    int fd_kbd   = open(dev_kbd,   O_RDONLY | O_NONBLOCK); if (fd_kbd   < 0) { perror("open kbd");   return 1; }
    int fd_mouse = open(dev_mouse, O_RDONLY | O_NONBLOCK); if (fd_mouse < 0) { perror("open mouse"); return 1; }
    int fd_prof  = open(dev_prof,  O_RDONLY | O_NONBLOCK); if (fd_prof  < 0) { perror("open prof");  return 1; }

    hidraw_fd = dev_hidraw[0] ? open(dev_hidraw, O_RDWR) : -1;
    if (hidraw_fd < 0) perror("open hidraw (LED/DPI disabled)");

    if (uinput_setup() < 0) return 1;
    ioctl(fd_kbd, EVIOCGRAB, 1);

    int fd_ino = inotify_init1(IN_NONBLOCK);
    int wd = -1;
    if (fd_ino >= 0) wd = inotify_add_watch(fd_ino, cfg_path, IN_CLOSE_WRITE);

    cur_dpi_slot = active_profile()->dpi_default - 1;
    if (cur_dpi_slot < 0) cur_dpi_slot = 0;

    write_all_profiles();
    printf("g600d running. Profile: %s\n", active_profile()->name);

    struct pollfd fds[3] = {
        {fd_kbd,  POLLIN, 0},
        {fd_prof, POLLIN, 0},
        {fd_ino,  POLLIN, 0},
    };

    struct input_event ev;
    while (running) {
        int r = poll(fds, 3, 500);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (fds[0].revents & POLLIN) while (read(fd_kbd,  &ev, sizeof(ev)) > 0) handle_kbd(&ev);
        if (fds[1].revents & POLLIN) while (read(fd_prof, &ev, sizeof(ev)) > 0) handle_prof(&ev);
        if (fds[2].revents & POLLIN) {
            char ibuf[512]; while (read(fd_ino, ibuf, sizeof(ibuf)) > 0);
            printf("Config changed, reloading...\n");
            stop_all_macros();
            config_load(&cfg, cfg_path);
            cur_dpi_slot = active_profile()->dpi_default - 1;
            if (cur_dpi_slot < 0) cur_dpi_slot = 0;
            write_all_profiles();
        }
    }

    stop_all_macros();
    ioctl(fd_kbd, EVIOCGRAB, 0);
    if (fd_ino >= 0) { inotify_rm_watch(fd_ino, wd); close(fd_ino); }
    if (hidraw_fd >= 0) close(hidraw_fd);
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd); close(fd_kbd); close(fd_mouse); close(fd_prof);
    printf("g600d stopped.\n");
    return 0;
}
