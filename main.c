#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

#define JLINKARM_SO_NAME    "/opt/SEGGER/JLink/libjlinkarm.so"

/* libjlinkarm.so exports */
static int (* jlink_open) (void);
static int (* jlink_execcommand) (char *in, char *out, int size);
static int (* jlink_tif_select) (int);
static void (* jlink_setspeed) (long int speed);
static int (* jlink_connect) (void);
static unsigned (* jlink_getsn) (void);
static void (* jlink_emu_getproductname) (char *out, int size);
static int (*jlink_rtterminal_control) (int cmd, void *data);
static int (*jlink_rtterminal_read) (int cmd, char *buf, int size);

static int load_jlinkarm(void)
{
    void *so;

    so = dlopen(JLINKARM_SO_NAME, RTLD_LAZY);
    if (!so) {
        fprintf(stderr, "Failed to open jlinkarm (%s)\n", dlerror());
        return -1;
    }

    jlink_open = dlsym(so, "JLINK_Open");
    jlink_execcommand = dlsym(so, "JLINK_ExecCommand");
    jlink_tif_select = dlsym(so, "JLINK_TIF_Select");
    jlink_setspeed = dlsym(so, "JLINK_SetSpeed");
    jlink_connect = dlsym(so, "JLINK_Connect");
    jlink_getsn = dlsym(so, "JLINK_GetSN");
    jlink_emu_getproductname = dlsym(so, "JLINK_EMU_GetProductName");
    jlink_rtterminal_control = dlsym(so, "JLINK_RTTERMINAL_Control");
    jlink_rtterminal_read = dlsym(so, "JLINK_RTTERMINAL_Read");

    if (!jlink_open || !jlink_execcommand || !jlink_tif_select ||
            !jlink_setspeed || !jlink_connect || !jlink_getsn ||
            !jlink_emu_getproductname || !jlink_rtterminal_control ||
            !jlink_rtterminal_read) {
        fprintf(stderr, "Failed to initialize jlinkarm\n");
        return -1;
    }

    return 0;
}

static int connect_jlink(void)
{
    unsigned sn;
    char buf[1024];

    if (jlink_open()) {
        fprintf(stderr, "Failed to open J-Link\n");
        return -1;
    }

    if (jlink_execcommand("device=nrf52", NULL, 0)) {
        fprintf(stderr, "Failed to setup J-Link\n");
        return -1;
    }

    if (jlink_tif_select(1)) {
        fprintf(stderr, "Failed to setup J-Link\n");
        return -1;
    }

    jlink_setspeed(4000);

    if (jlink_connect()) {
        fprintf(stderr, "Failed to connect J-Link\n");
        return -1;
    }

    sn = jlink_getsn();
    jlink_emu_getproductname(buf, sizeof(buf));

    printf("Connected to:\n");
    printf("  %s\n", buf);
    printf("  S/N: %u\n", sn);

    return 0;
}

static int configure_rtt(void)
{
    struct {
        uint32_t address;
        uint32_t dummy[3];
    } rtt_start = { };
    struct {
        uint32_t index;
        uint32_t dummy;
        char name[32];
        uint32_t size;
        uint32_t flags;
    } rtt_info = { };

    int up_count;
    int idx;

    if (jlink_rtterminal_control(0, NULL)) {
        fprintf(stderr, "Failed to initialize RTT\n");
        return -1;
    }

    printf("Searching for RTT control block...\n");

    do {
        usleep(100);
        rtt_start.address = 0;
        up_count = jlink_rtterminal_control(3, &rtt_start);
    } while (up_count < 0);

    printf("Found %d up-buffers.\n", up_count);

    for (idx = 0; idx < up_count; idx++) {
        int rc;

        memset(&rtt_info, 0, sizeof(rtt_info));
        rtt_info.index = idx;

        rc = jlink_rtterminal_control(2, &rtt_info);
        if (rc) {
            fprintf(stderr, "Failed to get information for up-buffer #%d\n", up_count);
            continue;
        }

        if (rtt_info.size > 0 && !strcmp("ble_monitor", rtt_info.name)) {
            break;
        }
    }

    if (idx == up_count) {
        fprintf(stderr, "Failed to find proper up-buffer\n");
        return -1;
    }

    printf("Using buffer #%d (size=%d)\n", idx, rtt_info.size);

    return idx;
}

static int open_pty(void)
{
    int fdm;

    fdm = posix_openpt(O_WRONLY);
    if (!fdm) {
        perror("Failed to open pty");
        return -1;
    }

    if (grantpt(fdm) < 0) {
        perror("Failed to configure pty");
        return -1;
    }

    if (unlockpt(fdm) < 0) {
        perror("Failed to unlock pty");
        return -1;
    }

    printf("PTY name is %s\n", ptsname(fdm));

    return fdm;
}

int main(int argc, char **argv)
{
    int pfd;
    int idx;

    if (load_jlinkarm() < 0) {
        return EXIT_FAILURE;
    }

    if (connect_jlink() < 0) {
        return EXIT_FAILURE;
    }

    idx = configure_rtt();
    if (idx < 0) {
        return EXIT_FAILURE;
    }

    pfd = open_pty();
    if (pfd < 0) {
        return EXIT_FAILURE;
    }

    while (1) {
        char buf[4096];
        int num;

        num = jlink_rtterminal_read(1, buf, sizeof(buf));
        if (num > 0) {
            write(pfd, buf, num);
        } else {
            usleep(100);
        }
    }

    return EXIT_SUCCESS;
}
