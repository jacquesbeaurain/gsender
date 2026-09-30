typedef unsigned long size_t;
void* memcpy(void* d, const void* s, size_t n) { unsigned char* dd = d; const unsigned char* ss = s; while (n--) *dd++ = *ss++; return d; }
void* memset(void* d, int c, size_t n) { unsigned char* dd = d; while (n--) *dd++ = (unsigned char)c; return d; }
__attribute__((import_module("env"), import_name("host_log"))) void host_log(int v);
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static int sq(int x) { return x * x; }
static int cube(int x) { return x * x * x; }
typedef int (*fnp)(int);
fnp table[2] = { sq, cube };
volatile int sel = 1;
__attribute__((export_name("t_fib"))) int t_fib(int n) { return fib(n); }
__attribute__((export_name("t_indirect"))) int t_indirect(int i, int x) { return table[i & 1](x); }
__attribute__((export_name("t_float"))) double t_float(double a, float b) { return a * b + (double)(long long)(a / 3.0) + __builtin_sqrt(a); }
__attribute__((export_name("t_switch"))) int t_switch(int k) { switch (k) { case 0: return 10; case 1: return 20; case 2: return 30; case 5: return 60; default: return -1; } }
__attribute__((export_name("t_i64"))) long long t_i64(long long a, long long b) { return (a * b) / 7 + (a % 13) - (long long)((unsigned long long)b >> 3); }
struct S { int a[64]; double d; };
__attribute__((export_name("t_struct"))) int t_struct(int n) { struct S s; memset(&s, 0, sizeof s); for (int i = 0; i < 64; ++i) s.a[i] = i * n; struct S t = s; int sum = 0; for (int i = 0; i < 64; ++i) sum += t.a[i]; host_log(sum); return sum; }
__attribute__((export_name("t_loop"))) int t_loop(void) { volatile int x = 0; for (;;) { x++; } return x; }
static char buf[256];
__attribute__((export_name("t_str"))) int t_str(void) { const char* m = "hello wasm"; int i = 0; while (m[i]) { buf[i] = m[i] - 32 * (m[i] >= 'a' && m[i] <= 'z'); i++; } buf[i] = 0; return (int)(long)buf; }
__attribute__((export_name("t_conv"))) int t_conv(float f) { return (int)f + (unsigned)(f * 2) ; }
