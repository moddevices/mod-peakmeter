// ----------------------------------------------------------------------------
//
//  Copyright (C) 2016 Filipe Coelho <falktx@falktx.com>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// ----------------------------------------------------------------------------

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <syscall.h>
#include <linux/futex.h>

#if !defined(SYS_futex) && defined(SYS_futex_time64)
#define SYS_futex SYS_futex_time64
#endif

extern "C" {
#include "i2c-dev.h"
}

#include "jacktools/jkmeter.h"

// --------------------------------------------------------------------------------------------------------------------

/* Register Addresses */
#define PCA9685_MODE1         0x00
#define PCA9685_MODE2         0x01
#define PCA9685_LED0_ON_L     0x06
#define PCA9685_LED0_ON_H     0x07
#define PCA9685_LED0_OFF_L    0x08
#define PCA9685_LED0_OFF_H    0x09
#define PCA9685_ALL_LED_ON_L  0xFA
#define PCA9685_ALL_LED_ON_H  0xFB
#define PCA9685_ALL_LED_OFF_L 0xFC
#define PCA9685_ALL_LED_OFF_H 0xFD
#define PCA9685_PRESCALE      0xFE

/* MODE1 Register */
#define PCA9685_ALLCALL 0x01
#define PCA9685_SLEEP   0x10
#define PCA9685_RESTART 0x80

/* MODE2 Register */
#define PCA9685_OUTDRV  0x04
#define PCA9685_INVRT   0x10

/* Custom MOD */
#define PCA9685_ADDR 0x41

// Brightness values
#define MIN_BRIGHTNESS_GREEN 120
#define MIN_BRIGHTNESS_RED   120
#define MAX_BRIGHTNESS_RED   1024

#define MIN_BRIGHTNESS_GREEN_f 120.f
#define MIN_BRIGHTNESS_RED_f   120.f

// macro for mapping audio value to LED brightness
#define MAP(x, Imin, Imax, Omin, Omax)      ((x - Imin) * (Omax -  Omin) / (Imax - Imin) + Omin)

// weighing factor
#define FILTER_WEIGHING_FACTOR 0.1f

// --------------------------------------------------------------------------------------------------------------------

typedef struct {
    int sem;
    int shm1, shm2;
    int padding;
    float peaks[4];
} Container;

// --------------------------------------------------------------------------------------------------------------------

// Do not change these enums! They match how the hardware works.
// Unless you're changing hardware, leave these alone.
enum LED_ID {
#ifndef _MOD_DEVICE_DWARF
    // normal
    kLedOut2,
    kLedOut1,
    kLedIn1,
    kLedIn2
#else
    // inverted for dwarf
    kLedOut1,
    kLedOut2,
    kLedIn2,
    kLedIn1
#endif
};

enum LED_Color {
    kLedColorBlue,
    kLedColorRed,
    kLedColorGreen,
    kLedColorOff
};

bool all_leds_off(const int bus)
{
    return (i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_ON_L,  0) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_ON_H,  0) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_OFF_L, 0) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_OFF_H, 0) >= 0);
}

bool set_led_color(const int bus, LED_ID led_id, LED_Color led_color, uint16_t value)
{
    const uint8_t channel = (led_id*4+led_color)*4;

    return (i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_L + channel, value & 0xFF) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_H + channel, value >> 8)   >= 0);
}

// --------------------------------------------------------------------------------------------------------------------
// Global variables

static int           g_bus       = -1;
static volatile bool g_running   = false;
static pthread_t     g_thread    = -1;
static Container*    g_container = nullptr;

// --------------------------------------------------------------------------------------------------------------------
// Peak Meter thread

