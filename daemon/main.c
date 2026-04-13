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
#define DEV_KBD    "/dev/input/event4"
#define DEV_MOUSE  "/dev/input/event3"
#define DEV_PROF   "/dev/input/event5"
#define DEV_HIDRAW "/dev/hidraw1"

/* ── config ── */
#define CFG_DIR  "/.config/g600d"
#define CFG_FILE "/.config/g600d/g600d.conf"

/* ── limits ── */
#define MAX_PROFILES   8
#define NUM_GKEYS     13
#define NUM_MKEYS      5
#define MAX_MACRO_STEPS 64

/* ── HID ── */
#define G600_REPORT_SIZE 154
#define REPORT_ID_P0     0xF3
#define REPORT_ID_P1     0xF4
#define REPORT_ID_P2     0xF5
#define LED_SOLID   0
#define LED_BREATHE 1
#define LED_CYCLE   2
#define DPI_SHIFT 4

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
    {"BTN_LEFT",BTN_LEFT},{"BTN_RIGHT",BTN_RIGHT},{"BTN_MIDDLE",BTN_MIDDLE},{"BTN_SIDE",BTN_SIDE},{"BTN_EXTRA",BTN_EXTRA}, {"DPI_SHIFT", DPI_SHIFT},
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

/* ──────────────────────────────────────────────
   Profile
   ────────────────────────────────────────────── */
