/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <cutils/uevent.h>
#include <errno.h>
#include <sys/poll.h>
#include <pthread.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOG_TAG "PowerHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define PLATFORM_SLEEP_MODES 2
#define XO_VOTERS 3
#define VMIN_VOTERS 0

#define RPM_PARAMETERS 4
#define NUM_PARAMETERS 10

#ifndef RPM_STAT
#define RPM_STAT "/d/rpm_stats"
#endif

#ifndef RPM_MASTER_STAT
#define RPM_MASTER_STAT "/d/rpm_master_stats"
#endif

/* RPM runs at 19.2Mhz. Divide by 19200 for msec */
#define RPM_CLK 19200

const char *parameter_names[] = {
    "vlow_count",
    "accumulated_vlow_time",
    "vmin_count",
    "accumulated_vmin_time",
    "xo_accumulated_duration",
    "xo_count",
    "xo_accumulated_duration",
    "xo_count",
    "xo_accumulated_duration",
    "xo_count"};

#define STATE_ON "state=1"
#define STATE_OFF "state=0"
#define MAX_LENGTH         50
#define BOOST_SOCKET       "/dev/socket/mpdecision/pb"
#define WAKE_GESTURE_PATH "/sys/bus/i2c/devices/1-004a/tsp"
static int client_sockfd;
static struct sockaddr_un client_addr;
static int last_state = -1;

enum {
    PROFILE_POWER_SAVE = 0,
    PROFILE_BALANCED,
    PROFILE_HIGH_PERFORMANCE
};

static int current_power_profile = PROFILE_BALANCED;

static void socket_init()
{
    if (!client_sockfd) {
        client_sockfd = socket(PF_UNIX, SOCK_DGRAM, 0);
        if (client_sockfd < 0) {
            ALOGE("%s: failed to open: %s", __func__, strerror(errno));
            return;
        }
        memset(&client_addr, 0, sizeof(struct sockaddr_un));
        client_addr.sun_family = AF_UNIX;
        snprintf(client_addr.sun_path, UNIX_PATH_MAX, BOOST_SOCKET);
    }
}

static void power_init(__attribute__((unused)) struct power_module *module)
{
    ALOGI("%s", __func__);
    socket_init();
}