static void* peakmeter_run(void* arg)
{
    if (arg == nullptr || ! g_running)
        return nullptr;

    const bool using_container = g_container != nullptr;
    jack_client_t* const client = (jack_client_t*)arg;

    float pks[4];
    Jkmeter meter(client, 4, using_container ? g_container->peaks : pks);

    {
        // connect monitor ports
        char ourportname[255];
        const char* const ourclientname = jack_get_client_name(client);

        sprintf(ourportname, "%s:in_1", ourclientname);
        jack_connect(client, "system:capture_1", ourportname);

        sprintf(ourportname, "%s:in_2", ourclientname);
        jack_connect(client, "system:capture_2", ourportname);

        if (jack_port_by_name(client, "mod-monitor:out_1") != nullptr)
        {
            sprintf(ourportname, "%s:in_3", ourclientname);
            jack_connect(client, "mod-monitor:out_1", ourportname);

            sprintf(ourportname, "%s:in_4", ourclientname);
            jack_connect(client, "mod-monitor:out_2", ourportname);
        }
    }

    if (Container* const container = g_container)
    {
        meter.setup_post(&g_container->sem);

        while (meter.get_state() == Jkmeter::PROCESS && g_running)
            usleep(100*1000);
        return nullptr;
    }

    if (g_bus == -1)
        return nullptr;

    LED_ID colorIdMap[4] = {
        kLedIn1,
        kLedIn2,
        kLedOut1,
        kLedOut2,
    };

    float value;
    uint8_t clip, clipping[4] = { 0, 0, 0, 0 };
    uint16_t cval;

    uint16_t ledsCache[4][3] = {
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}
    };

    float filtered_value[4] = {
        0.0f, 0.0f, 0.0f, 0.0f
    };

    #define set_led_color_cache(col, val)                  \
        if (ledsCache[i][col] != val) {                    \
            ledsCache[i][col] = val;                       \
            set_led_color(g_bus, colorIdMap[i], col, val); \
        }

    while (meter.get_levels() == Jkmeter::PROCESS && g_running)
    {
        for (int i=0; i<4; ++i)
        {
            value = pks[i];

            if (value > 0.988f) // clipping
            {
                clip = ++clipping[i];

                if (clip < 5)
                {
                    set_led_color_cache(kLedColorRed, MAX_BRIGHTNESS_RED);
                    set_led_color_cache(kLedColorGreen, 0);
                }
                else
                {
                    if (clip > 8)
                        clipping[i] = 0;

                    set_led_color_cache(kLedColorRed, MIN_BRIGHTNESS_RED);
                    set_led_color_cache(kLedColorGreen, 0);
                }
            }
            else // no clipping
            {
                // was clipping before
                if (clipping[i] > 0)
                    clipping[i] = 0;

                value = FILTER_WEIGHING_FACTOR * value + (1.0f - FILTER_WEIGHING_FACTOR) * filtered_value[i];

                if (value < 0.009f) // x < -40dB, off
                {
                    set_led_color_cache(kLedColorRed, 0);
                    set_led_color_cache(kLedColorGreen, 0);
                }
                else if (value < 0.5f) //x < -6dB, green
                {
                    cval = uint16_t(MAP(value, 0.0f, 0.5f, 10.f, MIN_BRIGHTNESS_GREEN_f));
                    set_led_color_cache(kLedColorRed, 0);
                    set_led_color_cache(kLedColorGreen, cval);
                }
                else if (value < 0.9f) //x < -1dB, yellow
                {
                    cval = uint16_t(MAP(value, 0.5f, 0.9f, 10.f, MIN_BRIGHTNESS_RED_f));
                    set_led_color_cache(kLedColorRed, cval);
                    set_led_color_cache(kLedColorGreen, MIN_BRIGHTNESS_GREEN);
                }
                else // all red
                {
                    set_led_color_cache(kLedColorRed, MIN_BRIGHTNESS_RED);
                    set_led_color_cache(kLedColorGreen, 0);
                }

                filtered_value[i] = value;
            }
        }
        usleep(25*1000);
    }

    return nullptr;
}

// --------------------------------------------------------------------------------------------------------------------
// peakmeter inside a container

static int jack_initialize_container(jack_client_t* client)
{
    // ----------------------------------------------------------------------------------------------------------------
    // Setup container

    const int fd = shm_open("/ac", O_RDWR, 0);

    if (fd < 0)
    {
        fprintf(stderr, "shm_open failed\n");
        return 1;
    }

    Container* const container = (Container*)mmap(NULL, sizeof(Container),
                                                  PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, fd, 0);

    if (container == NULL || container == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed\n");
        close(fd);
        return 1;
    }

    container->shm2 = fd;
    g_container = container;

    // ----------------------------------------------------------------------------------------------------------------
    // Start peakmeter thread

    g_running = true;
    pthread_create(&g_thread, NULL, peakmeter_run, client);

    return 0;
}

// --------------------------------------------------------------------------------------------------------------------
// peakmeter using MOD LEDs

