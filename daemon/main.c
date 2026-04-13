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
#include <linux/hidraw.h>
#include <strings.h>

/* ── devices ── */
/* Stable symlinks for hidraw — interface 1 has the profile feature reports */
#define DEV_HIDRAW_SYMLINK "/dev/input/by-id/usb-Logitech_Gaming_Mouse_G600_4FD9CE8DE3650017-if01-hidraw"
#define G600_NAME          "Logitech Gaming Mouse G600"
#define G600_VID           0x046d
#define G600_PID           0xc24a

/* ── config ── */
#define CFG_DIR  "/.config/g600d"
#define CFG_FILE "/.config/g600d/g600d.conf"

/* ── limits ── */
#define MAX_PROFILES   3
#define NUM_GKEYS     12
#define NUM_MKEYS      5
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
    {"BTN_LEFT",BTN_LEFT},{"BTN_RIGHT",BTN_RIGHT},{"BTN_MIDDLE",BTN_MIDDLE},{"BTN_SIDE",BTN_SIDE},{"BTN_EXTRA",BTN_EXTRA},
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
    int      keys[MAX_COMBO_KEYS]; /* for STEP_KEYS */
    int      nkeys;
    int      delay_ms;             /* for STEP_DELAY */
} MacroStep;

typedef struct {
    int          is_macro;
    /* rebind (is_macro=0) */
    int          keycode;
    /* macro (is_macro=1) */
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

/* ── Linux keycode → HID keycode ────────────────────────────────────── */
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
    /* modifiers — returned as mod flags with key=0x00 */
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

/* Convert a single linux keycode to HID keycode + modifier flags */
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

/* Convert a Binding (single key rebind only) to a G600Button struct.
 * Returns 0 on success, -1 if not representable in onboard flash. */
static int binding_to_g600btn(const Binding *b, uint8_t *code, uint8_t *mod, uint8_t *key) {
    if (b->is_macro) return -1; /* macros can't be stored onboard */
    int kc = b->keycode;

    /* special virtual keycodes */
    if (kc == SPECIAL_DPI_UP)       { *code=0x11; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_DPI_DOWN)     { *code=0x12; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_PROFILE_NEXT) { *code=0x14; *mod=0; *key=0; return 0; }
    if (kc == SPECIAL_PROFILE_PREV) { *code=0x14; *mod=0; *key=0; return 0; } /* no prev on G600 */
    if (kc == 0) { *code=0; *mod=0; *key=0; return 0; } /* NONE */

    /* mouse buttons */
    if (kc == BTN_LEFT)   { *code=0; *mod=1; *key=0; return 0; }
    if (kc == BTN_RIGHT)  { *code=0; *mod=2; *key=0; return 0; }
    if (kc == BTN_MIDDLE) { *code=0; *mod=3; *key=0; return 0; }
    if (kc == BTN_SIDE)   { *code=0; *mod=4; *key=0; return 0; }
    if (kc == BTN_EXTRA)  { *code=0; *mod=5; *key=0; return 0; }

    /* keyboard key */
    uint8_t hk=0, hm=0;
    if (linux_to_hid(kc, &hk, &hm) == 0) {
        *code=0; *mod=hm; *key=hk; return 0;
    }

    return -1;
}


/* ──────────────────────────────────────────────
   Profile
   ────────────────────────────────────────────── */
static const int SRC_GKEYS[NUM_GKEYS] = {
    KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,
    KEY_7,KEY_8,KEY_9,KEY_0,KEY_MINUS,KEY_EQUAL,
};
static const char *GKEY_NAMES[NUM_GKEYS] = {
    "G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","G19","G20"
};
static const int SRC_MKEYS[NUM_MKEYS] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA };
static const char *MKEY_NAMES[NUM_MKEYS] = { "BTN_LEFT","BTN_RIGHT","MIDDLE","TILT_LEFT","TILT_RIGHT" };

typedef struct {
    char    name[64];
    int     abs_misc_val;
    Binding gkeys_normal[NUM_GKEYS];
    Binding gkeys_gshift[NUM_GKEYS];
    Binding mkeys[NUM_MKEYS];
    uint8_t led_r, led_g, led_b;
    uint8_t led_effect, led_duration;
    int     led_enabled;
    /* DPI */
    uint16_t dpi[4];        /* slots 1-4, 0=disabled */
    uint16_t dpi_shift;     /* G-Shift DPI, 0=disabled */
    uint8_t  dpi_default;   /* 1-4 */
    int      dpi_enabled;
    /* onboard button remaps (written to HID flash) */
    Binding  onboard_ring;   /* button index 2 */
    Binding  onboard_g7;     /* button index 6 */
    Binding  onboard_g8;     /* button index 7 */
    int      onboard_enabled;
} Profile;

