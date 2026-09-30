/* The SDK's freestanding runtime for C plugins (see include/gsender/runtime.h). */

#include "gsender/plugin.h"
#include "gsender/runtime.h"

/* ------------------------------------------------------------------------- */
/* Memory and strings                                                        */
/* ------------------------------------------------------------------------- */

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (; n; --n, ++x, ++y) {
        if (*x != *y) return *x - *y;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (; n; --n, ++a, ++b) {
        if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Heap: first fit over a free list, growing linear memory as needed         */
/* ------------------------------------------------------------------------- */

typedef struct gs_block {
    size_t size; /* payload bytes */
    struct gs_block* next;
} gs_block_t;

#define GS_HEADER sizeof(gs_block_t)
#define GS_ALIGN(n) (((n) + 15u) & ~(size_t)15u)

extern unsigned char __heap_base;
static unsigned char* g_heap_top;
static gs_block_t* g_free_list;

static int gs_heap_reserve(size_t bytes) {
    if (!g_heap_top) g_heap_top = (unsigned char*)GS_ALIGN((size_t)&__heap_base);
    const size_t end = (size_t)__builtin_wasm_memory_size(0) * 65536u;
    const size_t need = (size_t)g_heap_top + bytes;
    if (need < (size_t)g_heap_top) return 0;
    if (need > end) {
        const size_t pages = (need - end + 65535u) / 65536u;
        if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return 0;
    }
    return 1;
}

void* malloc(size_t size) {
    if (size == 0) size = 1;
    size = GS_ALIGN(size);
    gs_block_t** link = &g_free_list;
    for (gs_block_t* b = g_free_list; b; link = &b->next, b = b->next) {
        if (b->size >= size) {
            if (b->size >= size + GS_HEADER + 64) {
                gs_block_t* rest = (gs_block_t*)((unsigned char*)b + GS_HEADER + size);
                rest->size = b->size - size - GS_HEADER;
                rest->next = b->next;
                *link = rest;
                b->size = size;
            } else {
                *link = b->next;
            }
            return (unsigned char*)b + GS_HEADER;
        }
    }
    if (size > (size_t)1 << 30 || !gs_heap_reserve(GS_HEADER + size)) return 0;
    gs_block_t* b = (gs_block_t*)g_heap_top;
    b->size = size;
    g_heap_top += GS_HEADER + size;
    return (unsigned char*)b + GS_HEADER;
}

void free(void* ptr) {
    if (!ptr) return;
    gs_block_t* b = (gs_block_t*)((unsigned char*)ptr - GS_HEADER);
    b->next = g_free_list;
    g_free_list = b;
}

void* calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size) return 0;
    void* p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    gs_block_t* b = (gs_block_t*)((unsigned char*)ptr - GS_HEADER);
    if (b->size >= size) return ptr;
    void* p = malloc(size);
    if (!p) return 0;
    memcpy(p, ptr, b->size);
    free(ptr);
    return p;
}

void* gsender_plugin_alloc(int32_t size) {
    return size < 0 ? 0 : malloc((size_t)size);
}

void gsender_plugin_free(void* ptr) {
    free(ptr);
}

/* ------------------------------------------------------------------------- */
/* Strings                                                                   */
/* ------------------------------------------------------------------------- */

void gs_str_init(gs_str_t* s) {
    s->data = 0;
    s->len = 0;
    s->cap = 0;
}

void gs_str_free(gs_str_t* s) {
    free(s->data);
    gs_str_init(s);
}

static void gs_str_reserve(gs_str_t* s, int32_t extra) {
    if (s->len + extra + 1 <= s->cap) return;
    int32_t cap = s->cap ? s->cap : 64;
    while (cap < s->len + extra + 1) cap *= 2;
    char* data = (char*)realloc(s->data, (size_t)cap);
    if (!data) return;
    s->data = data;
    s->cap = cap;
}

static void gs_str_append_n(gs_str_t* s, const char* text, int32_t n) {
    gs_str_reserve(s, n);
    if (s->len + n + 1 > s->cap) return;
    memcpy(s->data + s->len, text, (size_t)n);
    s->len += n;
    s->data[s->len] = 0;
}

void gs_str_append(gs_str_t* s, const char* text) {
    gs_str_append_n(s, text, (int32_t)strlen(text));
}