static int jack_initialize_leds(jack_client_t* client, const char* load_init)
{
    // ----------------------------------------------------------------------------------------------------------------
    // Check if using inverted mode

    bool inverted = (load_init != nullptr && load_init[0] != '\0' &&
                     (std::strcmp(load_init, "1") == 0 || std::strcmp(load_init, "true") == 0));

    // ----------------------------------------------------------------------------------------------------------------
    // Setup environment

    const char* const bus_number_env = std::getenv("MOD_PEAKMETER_BUS_NUMBER");

    if (bus_number_env == nullptr || bus_number_env[0] == '\0')
    {
        printf("MOD_PEAKMETER_BUS_NUMBER env var missing\n");
        return 1;
    }

    const char* const gpio_path_env = std::getenv("MOD_PEAKMETER_GPIO_PATH");

    if (gpio_path_env == nullptr || gpio_path_env[0] == '\0')
    {
        printf("MOD_PEAKMETER_GPIO_PATH env var missing\n");
        return 1;
    }

    const int bus_number = std::atoi(bus_number_env);
    const size_t gpio_path_len = std::strlen(gpio_path_env);

    if (gpio_path_len > 1000)
    {
        printf("MOD_PEAKMETER_GPIO_PATH env var value is too big\n");
        return 1;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Configure GPIO

    FILE* fd;
    char gpio_path[1024];
    std::strcpy(gpio_path, gpio_path_env);

    std::strcpy(&gpio_path[gpio_path_len], "/direction");
    fd = fopen(gpio_path, "w");
    if (fd != NULL)
    {
        fprintf(fd, "out\n");
        fclose(fd);
    }
    else
    {
        printf("mod-peakmeter: gpio direction setup failed, path: %s\n", gpio_path);
    }

    std::strcpy(&gpio_path[gpio_path_len], "/value");
    fd = fopen(gpio_path, "w");
    if (fd != NULL)
    {
        fprintf(fd, "0\n");
        fclose(fd);
    }
    else
    {
        printf("mod-peakmeter: gpio value setup failed, path: %s\n", gpio_path);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Open i2c bus

    char i2c_dev_path[16];
    sprintf(i2c_dev_path, "/dev/i2c-%d", bus_number);

    const int bus = open(i2c_dev_path, O_RDWR);

    if (bus < 0)
    {
        printf("open failed\n");
        return 1;
    }

    if (ioctl(bus, I2C_SLAVE, PCA9685_ADDR) < 0)
    {
        printf("slave addr failed\n");
        return 1;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Reseting PCA9685 MODE1 (without SLEEP) and MODE2

    if (! all_leds_off(bus))
    {
        printf("write byte data1 failed\n");
        return 1;
    }

    const uint8_t flags = inverted ? (PCA9685_INVRT|PCA9685_OUTDRV) : PCA9685_OUTDRV;

    if (i2c_smbus_write_byte_data(bus, PCA9685_MODE2, flags) < 0)
    {
        printf("write byte data2 failed\n");
        return 1;
    }

    if (i2c_smbus_write_byte_data(bus, PCA9685_MODE1, PCA9685_ALLCALL) < 0)
    {
        printf("write byte data3 failed\n");
        return 1;
    }

    // wait for oscillator
    usleep(5*1000);

    // ----------------------------------------------------------------------------------------------------------------
    // wake up (reset sleep)

    int mode1 = i2c_smbus_read_byte_data(bus, PCA9685_MODE1);
    mode1 = mode1 & ~PCA9685_SLEEP;

    if (i2c_smbus_write_byte_data(bus, PCA9685_MODE1, mode1) < 0)
    {
        printf("write byte data4 failed\n");
        return 1;
    }

    // wait for oscillator
    usleep(5*1000);

    // ----------------------------------------------------------------------------------------------------------------
    // Reset everything

    /* By resetting all values we will only need to change the 2 off bits for setting led color.
     * This makes led color change faster, and also uses less cpu. */

    for (int i=0; i<16; ++i)
    {
        if (i2c_smbus_write_byte_data(bus, PCA9685_LED0_ON_L  + i*4, 0) < 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_ON_H  + i*4, 0) < 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_L + i*4, 0) < 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_H + i*4, 0) < 0)
        {
            printf("write byte data5 failed\n");
            return 1;
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Start peakmeter thread

    g_bus = bus;
    g_running = true;
    pthread_create(&g_thread, NULL, peakmeter_run, client);

    return 0;
}

// --------------------------------------------------------------------------------------------------------------------
// JACK internal client calls

extern "C" __attribute__ ((visibility("default")))
int jack_initialize(jack_client_t* client, const char* load_init);

int jack_initialize(jack_client_t* client, const char* load_init)
{
    if (load_init != nullptr && std::strcmp(load_init, "container") == 0)
        return jack_initialize_container(client);

    return jack_initialize_leds(client, load_init);
}

extern "C" __attribute__ ((visibility("default")))
void jack_finish(void *arg);

void jack_finish(void *arg)
{
    g_running = false;
    pthread_join(g_thread, nullptr);

    if (g_bus != -1)
        close(g_bus);

    if (g_container != nullptr)
    {
        const int fd = g_container->shm2;
        munmap(g_container, sizeof(Container));
        close(fd);
    }

    g_bus = -1;
    g_thread = -1;
    g_container = nullptr;

    return;

    // unused
    (void)arg;
}

// --------------------------------------------------------------------------------------------------------------------

#include "jacktools/jclient.cc"
#include "jacktools/jkmeter.cc"
#include "jacktools/kmeterdsp.cc"

// --------------------------------------------------------------------------------------------------------------------