typedef struct {
    Profile profiles[MAX_PROFILES];
    int     num_profiles;
} Config;

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

/* ──────────────────────────────────────────────
   Macro parser
   ────────────────────────────────────────────── */

/* parse "LEFTCTRL+LEFTSHIFT+C" into keys array, return nkeys */
static int parse_combo(const char *s, int keys[MAX_COMBO_KEYS]) {
    int n = 0;
    char buf[256]; strncpy(buf, s, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    char *tok = strtok(buf, "+");
    while (tok && n < MAX_COMBO_KEYS) {
        /* trim */
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

/*
 * Parse: macro:<trigger>:<hold>: step, step, ...
 * steps: "LEFTCTRL+C"  or  "50ms"
 */
static int parse_macro_binding(Binding *b, const char *val) {
    /* val points past "macro:" */
    char buf[1024]; strncpy(buf, val, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';

    /* trigger */
    char *p = buf;
    if (strncasecmp(p, "press:", 6) == 0)        { b->trigger = TRIG_PRESS;   p += 6; }
    else if (strncasecmp(p, "release:", 8) == 0) { b->trigger = TRIG_RELEASE; p += 8; }
    else { fprintf(stderr, "Macro: expected press:|release:\n"); return -1; }

    /* hold */
    if (strncasecmp(p, "once:", 5) == 0)         { b->hold = HOLD_ONCE;   p += 5; }
    else if (strncasecmp(p, "repeat:", 7) == 0)  { b->hold = HOLD_REPEAT; p += 7; }
    else if (strncasecmp(p, "toggle:", 7) == 0)  { b->hold = HOLD_TOGGLE; p += 7; }
    else { fprintf(stderr, "Macro: expected once:|repeat:|toggle:\n"); return -1; }

    /* skip spaces */
    while (*p == ' ') p++;

    /* parse comma-separated steps */
    b->nsteps = 0;
    char *saveptr;
    char *tok = strtok_r(p, ",", &saveptr);
    while (tok && b->nsteps < MAX_MACRO_STEPS) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';

        MacroStep *step = &b->steps[b->nsteps];

        /* delay? e.g. "50ms" */
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
    /* trim leading spaces */
    while (*val == ' ') val++;

    if (strncasecmp(val, "macro:", 6) == 0)
        return parse_macro_binding(b, val + 6);

    /* plain key */
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

        if (strcasecmp(key, "abs_misc") == 0) { cur->abs_misc_val = atoi(val); goto next; }
        if (strcasecmp(key, "led_r") == 0)    { cur->led_r = (uint8_t)atoi(val); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "led_g") == 0)    { cur->led_g = (uint8_t)atoi(val); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "led_b") == 0)    { cur->led_b = (uint8_t)atoi(val); cur->led_enabled = 1; goto next; }
        if (strcasecmp(key, "led_effect") == 0) {
            int e = parse_led_effect(val); if (e >= 0) { cur->led_effect = e; cur->led_enabled = 1; } goto next;
        }
        if (strcasecmp(key, "dpi_1") == 0)       { cur->dpi[0] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_2") == 0)       { cur->dpi[1] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_3") == 0)       { cur->dpi[2] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_4") == 0)       { cur->dpi[3] = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_shift") == 0)   { cur->dpi_shift   = (uint16_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "dpi_default") == 0) { cur->dpi_default  = (uint8_t)atoi(val); cur->dpi_enabled = 1; goto next; }
        if (strcasecmp(key, "led_duration") == 0) {
            int d = atoi(val); cur->led_duration = (uint8_t)(d<1?1:d>15?15:d); cur->led_enabled = 1; goto next;
        }

        if (strcasecmp(key, "ONBOARD_RING") == 0) { parse_binding(&cur->onboard_ring, val); cur->onboard_enabled = 1; goto next; }
        if (strcasecmp(key, "ONBOARD_G7")   == 0) { parse_binding(&cur->onboard_g7,   val); cur->onboard_enabled = 1; goto next; }
        if (strcasecmp(key, "ONBOARD_G8")   == 0) { parse_binding(&cur->onboard_g8,   val); cur->onboard_enabled = 1; goto next; }

        /* G-keys */
        for (int i = 0; i < NUM_GKEYS; i++) {
            if (strcasecmp(key, GKEY_NAMES[i]) == 0)         { parse_binding(&cur->gkeys_normal[i], val); goto next; }
            char sh[32]; snprintf(sh, sizeof(sh), "SHIFT_%s", GKEY_NAMES[i]);
            if (strcasecmp(key, sh) == 0)                    { parse_binding(&cur->gkeys_gshift[i], val); goto next; }
        }
        /* mouse keys */
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
        "# Profile 1 = hardware slot 1 (ABS_MISC=32)\n"
        "# Profile 2 = hardware slot 2 (ABS_MISC=64)\n"
        "# Profile 3 = hardware slot 3 (ABS_MISC=128)\n"
        "# G8 cycles between them on the mouse.\n\n"
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
        fprintf(f, "# Onboard buttons (stored in mouse flash)\n");
        fprintf(f, "ONBOARD_RING = BTN_MIDDLE\n");
        fprintf(f, "ONBOARD_G7   = BTN_MIDDLE\n");
        fprintf(f, "ONBOARD_G8   = PROFILE_NEXT\n\n");
        fprintf(f, "# G-keys\n");
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
static int uinput_fd = -1;
static pthread_mutex_t uinput_lock = PTHREAD_MUTEX_INITIALIZER;

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
    for (const KeyEntry *e = KEY_TABLE; e->name; e++)
        if (e->code > 0) ioctl(uinput_fd, UI_SET_KEYBIT, e->code);
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

/* fire a single step — press all keys in order then release in reverse */
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

/* per-button macro thread state */
typedef struct {
    Binding  binding;   /* copy so config reload doesn't corrupt */
    volatile int running;
    volatile int held;  /* physical button still held? */
    pthread_t tid;
    int       active;   /* slot in use */
} MacroThread;

#define MAX_MACRO_THREADS (NUM_GKEYS * 2 + NUM_MKEYS)
static MacroThread macro_threads[MAX_MACRO_THREADS];
static pthread_mutex_t mt_lock = PTHREAD_MUTEX_INITIALIZER;

static void *macro_thread_fn(void *arg) {
    MacroThread *mt = arg;
    const Binding *b = &mt->binding;

    if (b->hold == HOLD_ONCE) {
        exec_sequence(b);
    } else if (b->hold == HOLD_REPEAT) {
        while (mt->held && mt->running) exec_sequence(b);
    } else if (b->hold == HOLD_TOGGLE) {
        /* toggle: keep running until mt->running is cleared by a second press */
        while (mt->running) exec_sequence(b);
    }

    pthread_mutex_lock(&mt_lock);
    mt->active = 0;
    pthread_mutex_unlock(&mt_lock);
    return NULL;
}

/* find existing active thread for this binding pointer identity (by slot index) */

static MacroThread *alloc_thread(void) {
    for (int i = 0; i < MAX_MACRO_THREADS; i++)
        if (!macro_threads[i].active) return &macro_threads[i];
    return NULL;
}

/*
 * slot: unique identifier per button (e.g. index into gkeys array + offset)
 * We abuse binding->keycode to store the slot when is_macro=1.
 */
static void macro_on_press(const Binding *b, int slot) {
    pthread_mutex_lock(&mt_lock);

    /* check if toggle thread already running → stop it */
    for (int i = 0; i < MAX_MACRO_THREADS; i++) {
        MacroThread *mt = &macro_threads[i];
        if (mt->active && mt->binding.keycode == slot) {
            if (mt->binding.hold == HOLD_TOGGLE) {
                mt->running = 0;
                pthread_mutex_unlock(&mt_lock);
                pthread_join(mt->tid, NULL);
                return;
            }
            /* for repeat, mark released */
            mt->held = 0;
            pthread_mutex_unlock(&mt_lock);
            return;
        }
    }

    if (b->trigger != TRIG_PRESS) { pthread_mutex_unlock(&mt_lock); return; }

    MacroThread *mt = alloc_thread();
    if (!mt) { pthread_mutex_unlock(&mt_lock); fprintf(stderr, "No macro thread slots\n"); return; }

    mt->binding = *b;
    mt->binding.keycode = slot; /* reuse keycode field as slot id */
    mt->running = 1;
    mt->held    = 1;
    mt->active  = 1;
    pthread_mutex_unlock(&mt_lock);
    pthread_create(&mt->tid, NULL, macro_thread_fn, mt);
    pthread_detach(mt->tid);
}

static void macro_on_release(const Binding *b, int slot) {
    pthread_mutex_lock(&mt_lock);

    /* signal repeat thread to stop */
    for (int i = 0; i < MAX_MACRO_THREADS; i++) {
        MacroThread *mt = &macro_threads[i];
        if (mt->active && mt->binding.keycode == slot && mt->binding.hold == HOLD_REPEAT) {
            mt->held = 0;
            pthread_mutex_unlock(&mt_lock);
            return;
        }
    }

    if (b->trigger != TRIG_RELEASE) { pthread_mutex_unlock(&mt_lock); return; }

    /* fire on release */
    MacroThread *mt = alloc_thread();
    if (!mt) { pthread_mutex_unlock(&mt_lock); return; }
    mt->binding = *b;
    mt->binding.keycode = slot;
    mt->running = 1; mt->held = 0; mt->active = 1;
    pthread_mutex_unlock(&mt_lock);
    pthread_create(&mt->tid, NULL, macro_thread_fn, mt);
    pthread_detach(mt->tid);
}

static void stop_all_macros(void) {
    pthread_mutex_lock(&mt_lock);
    for (int i = 0; i < MAX_MACRO_THREADS; i++)
        if (macro_threads[i].active) macro_threads[i].running = 0;
    pthread_mutex_unlock(&mt_lock);
}

/* ──────────────────────────────────────────────
   Binding dispatch
   ────────────────────────────────────────────── */
static void dispatch(const Binding *b, int value, int slot) {
    if (!b->is_macro) {
        /* plain rebind */
        if (b->keycode == 0) return;
        emit(EV_KEY, b->keycode, value);
        emit(EV_SYN, SYN_REPORT, 0);
    } else {
        if (value == 1) macro_on_press(b, slot);
        else if (value == 0) macro_on_release(b, slot);
    }
}

/* ──────────────────────────────────────────────
   LED
   ────────────────────────────────────────────── */
static int hidraw_fd = -1;

static uint8_t profile_report_id(int idx) {
    switch (idx % 3) { case 0: return REPORT_ID_P0; case 1: return REPORT_ID_P1; default: return REPORT_ID_P2; }
}

static void led_apply(const Profile *p, int idx) {
    if (!p->led_enabled || hidraw_fd < 0) return;
    uint8_t buf[G600_REPORT_SIZE] = {0};
    buf[0] = profile_report_id(idx);
    if (ioctl(hidraw_fd, HIDIOCGFEATURE(G600_REPORT_SIZE), buf) < 0) { perror("HIDIOCGFEATURE"); return; }
    buf[1] = p->led_r; buf[2] = p->led_g; buf[3] = p->led_b;
    buf[4] = p->led_effect;
    buf[5] = (p->led_effect == LED_SOLID) ? 0 : p->led_duration;
    if (ioctl(hidraw_fd, HIDIOCSFEATURE(G600_REPORT_SIZE), buf) < 0) perror("HIDIOCSFEATURE");
    else printf("LED #%02x%02x%02x effect=%d\n", p->led_r, p->led_g, p->led_b, p->led_effect);
}

static void dpi_apply(const Profile *p, int idx) {
    if (!p->dpi_enabled || hidraw_fd < 0) return;
    uint8_t buf[G600_REPORT_SIZE] = {0};
    buf[0] = profile_report_id(idx);
    if (ioctl(hidraw_fd, HIDIOCGFEATURE(G600_REPORT_SIZE), buf) < 0) { perror("HIDIOCGFEATURE"); return; }
    /* clamp and convert: DPI = value * 50, range 200-8200 */
    buf[12] = p->dpi_shift  ? (uint8_t)(p->dpi_shift  / 50) : 0;
    buf[13] = p->dpi_default ? p->dpi_default : 1;
    for (int i = 0; i < 4; i++)
        buf[14 + i] = p->dpi[i] ? (uint8_t)(p->dpi[i] / 50) : 0;
    if (ioctl(hidraw_fd, HIDIOCSFEATURE(G600_REPORT_SIZE), buf) < 0) perror("HIDIOCSFEATURE (dpi)");
    else printf("DPI slots: %d %d %d %d (default=%d shift=%d)\n",
                p->dpi[0], p->dpi[1], p->dpi[2], p->dpi[3],
                p->dpi_default, p->dpi_shift);
}

static void onboard_apply(const Profile *p, int idx) {
    if (!p->onboard_enabled || hidraw_fd < 0) return;
    uint8_t buf[G600_REPORT_SIZE] = {0};
    buf[0] = profile_report_id(idx);
    if (ioctl(hidraw_fd, HIDIOCGFEATURE(G600_REPORT_SIZE), buf) < 0) { perror("HIDIOCGFEATURE"); return; }

    /* write ring (button 2), G7 (button 6), G8 (button 7) */
    const struct { int idx; const Binding *b; } btns[] = {
        {2, &p->onboard_ring},
        {6, &p->onboard_g7},
        {7, &p->onboard_g8},
    };
    for (int i = 0; i < 3; i++) {
        int offset = 31 + btns[i].idx * 3;
        uint8_t code=0, mod=0, key=0;
        if (binding_to_g600btn(btns[i].b, &code, &mod, &key) == 0) {
            buf[offset]   = code;
            buf[offset+1] = mod;
            buf[offset+2] = key;
        } else {
            fprintf(stderr, "onboard: binding not representable in flash\n");
        }
    }

    if (ioctl(hidraw_fd, HIDIOCSFEATURE(G600_REPORT_SIZE), buf) < 0)
        perror("HIDIOCSFEATURE (onboard)");
    else
        printf("Onboard buttons written to profile %d\n", idx);
}

/* ──────────────────────────────────────────────
   Globals
   ────────────────────────────────────────────── */
static volatile int running = 1;
static Config cfg;
static int cur_profile = 0;
static int gshift = 0;

static void sig_handler(int s) { (void)s; running = 0; }
static Profile *active_profile(void) { return &cfg.profiles[cur_profile]; }

static void write_all_profiles(void) {
    printf("Writing all %d profiles to mouse flash...\n", cfg.num_profiles);
    for (int i = 0; i < cfg.num_profiles && i < 3; i++) {
        led_apply(&cfg.profiles[i], i);
        dpi_apply(&cfg.profiles[i], i);
        onboard_apply(&cfg.profiles[i], i);
    }
    printf("All profiles written.\n");
}

static void switch_profile(int idx) {
    if (idx == cur_profile) return;
    stop_all_macros();
    cur_profile = idx;
    printf("Profile: %s\n", cfg.profiles[idx].name);
    led_apply(&cfg.profiles[idx], idx);
    dpi_apply(&cfg.profiles[idx], idx);
    onboard_apply(&cfg.profiles[idx], idx);
}

static void switch_profile_by_abs(int abs_val) {
    /* Map hardware ABS_MISC values to profile indices */
    int idx = -1;
    if      (abs_val == 32)  idx = 0;
    else if (abs_val == 64)  idx = 1;
    else if (abs_val == 128) idx = 2;
    if (idx >= 0 && idx < cfg.num_profiles)
        switch_profile(idx);
}

/* ──────────────────────────────────────────────
   Event handlers
   ────────────────────────────────────────────── */
static void handle_kbd(struct input_event *ev) {
    if (ev->type != EV_KEY) return;
    static int lshift_down = 0, keyb_down = 0;
    if (ev->code == KEY_LEFTSHIFT) { lshift_down = (ev->value != 0); gshift = lshift_down && keyb_down; return; }
    if (ev->code == KEY_B)         { keyb_down   = (ev->value != 0); gshift = lshift_down && keyb_down; return; }
    for (int i = 0; i < NUM_GKEYS; i++) {
        if (ev->code != SRC_GKEYS[i]) continue;
        Binding *b = gshift ? &active_profile()->gkeys_gshift[i] : &active_profile()->gkeys_normal[i];
        int slot = gshift ? (NUM_GKEYS + i) : i;
        dispatch(b, ev->value, slot);
        return;
    }
}

static void handle_mouse(struct input_event *ev) {
    /* We don't grab fd_mouse so the kernel delivers events to the desktop
     * normally. We only need to read here to avoid the buffer filling up.
     * Don't re-emit anything — that would cause double events. */
    (void)ev;
}

static void handle_prof(struct input_event *ev) {
    if (ev->type == EV_ABS && ev->code == ABS_MISC && ev->value != 0)
        switch_profile_by_abs(ev->value);
}

/* ──────────────────────────────────────────────
   Main
   ────────────────────────────────────────────── */
/* ──────────────────────────────────────────────
   Device auto-discovery
   ────────────────────────────────────────────── */

/* Open an event device and check if it belongs to the G600 */
static int g600_check_device(const char *path, int *has_rel, int *has_abs, int *has_gkeys) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return 0;

    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) { close(fd); return 0; }
    if (strstr(name, "G600") == NULL) { close(fd); return 0; }

    struct input_id id;
    if (ioctl(fd, EVIOCGID, &id) < 0) { close(fd); return 0; }
    if (id.vendor != G600_VID || id.product != G600_PID) { close(fd); return 0; }

    /* Check event types */
    uint8_t evbits[EV_MAX/8+1] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    *has_rel = (evbits[EV_REL/8] >> (EV_REL%8)) & 1;
    *has_abs = (evbits[EV_ABS/8] >> (EV_ABS%8)) & 1;

    /* Check if it has G-key scancodes (KEY_1 through KEY_EQUAL) */
    uint8_t keybits[KEY_MAX/8+1] = {0};
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    /* G9-G20 map to KEY_1(2) through KEY_EQUAL(13) */
    int has_k1 = (keybits[KEY_1/8] >> (KEY_1%8)) & 1;
    int has_b  = (keybits[KEY_B/8] >> (KEY_B%8)) & 1; /* G-Shift sends KEY_B */
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

        printf("Found G600 device: %s (rel=%d abs=%d gkeys=%d)\n",
               path, has_rel, has_abs, has_gkeys);

        if (has_rel && !found_mouse)       { strncpy(mouse, path, bufsz-1); found_mouse=1; }
        else if (has_gkeys && !found_kbd)  { strncpy(kbd,   path, bufsz-1); found_kbd=1;   }
        else if (has_abs && !found_prof)   { strncpy(prof,  path, bufsz-1); found_prof=1;  }
    }

    return found_kbd && found_mouse && found_prof;
}