void gs_str_append_int(gs_str_t* s, int64_t value) {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    uint64_t v = value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
    do {
        buf[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    if (value < 0) buf[--i] = '-';
    gs_str_append(s, buf + i);
}

void gs_str_append_fixed(gs_str_t* s, double value, int decimals) {
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    if (value != value) {
        gs_str_append(s, "0");
        return;
    }
    int64_t scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    const double limit = 9.0e18 / (double)scale;
    if (value > limit) value = limit;
    if (value < -limit) value = -limit;
    const int negative = value < 0;
    const double scaled = (negative ? -value : value) * (double)scale + 0.5;
    const int64_t units = (int64_t)scaled;
    if (negative && units != 0) gs_str_append(s, "-");
    gs_str_append_int(s, units / scale);
    if (decimals > 0) {
        char frac[10];
        int64_t f = units % scale;
        for (int i = decimals - 1; i >= 0; --i) {
            frac[i] = (char)('0' + f % 10);
            f /= 10;
        }
        frac[decimals] = 0;
        gs_str_append(s, ".");
        gs_str_append(s, frac);
    }
}

void gs_str_append_json_string(gs_str_t* s, const char* text) {
    static const char hex[] = "0123456789abcdef";
    gs_str_append(s, "\"");
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        switch (*p) {
            case '"': gs_str_append(s, "\\\""); break;
            case '\\': gs_str_append(s, "\\\\"); break;
            case '\n': gs_str_append(s, "\\n"); break;
            case '\r': gs_str_append(s, "\\r"); break;
            case '\t': gs_str_append(s, "\\t"); break;
            default:
                if (*p < 0x20) {
                    char esc[7] = {'\\', 'u', '0', '0', hex[*p >> 4], hex[*p & 15], 0};
                    gs_str_append(s, esc);
                } else {
                    gs_str_append_n(s, (const char*)p, 1);
                }
        }
    }
    gs_str_append(s, "\"");
}

int32_t gs_respond(char* response_buf, int32_t buf_len, const char* json) {
    const int32_t len = (int32_t)strlen(json);
    if (len + 1 > buf_len) return -1;
    memcpy(response_buf, json, (size_t)len + 1);
    return len;
}

/* ------------------------------------------------------------------------- */
/* JSON reading                                                              */
/* ------------------------------------------------------------------------- */

static const char* gs_skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

static const char* gs_skip_string(const char* p) {
    ++p; /* opening quote */
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        ++p;
    }
    return *p ? p + 1 : p;
}

/* Skips one value; returns where it ends. */
static const char* gs_skip_value(const char* p) {
    p = gs_skip_ws(p);
    if (*p == '"') return gs_skip_string(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = gs_skip_string(p);
                continue;
            }
            if (*p == '{' || *p == '[') ++depth;
            if (*p == '}' || *p == ']') {
                if (--depth == 0) return p + 1;
            }
            ++p;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') ++p;
    return p;
}

/* The value of a top-level key, or 0. */
static const char* gs_json_find(const char* json, const char* key) {
    const size_t key_len = strlen(key);
    const char* p = gs_skip_ws(json);
    if (*p != '{') return 0;
    ++p;
    for (;;) {
        p = gs_skip_ws(p);
        if (*p != '"') return 0;
        const char* name = p + 1;
        const char* after = gs_skip_string(p);
        const int match = (size_t)(after - 1 - name) == key_len && strncmp(name, key, key_len) == 0;
        p = gs_skip_ws(after);
        if (*p != ':') return 0;
        p = gs_skip_ws(p + 1);
        if (match) return p;
        p = gs_skip_ws(gs_skip_value(p));
        if (*p != ',') return 0;
        ++p;
    }
}

bool gs_json_number(const char* json, const char* key, double* out) {
    const char* p = gs_json_find(json, key);
    if (!p) return false;
    int negative = 0;
    if (*p == '-') {
        negative = 1;
        ++p;
    }
    if (*p < '0' || *p > '9') return false;
    double v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    if (*p == '.') {
        double scale = 0.1;
        for (++p; *p >= '0' && *p <= '9'; ++p, scale *= 0.1) v += (*p - '0') * scale;
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        int eneg = 0;
        if (*p == '+' || *p == '-') eneg = *p++ == '-';
        int e = 0;
        while (*p >= '0' && *p <= '9' && e < 400) e = e * 10 + (*p++ - '0');
        while (e-- > 0) v = eneg ? v / 10 : v * 10;
    }
    *out = negative ? -v : v;
    return true;
}

bool gs_json_bool(const char* json, const char* key, bool* out) {
    const char* p = gs_json_find(json, key);
    if (!p) return false;
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

bool gs_json_string(const char* json, const char* key, char* out, int32_t out_len) {
    const char* p = gs_json_find(json, key);
    if (!p || *p != '"' || out_len <= 0) return false;
    int32_t n = 0;
    for (++p; *p && *p != '"'; ++p) {
        char c = *p;
        if (c == '\\' && p[1]) {
            ++p;
            switch (*p) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                default: c = *p; break; /* \" \\ \/; \u escapes are kept as the letter */
            }
        }
        if (n + 1 < out_len) out[n++] = c;
    }
    out[n] = 0;
    return true;
}