static const int SRC_GKEYS[NUM_GKEYS] = {
    KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,
    KEY_7,KEY_8,KEY_9,KEY_0,KEY_MINUS,KEY_EQUAL,
};
static const char *GKEY_NAMES[NUM_GKEYS] = {
    "G8", "G9","G10","G11","G12","G13","G14","G15","G16","G17","G18","G19","G20"
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
    FILE *f = fopen(path, "w"); if (!f) { perror("fopen config"); return; }
    fprintf(f,
        "# G600 Daemon Config\n"
        "# Simple rebind:  G9 = F13\n"
        "# Macro:          G10 = macro:<trigger>:<hold>: step, step, ...\n"
        "#   trigger: press | release\n"
        "#   hold:    once | repeat | toggle\n"
        "#   steps:   KEY, MOD+KEY, MOD+MOD+KEY, or Nms (delay)\n"
        "# Example:  G11 = macro:press:once: LEFTCTRL+C, 50ms, LEFTCTRL+V\n\n"
        "# LED: led_r/led_g/led_b = 0-255, led_effect = solid|breathe|cycle\n"
        "#      led_duration = 1-15 (seconds, breathe/cycle only)\n\n"
        "[default]\n"
        "abs_misc = 32\n\n"
        "# Uncomment and edit for additional hardware profiles\n"
        "#[profile2]\n"
        "#abs_misc = 64\n\n"
        "#[profile3]\n"
        "#abs_misc = 128\n\n"
        "G9  = F13\nG10 = F14\nG11 = F15\nG12 = F16\n"
        "G13 = F17\nG14 = F18\nG15 = F19\nG16 = F20\n"
        "G17 = F21\nG18 = F22\nG19 = F23\nG20 = F24\n\n"
        "SHIFT_G9  = F1\nSHIFT_G10 = F2\nSHIFT_G11 = F3\nSHIFT_G12 = F4\n"
        "SHIFT_G13 = F5\nSHIFT_G14 = F6\nSHIFT_G15 = F7\nSHIFT_G16 = F8\n"
        "SHIFT_G17 = F9\nSHIFT_G18 = F10\nSHIFT_G19 = F11\nSHIFT_G20 = F12\n\n"
        "BTN_LEFT   = BTN_LEFT\n"
        "BTN_RIGHT  = BTN_RIGHT\n"
        "MIDDLE     = BTN_MIDDLE\n"
        "TILT_LEFT  = BTN_SIDE\n"
        "TILT_RIGHT = BTN_EXTRA\n"
    );
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
    usetup.id.bustype = BUS_USB; usetup.id.vendor = 0x046d; usetup.id.product = 0xc24a;
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

/* ──────────────────────────────────────────────
   Globals
   ────────────────────────────────────────────── */
static volatile int running = 1;
static Config cfg;
static int cur_profile = 0;
static int gshift = 0;

static void sig_handler(int s) { (void)s; running = 0; }
static Profile *active_profile(void) { return &cfg.profiles[cur_profile]; }

static void switch_profile(int idx) {
    if (idx == cur_profile) return;
    stop_all_macros();
    cur_profile = idx;
    printf("Profile: %s\n", cfg.profiles[idx].name);
    led_apply(&cfg.profiles[idx], idx);
    dpi_apply(&cfg.profiles[idx], idx);
}

static void switch_profile_by_abs(int abs_val) {
    for (int i = 0; i < cfg.num_profiles; i++)
        if (cfg.profiles[i].abs_misc_val == abs_val) { switch_profile(i); return; }
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
    if (ev->type != EV_KEY) return;
    for (int i = 0; i < NUM_MKEYS; i++) {
        if (ev->code != SRC_MKEYS[i]) continue;
        dispatch(&active_profile()->mkeys[i], ev->value, NUM_GKEYS * 2 + i);
        return;
    }
    if (ev->code == BTN_LEFT || ev->code == BTN_RIGHT) {
        emit(EV_KEY, ev->code, ev->value); emit(EV_SYN, SYN_REPORT, 0);
    }
}

static void handle_prof(struct input_event *ev) {
    if (ev->type == EV_ABS && ev->code == ABS_MISC && ev->value != 0)
        switch_profile_by_abs(ev->value);
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

    int fd_kbd   = open(DEV_KBD,   O_RDONLY | O_NONBLOCK); if (fd_kbd   < 0) { perror("open kbd");   return 1; }
    int fd_mouse = open(DEV_MOUSE, O_RDONLY | O_NONBLOCK); if (fd_mouse < 0) { perror("open mouse"); return 1; }
    int fd_prof  = open(DEV_PROF,  O_RDONLY | O_NONBLOCK); if (fd_prof  < 0) { perror("open prof");  return 1; }

    hidraw_fd = open(DEV_HIDRAW, O_RDWR);
    if (hidraw_fd < 0) perror("open hidraw (LED disabled)");

    if (uinput_setup() < 0) return 1;
    ioctl(fd_kbd, EVIOCGRAB, 1);

    int fd_ino = inotify_init1(IN_NONBLOCK);
    int wd = -1;
    if (fd_ino >= 0) wd = inotify_add_watch(fd_ino, cfg_path, IN_CLOSE_WRITE | IN_MODIFY);

    led_apply(active_profile(), cur_profile);
    dpi_apply(active_profile(), cur_profile);
    printf("g600d running. Profile: %s\n", active_profile()->name);

    struct pollfd fds[4] = {
        {fd_kbd,   POLLIN, 0}, {fd_mouse, POLLIN, 0},
        {fd_prof,  POLLIN, 0}, {fd_ino,   POLLIN, 0},
    };

    struct input_event ev;
    while (running) {
        int r = poll(fds, 4, 500);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (fds[0].revents & POLLIN) while (read(fd_kbd,   &ev, sizeof(ev)) > 0) handle_kbd(&ev);
        if (fds[1].revents & POLLIN) while (read(fd_mouse, &ev, sizeof(ev)) > 0) handle_mouse(&ev);
        if (fds[2].revents & POLLIN) while (read(fd_prof,  &ev, sizeof(ev)) > 0) handle_prof(&ev);
        if (fds[3].revents & POLLIN) {
            char ibuf[512]; while (read(fd_ino, ibuf, sizeof(ibuf)) > 0);
            printf("Config changed, reloading...\n");
            stop_all_macros();
            config_load(&cfg, cfg_path);
            cur_profile = 0;
            led_apply(active_profile(), cur_profile);
    dpi_apply(active_profile(), cur_profile);
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