static void coresonline(int off)
{
    int rc;
    pid_t client;
    char data[MAX_LENGTH];

    if (client_sockfd < 0) {
        ALOGE("%s: boost socket not created", __func__);
        return;
    }

    client = getpid();

    if (!off) {
        snprintf(data, MAX_LENGTH, "8:%d", client);
        rc = sendto(client_sockfd, data, strlen(data), 0, (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
    } else {
        snprintf(data, MAX_LENGTH, "7:%d", client);
        rc = sendto(client_sockfd, data, strlen(data), 0, (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
    }

    if (rc < 0) {
        ALOGE("%s: failed to send: %s", __func__, strerror(errno));
    }
}

static void enc_boost(int off)
{
    int rc;
    pid_t client;
    char data[MAX_LENGTH];

    if (client_sockfd < 0) {
        ALOGE("%s: boost socket not created", __func__);
        return;
    }

    client = getpid();

    if (!off) {
        snprintf(data, MAX_LENGTH, "5:%d", client);
        rc = sendto(client_sockfd, data, strlen(data), 0,
            (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
    } else {
        snprintf(data, MAX_LENGTH, "6:%d", client);
        rc = sendto(client_sockfd, data, strlen(data), 0,
            (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
    }

    if (rc < 0) {
        ALOGE("%s: failed to send: %s", __func__, strerror(errno));
    }
}

static void process_video_encode_hint(void *metadata)
{

    socket_init();

    if (client_sockfd < 0) {
        ALOGE("%s: boost socket not created", __func__);
        return;
    }

    if (!metadata)
        return;

    if (!strncmp(metadata, STATE_ON, sizeof(STATE_ON))) {
        /* Video encode started */
        enc_boost(1);
    } else if (!strncmp(metadata, STATE_OFF, sizeof(STATE_OFF))) {
        /* Video encode stopped */
        enc_boost(0);
    }
}


static void touch_boost()
{
    int rc, fd;
    pid_t client;
    char data[MAX_LENGTH];
    char buf[MAX_LENGTH];

    if (client_sockfd < 0) {
        ALOGE("%s: boost socket not created", __func__);
        return;
    }

    client = getpid();

    snprintf(data, MAX_LENGTH, "1:%d", client);
    rc = sendto(client_sockfd, data, strlen(data), 0,
        (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
    if (rc < 0) {
        ALOGE("%s: failed to send: %s", __func__, strerror(errno));
    }
}

static void low_power(int on)
{
    int rc;
    pid_t client;
    char data[MAX_LENGTH];

    if (client_sockfd < 0) {
        ALOGE("%s: boost socket not created", __func__);
        return;
    }

    client = getpid();

    if (on) {
        snprintf(data, MAX_LENGTH, "10:%d", client);
        rc = sendto(client_sockfd, data, strlen(data), 0, (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
        if (rc < 0) {
            ALOGE("%s: failed to send: %s", __func__, strerror(errno));
        }
    } else {
        snprintf(data, MAX_LENGTH, "9:%d", client);
        rc = sendto(client_sockfd, data, strlen(data), 0, (const struct sockaddr *)&client_addr, sizeof(struct sockaddr_un));
        if (rc < 0) {
            ALOGE("%s: failed to send: %s", __func__, strerror(errno));
        }
    }
}

static void power_set_interactive(__attribute__((unused)) struct power_module *module, int on)
{
    if (current_power_profile != PROFILE_BALANCED)
        return;

    if (last_state == on)
        return;

    last_state = on;

    ALOGV("%s %s", __func__, (on ? "ON" : "OFF"));
    if (on) {
        coresonline(0);
        touch_boost();
    } else {
        coresonline(1);
    }
}

static void set_power_profile(int profile) {

    if (profile == current_power_profile)
        return;

    ALOGV("%s: profile=%d", __func__, profile);

    if (profile == PROFILE_BALANCED) {
        low_power(0);
        coresonline(1);
        enc_boost(0);
        ALOGD("%s: set balanced mode", __func__);
    } else if (profile == PROFILE_HIGH_PERFORMANCE) {
        low_power(0);
        coresonline(1);
        enc_boost(1);
        ALOGD("%s: set performance mode", __func__);

    } else if (profile == PROFILE_POWER_SAVE) {
        coresonline(0);
        enc_boost(0);
        low_power(1);
        ALOGD("%s: set powersave", __func__);
    }

    current_power_profile = profile;
}

static void sysfs_write(char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s\n", path, buf);
        return;
    }

    len = write(fd, s, strlen(s));
    if (len < 0) {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s\n", path, buf);
    }

    close(fd);
}

static void set_feature(struct power_module *module __unused, feature_t feature, int state)
{
    switch (feature) {
    case POWER_FEATURE_DOUBLE_TAP_TO_WAKE:
        sysfs_write(WAKE_GESTURE_PATH, state ? "AUTO" : "OFF");
        break;
    default:
        ALOGW("Error setting the feature, it doesn't exist %d\n", feature);
        break;
    }
}

int get_feature(struct power_module *module __unused, feature_t feature)
{
    if (feature == POWER_FEATURE_SUPPORTED_PROFILES) {
        return 3;
    }
    return -1;
}

static void power_hint( __attribute__((unused)) struct power_module *module,
                        __attribute__((unused)) power_hint_t hint,
                        __attribute__((unused)) void *data)
{
    switch (hint) {
        case POWER_HINT_INTERACTION:
        case POWER_HINT_LAUNCH:
            if (current_power_profile == PROFILE_POWER_SAVE)
                return;
            ALOGV("POWER_HINT_INTERACTION");
            touch_boost();
            break;
        case POWER_HINT_VIDEO_ENCODE:
            if (current_power_profile != PROFILE_BALANCED)
                return;
            process_video_encode_hint(data);
            break;
        case POWER_HINT_SET_PROFILE:
            set_power_profile(*(int32_t *)data);
            break;
        case POWER_HINT_LOW_POWER:
            if (data)
                set_power_profile(PROFILE_POWER_SAVE);
            else
                set_power_profile(PROFILE_BALANCED);
            break;
        default:
            break;
    }
}

static ssize_t get_number_of_platform_modes(struct power_module *module) {
   return PLATFORM_SLEEP_MODES;
}

static int get_voter_list(struct power_module *module, size_t *voter) {
   voter[0] = XO_VOTERS;
   voter[1] = VMIN_VOTERS;

   return 0;
}

static int extract_stats(uint64_t *list, char *file,
    unsigned int num_parameters, unsigned int index) {
    FILE *fp;
    ssize_t read;
    size_t len;
    char *line;
    int ret;

    fp = fopen(file, "r");
    if (fp == NULL) {
        ret = -errno;
        ALOGE("%s: failed to open: %s", __func__, strerror(errno));
        return ret;
    }

    for (line = NULL, len = 0;
         ((read = getline(&line, &len, fp) != -1) && (index < num_parameters));
         free(line), line = NULL, len = 0) {
        uint64_t value;
        char* offset;

        size_t begin = strspn(line, " \t");
        if (strncmp(line + begin, parameter_names[index], strlen(parameter_names[index]))) {
            continue;
        }

        offset = memchr(line, ':', len);
        if (!offset) {
            continue;
        }

        if (!strcmp(file, RPM_MASTER_STAT)) {
            /* RPM_MASTER_STAT is reported in hex */
            sscanf(offset, ":%" SCNx64, &value);
            /* Duration is reported in rpm SLEEP TICKS */
            if (!strcmp(parameter_names[index], "xo_accumulated_duration")) {
                value /= RPM_CLK;
            }
        } else {
            /* RPM_STAT is reported in decimal */
            sscanf(offset, ":%" SCNu64, &value);
        }
        list[index] = value;
        index++;
    }
    free(line);

    fclose(fp);
    return 0;
}

static int get_platform_low_power_stats(struct power_module *module,
    power_state_platform_sleep_state_t *list) {
    uint64_t stats[sizeof(parameter_names)] = {0};
    int ret;

    if (!list) {
        return -EINVAL;
    }

    ret = extract_stats(stats, RPM_STAT, RPM_PARAMETERS, 0);

    if (ret) {
        return ret;
    }

    ret = extract_stats(stats, RPM_MASTER_STAT, NUM_PARAMETERS, 4);

    if (ret) {
        return ret;
    }

    /* Update statistics for XO_shutdown */
    strcpy(list[0].name, "XO_shutdown");
    list[0].total_transitions = stats[0];
    list[0].residency_in_msec_since_boot = stats[1];
    list[0].supported_only_in_suspend = false;
    list[0].number_of_voters = XO_VOTERS;

    /* Update statistics for APSS voter */
    strcpy(list[0].voters[0].name, "APSS");
    list[0].voters[0].total_time_in_msec_voted_for_since_boot = stats[4];
    list[0].voters[0].total_number_of_times_voted_since_boot = stats[5];

    /* Update statistics for MPSS voter */
    strcpy(list[0].voters[1].name, "MPSS");
    list[0].voters[1].total_time_in_msec_voted_for_since_boot = stats[6];
    list[0].voters[1].total_number_of_times_voted_since_boot = stats[7];

    /* Update statistics for LPASS voter */
    strcpy(list[0].voters[2].name, "LPASS");
    list[0].voters[2].total_time_in_msec_voted_for_since_boot = stats[8];
    list[0].voters[2].total_number_of_times_voted_since_boot = stats[9];

    /* Update statistics for VMIN state */
    strcpy(list[1].name, "VMIN");
    list[1].total_transitions = stats[2];
    list[1].residency_in_msec_since_boot = stats[3];
    list[1].supported_only_in_suspend = false;
    list[1].number_of_voters = VMIN_VOTERS;

    return 0;
}

static int power_open(const hw_module_t* module, const char* name,
                    hw_device_t** device)
{
    ALOGD("%s: enter; name=%s", __FUNCTION__, name);
    int retval = 0; /* 0 is ok; -1 is error */
    if (strcmp(name, POWER_HARDWARE_MODULE_ID) == 0) {
        power_module_t *dev = (power_module_t *)calloc(1,
                sizeof(power_module_t));
        if (dev) {
            /* Common hw_device_t fields */
            dev->common.tag = HARDWARE_DEVICE_TAG;
            dev->common.module_api_version = POWER_MODULE_API_VERSION_0_5;
            dev->common.hal_api_version = HARDWARE_HAL_API_VERSION;
            dev->init = power_init;
            dev->powerHint = power_hint;
            dev->setInteractive = power_set_interactive;
            dev->get_number_of_platform_modes = get_number_of_platform_modes;
            dev->get_platform_low_power_stats = get_platform_low_power_stats;
            dev->get_voter_list = get_voter_list;
            *device = (hw_device_t*)dev;
        } else
            retval = -ENOMEM;
    } else {
        retval = -EINVAL;
    }
    ALOGD("%s: exit %d", __FUNCTION__, retval);
    return retval;
}

static struct hw_module_methods_t power_module_methods = {
    .open = power_open,
};

struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_5,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "Shamu Power HAL",
        .author = "The Android Open Source Project",
        .methods = &power_module_methods,
    },

    .init = power_init,
    .setInteractive = power_set_interactive,
    .powerHint = power_hint,
    .setFeature = set_feature,
    .getFeature = get_feature,
    .get_number_of_platform_modes = get_number_of_platform_modes,
    .get_platform_low_power_stats = get_platform_low_power_stats,
    .get_voter_list = get_voter_list
};
