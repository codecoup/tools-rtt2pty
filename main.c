/*
 * rtt2pty - RTT-PTY bridge
 *
 * Copyright (C) 2017  Codecoup
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <getopt.h>
#include <termios.h>
#include <sys/select.h>
#include <signal.h>

#define RTT_CONTROL_START           0
#define RTT_CONTROL_STOP            1
#define RTT_CONTROL_GET_DESC        2
#define RTT_CONTROL_GET_NUM_BUF     3
#define RTT_CONTROL_GET_STAT        4

#define RTT_DIRECTION_UP            0
#define RTT_DIRECTION_DOWN          1

struct rtt_desc {
    uint32_t index;
    uint32_t direction;
    char name[32];
    uint32_t size;
    uint32_t flags;
};

/* libjlinkarm.so exports */
static int (* jlink_emu_selectbyusbsn) (unsigned sn);
static int (* jlink_open) (void);
static int (* jlink_execcommand) (char *in, char *out, int size);
static int (* jlink_tif_select) (int);
static void (* jlink_setspeed) (long int speed);
static int (* jlink_connect) (void);
static unsigned (* jlink_getsn) (void);
static void (* jlink_emu_getproductname) (char *out, int size);
static int (*jlink_rtterminal_control) (int cmd, void *data);
static int (*jlink_rtterminal_read) (int cmd, char *buf, int size);
static int (*jlink_rtterminal_write) (int cmd, char *buf, int size);

static const struct option options[] = {
    { "device",   required_argument, NULL, 'd' },
    { "if",       required_argument, NULL, 'i' },
    { "sn",       required_argument, NULL, 's' },
    { "speed",    required_argument, NULL, 'S' },
    { "buffer",   required_argument, NULL, 'b' },
    { "bidir",    no_argument,       NULL, '2' },
    { "jlinkarm", required_argument, NULL, 'j' },
    { "help",     no_argument,       NULL, 'h' },
    { }
};

static unsigned opt_sn = 0;
static const char *opt_address = NULL;
static const char *opt_device = "nrf52";
static int opt_if = 1; // SWD
static unsigned opt_speed = 4000;
static const char *opt_buffer = "Terminal";
static int opt_bidir = 0;
static const char *opt_jlinkarm = NULL;
static int opt_help = 0;

static void *try_dlopen_jlinkarm(void)
{
    static const char *dir[] = {
            "/usr/bin",
            "/opt/SEGGER/JLink",
    };
    static const char *file[] = {
            "libjlinkarm.so",
            "libjlinkarm.so.6",
    };
    char fname[100];
    void *so = NULL;
    int i, j;

    for (i = 0; i < sizeof(dir) / sizeof(dir[0]); i++) {
        for (j = 0; j < sizeof(file) / sizeof(file[0]); j++) {
            snprintf(fname, sizeof(fname), "%s/%s", dir[i], file[j]);
            fname[sizeof(fname) - 1] = '\0';

            so = dlopen(fname, RTLD_LAZY);
            if (so) {
                printf("Using jlinkarm found at %s\n", fname);
                break;
            }
        }
    }

    return so;
}

static int load_jlinkarm(void)
{
    void *so;

    if (opt_jlinkarm) {
        so = dlopen(opt_jlinkarm, RTLD_LAZY);
    } else {
        so = try_dlopen_jlinkarm();
    }

    if (!so) {
        fprintf(stderr, "Failed to open jlinkarm (%s)\n", dlerror());
        return -1;
    }

    jlink_emu_selectbyusbsn = dlsym(so, "JLINK_EMU_SelectByUSBSN");
    jlink_open = dlsym(so, "JLINK_Open");
    jlink_execcommand = dlsym(so, "JLINK_ExecCommand");
    jlink_tif_select = dlsym(so, "JLINK_TIF_Select");
    jlink_setspeed = dlsym(so, "JLINK_SetSpeed");
    jlink_connect = dlsym(so, "JLINK_Connect");
    jlink_getsn = dlsym(so, "JLINK_GetSN");
    jlink_emu_getproductname = dlsym(so, "JLINK_EMU_GetProductName");
    jlink_rtterminal_control = dlsym(so, "JLINK_RTTERMINAL_Control");
    jlink_rtterminal_read = dlsym(so, "JLINK_RTTERMINAL_Read");
    jlink_rtterminal_write = dlsym(so, "JLINK_RTTERMINAL_Write");

    if (!jlink_emu_selectbyusbsn || !jlink_open || !jlink_execcommand ||
            !jlink_tif_select || !jlink_setspeed || !jlink_connect ||
            !jlink_getsn || !jlink_emu_getproductname ||
            !jlink_rtterminal_control || !jlink_rtterminal_read ||
            !jlink_rtterminal_write) {
        fprintf(stderr, "Failed to initialize jlinkarm\n");
        return -1;
    }

    return 0;
}

