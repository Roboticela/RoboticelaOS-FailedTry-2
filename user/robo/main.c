#include <robosys/syscall.h>

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void put(const char *s)
{
    unsigned n = slen(s);
    if (n)
        robo_write(1, s, n);
}

static void line(const char *s)
{
    put(s);
    put("\n");
}

static void put_u(unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    if (!v) {
        robo_write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        robo_write(1, &buf[i - 1], 1);
}

static int eq(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

static int driver_status(void)
{
    RoboDriverStatus st;
    robo_long r = robo_driver_query(&st);
    if ((uint64_t)r >= (uint64_t)-4095ll) {
        line("Driver Manager query failed.");
        return 1;
    }
    line("Driver Manager");
    put("PCI devices       : ");
    put_u(st.pci_count);
    put("\n");
    put("Drivers registered: ");
    put_u(st.drivers_registered);
    put("\n");
    put("Drivers loaded    : ");
    put_u(st.drivers_loaded);
    put("\n");
    put("Storage           : ");
    line(st.storage_ready ? "READY" : "NOT READY");
    put("Graphics          : ");
    put_u(st.gfx_width);
    put(" x ");
    put_u(st.gfx_height);
    put("\n");
    put("Keyboard          : ");
    line(st.keyboard ? "READY" : "NOT READY");
    put("Mouse             : ");
    line(st.mouse ? "READY" : "NOT READY");
    put("Signature rejects : ");
    put_u(st.rejected);
    put("\n");
    return 0;
}

static void usage(void)
{
    line("robo driver list");
    line("robo driver status");
    line("robo driver devices");
    line("robo driver verify <package>   (not connected: offline verify is kernel-side)");
    line("robo driver install <package>  (unsigned production packages are rejected)");
}

int main(void)
{
    usage();
    line("");
    return driver_status();
}