/* Find the hidraw interface 1 for the G600 (has profile feature reports) */
static int g600_find_hidraw(char *out, size_t bufsz) {
    /* Try stable symlink first */
    if (access(DEV_HIDRAW_SYMLINK, F_OK) == 0) {
        strncpy(out, DEV_HIDRAW_SYMLINK, bufsz-1);
        return 1;
    }
    /* Fall back: scan hidraw devices, pick interface 1 (if01) */
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/hidraw%d", i);
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        struct hidraw_devinfo info = {0};
        if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
            info.vendor == G600_VID && info.product == G600_PID) {
            /* Try to read profile 1 report — only interface 1 responds */
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

   char dev_kbd[64]={0}, dev_mouse[64]={0}, dev_prof[64]={0}, dev_hidraw[128]={0};    printf("Scanning for G600 devices...\n");
    if (!g600_find_devices(dev_kbd, dev_mouse, dev_prof, sizeof(dev_kbd))) {
        fprintf(stderr, "Could not find all G600 event devices\n");
        return 1;
    }
    if (!g600_find_hidraw(dev_hidraw, sizeof(dev_hidraw))) {
        fprintf(stderr, "Warning: Could not find G600 hidraw device (LED/DPI disabled)\n");
    }
    printf("KBD:    %s\n", dev_kbd);
    printf("MOUSE:  %s\n", dev_mouse);
    printf("PROF:   %s\n", dev_prof);
    printf("HIDRAW: %s\n", dev_hidraw[0] ? dev_hidraw : "(none)");

    int fd_kbd   = open(dev_kbd,   O_RDONLY | O_NONBLOCK); if (fd_kbd   < 0) { perror("open kbd");   return 1; }
    int fd_mouse = open(dev_mouse, O_RDONLY | O_NONBLOCK); if (fd_mouse < 0) { perror("open mouse"); return 1; }
    int fd_prof  = open(dev_prof,  O_RDONLY | O_NONBLOCK); if (fd_prof  < 0) { perror("open prof");  return 1; }

    hidraw_fd = dev_hidraw[0] ? open(dev_hidraw, O_RDWR) : -1;
    if (hidraw_fd < 0) perror("open hidraw (LED disabled)");

    if (uinput_setup() < 0) return 1;
    ioctl(fd_kbd, EVIOCGRAB, 1);

    int fd_ino = inotify_init1(IN_NONBLOCK);
    int wd = -1;
    if (fd_ino >= 0) wd = inotify_add_watch(fd_ino, cfg_path, IN_CLOSE_WRITE | IN_MODIFY);

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
            cur_profile = 0;
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