static int connect_jlink(void)
{
    unsigned sn;
    char buf[1024];

    if (opt_sn) {
        if (jlink_emu_selectbyusbsn(opt_sn) < 0) {
            fprintf(stderr, "Failed to select emu\n");
            return -1;
        }
    }

    if (jlink_open()) {
        fprintf(stderr, "Failed to open J-Link\n");
        return -1;
    }

    snprintf(buf, sizeof(buf), "device=%s", opt_device);
    if (jlink_execcommand(buf, NULL, 0)) {
        fprintf(stderr, "Failed to setup J-Link\n");
        return -1;
    }

    if (jlink_tif_select(opt_if)) {
        fprintf(stderr, "Failed to setup J-Link\n");
        return -1;
    }

    jlink_setspeed(opt_speed);

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

static int find_buffer(const char *name, int direction, struct rtt_desc *desc)
{
    int count;
    int index;

    do {
        usleep(100);
        count = jlink_rtterminal_control(RTT_CONTROL_GET_NUM_BUF, &direction);
    } while (count < 0);

    for (index = 0; index < count; index++) {
        int rc;

        memset(desc, 0, sizeof(*desc));
        desc->index = index;
        desc->direction = direction;

        rc = jlink_rtterminal_control(RTT_CONTROL_GET_DESC, desc);
        if (rc) {
            continue;
        }

        if (desc->size > 0 && !strcmp(name, desc->name)) {
            break;
        }
    }

    if (index == count) {
        return -1;
    }

    return index;
}

static int configure_rtt(int *index_up, int *index_down)
{
    int index;
    struct rtt_desc desc;
    char *range_size;
    char cmd[128];

    if (opt_address && strlen(opt_address)) {
        range_size = strchr(opt_address, ',');
        if (range_size) {
            *range_size++ = '\0';
            if (strlen(range_size) == 0) {
                range_size = "0x1000";
            }
            snprintf(cmd, sizeof(cmd), "SetRTTSearchRanges %s %s", opt_address, range_size);
        } else {
            snprintf(cmd, sizeof(cmd), "SetRTTAddr %s", opt_address);
        }

        cmd[sizeof(cmd) - 1] = '\0';

        if (jlink_execcommand(cmd, NULL, 0)) {
            fprintf(stderr, "Warning: failed to set RTT control block search range\n");
            return -1;
        }
    }

    if (jlink_rtterminal_control(RTT_CONTROL_START, NULL)) {
        fprintf(stderr, "Failed to initialize RTT\n");
        return -1;
    }

    printf("Searching for RTT control block...\n");

    index = find_buffer(opt_buffer, RTT_DIRECTION_UP, &desc);
    if (index < 0) {
        fprintf(stderr, "Failed to find matching up-buffer\n");
        return -1;
    } else {
        printf("Using up-buffer #%d (size=%d)\n", index, desc.size);
        *index_up = index;
    }

    if (!opt_bidir) {
        return 0;
    }

    index = find_buffer(opt_buffer, RTT_DIRECTION_DOWN, &desc);
    if (index < 0) {
        fprintf(stderr, "Failed to find matching down-buffer\n");
        return -1;
    } else {
        printf("Using down-buffer #%d (size=%d)\n", index, desc.size);
        *index_down = index;
    }

    return 0;
}

static int open_pty(void)
{
    struct termios tio;
    int fdm;

    if (opt_bidir) {
        fdm = posix_openpt(O_RDWR);
    } else {
        fdm = posix_openpt(O_WRONLY);
    }
    if (!fdm) {
        perror("Failed to open pty");
        return -1;
    }

    if (tcgetattr(fdm, &tio) == 0) {
        tio.c_lflag &= ~ECHO;
        tcsetattr(fdm, TCSAFLUSH, &tio);
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

static bool do_exit = false;

static void sig_handler(int signum)
{
    do_exit = true;
}

int main(int argc, char **argv)
{
    int pfd;
    int index_up;
    int index_down;
    int ret;

    for (;;) {
         int opt;

         opt = getopt_long(argc, argv, "a:d:i:s:S:b:2j:h:", options, NULL);
         if (opt < 0)
             break;

         switch (opt) {
         case 'a':
             opt_address = optarg;
             break;
         case 'd':
             opt_device = optarg;
             break;
         case 'i':
             // TODO: not yet supported...
             break;
         case 's':
             opt_sn = atoi(optarg);
             break;
         case 'S':
             opt_speed = atoi(optarg);
             break;
         case 'b':
             opt_buffer = optarg;
             break;
         case '2':
             opt_bidir = 1;
             break;
         case 'j':
             opt_jlinkarm = optarg;
             break;
         case 'h':
             opt_help = 1;
             break;
         default:
             return EXIT_FAILURE;
         }
    }

    if (opt_help) {
        printf("rtt2pty: Segger RTT to PTY bridge\n");
        printf("rtt2pty [-opt <param>]\n");
        printf("\nOptions:\n");
        printf("\t-d <devname>      Segger device name\n");
        printf("\t-s <serial>       J-Link serial number\n");
        printf("\t-S <speed>        SWD/JTAG speed\n");
        printf("\t-b <name>         Buffer name\n");
        printf("\t-2                Enable bi-directional comms\n");
        printf("\t-j <filename>     libjlinkarm.so/dylib location\n");
        return EXIT_SUCCESS;
    }

    if (load_jlinkarm() < 0) {
        return EXIT_FAILURE;
    }

    if (connect_jlink() < 0) {
        return EXIT_FAILURE;
    }

    ret = configure_rtt(&index_up, &index_down);
    if (ret < 0) {
        return EXIT_FAILURE;
    }

    pfd = open_pty();
    if (pfd < 0) {
        return EXIT_FAILURE;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGQUIT, sig_handler);

    while (!do_exit) {
        char buf[4096];

        ret = jlink_rtterminal_read(index_up, buf, sizeof(buf));
        if (ret > 0) {
            write(pfd, buf, ret);
        } else if (opt_bidir) {
            struct timeval tmo;
            fd_set rfds;

            tmo.tv_sec = 0;
            tmo.tv_usec = 100;

            FD_ZERO(&rfds);
            FD_SET(pfd, &rfds);

            ret = select(pfd + 1, &rfds, NULL, NULL, &tmo);

            if (ret > 0 && FD_ISSET(pfd, &rfds)) {
                ret = read(pfd, buf, sizeof(buf));
                if (ret > 0) {
                    jlink_rtterminal_write(index_down, buf, ret);
                }
            }
        } else {
            usleep(100);
        }
    }

    return EXIT_SUCCESS;
}
