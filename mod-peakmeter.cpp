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
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

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
#ifdef __aarch64__
// MOD Duo X
#define PCA9685_BUS     0
#define PCA9685_ADDR    0x41
#define PCA9685_GPIO_ID 87
#else
// MOD Duo
#define PCA9685_BUS     4
#define PCA9685_ADDR    0x41
#define PCA9685_GPIO_ID 5
#define PCA9685_GPIO_OE "gpio5*"
#endif

// Brightness values
#define MIN_BRIGHTNESS_GREEN 120
#define MIN_BRIGHTNESS_RED   120
#define MAX_BRIGHTNESS_RED   1024

//macro for mapping audio value to LED brightness
#define MAP(x, Imin, Imax, Omin, Omax)      (( x - Imin ) * (Omax -  Omin)  / (Imax - Imin) + Omin)

// --------------------------------------------------------------------------------------------------------------------

// Do not change these enums! They match how the hardware works.
// Unless you're changing hardware, leave these alone.
enum LED_ID {
    kLedOut2,
    kLedOut1,
    kLedIn1,
    kLedIn2
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

static int           g_bus     = -1;
static volatile bool g_running = false;
static pthread_t     g_thread  = -1;
static float filtered_value[4] = {};
// --------------------------------------------------------------------------------------------------------------------
// Peak Meter thread

static void* peakmeter_run(void* arg)
{
    if (g_bus < 0 || arg == nullptr || ! g_running)
        return nullptr;

    jack_client_t* const client = (jack_client_t*)arg;

    LED_ID colorIdMap[4] = {
        kLedIn1,
        kLedIn2,
        kLedOut1,
        kLedOut2,
    };

    float pks[4];

    Jkmeter meter(client, 4, pks);

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

    float value;
    uint8_t clip, clipping[4] = { 0, 0, 0, 0 };

    //weighing factor
    //float k = 0.;

    uint16_t ledsCache[4][3] = {
        {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}
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

                //filtered_value[i] = k * value + (1.0 - k) * filtered_value[i];

                if (value < 0.032f) // x < -30dB, off
                {
                    set_led_color_cache(kLedColorRed, 0);
                    set_led_color_cache(kLedColorGreen, 0);
                }
                else if (value < 0.75f) //green
                {
                    set_led_color_cache(kLedColorRed, 0);
                    set_led_color_cache(kLedColorGreen, (MAP(value, 0, 0.75, 10, MIN_BRIGHTNESS_GREEN)));
                }
                else if (value< 0.9f) //yellow
                {
                    set_led_color_cache(kLedColorRed, (MAP(value, 0.75, 0.9, 10, MIN_BRIGHTNESS_RED)));
                    set_led_color_cache(kLedColorGreen, MIN_BRIGHTNESS_GREEN);
                }
                else // all red
                {
                    set_led_color_cache(kLedColorRed, MIN_BRIGHTNESS_RED);
                    set_led_color_cache(kLedColorGreen, 0);
                }

            }
        }
        usleep(25*1000);
    }

    return nullptr;
}

// --------------------------------------------------------------------------------------------------------------------
// JACK internal client calls

extern "C" __attribute__ ((visibility("default")))
int jack_initialize(jack_client_t* client, const char* load_init);

int jack_initialize(jack_client_t* client, const char* load_init)
{
    // ----------------------------------------------------------------------------------------------------------------
    // Check if using inverted mode

    bool inverted = (load_init != nullptr && load_init[0] != '\0' &&
                     (std::strcmp(load_init, "1") == 0 || std::strcmp(load_init, "true") == 0));

    // ----------------------------------------------------------------------------------------------------------------
    // Export and configure GPIO

    FILE* fd;

    char gpio_path[1024];
#ifdef __aarch64__
    sprintf(gpio_path, "/sys/class/gpio/gpio%d", PCA9685_GPIO_ID);
#else
    fd = popen("basename /sys/devices/platform/gpio-sunxi/gpio/" PCA9685_GPIO_OE, "r");
    if (fd != NULL)
    {
        strcpy(gpio_path, "/sys/class/gpio/");
        fgets(&gpio_path[16], sizeof(gpio_path)-1, fd);
        gpio_path[strlen(gpio_path)-1] = 0;
        pclose(fd);
    }
#endif

    fd = fopen("/sys/class/gpio/export", "w");
    if (fd != NULL)
    {
        fprintf(fd, "%i\n", PCA9685_GPIO_ID);
        fclose(fd);
    }

    int e = strlen(gpio_path);

    strcpy(&gpio_path[e], "/direction");
    fd = fopen(gpio_path, "w");
    if (fd != NULL)
    {
        fprintf(fd, "out\n");
        fclose(fd);
    }
    else
    {
        printf("gpio direction setup failed\n");
    }

    strcpy(&gpio_path[e], "/value");
    fd = fopen(gpio_path, "w");
    if (fd != NULL)
    {
        fprintf(fd, "0\n");
        fclose(fd);
    }
    else
    {
        printf("gpio value setup failed\n");
    }


    // ----------------------------------------------------------------------------------------------------------------
    // Open i2c bus

    char i2c_dev_path[16];
    sprintf(i2c_dev_path, "/dev/i2c-%d", PCA9685_BUS);

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

extern "C" __attribute__ ((visibility("default")))
void jack_finish(void);

void jack_finish(void)
{
    g_running = false;
    pthread_join(g_thread, nullptr);
    close(g_bus);

    g_bus = -1;
    g_thread = -1;
}

// --------------------------------------------------------------------------------------------------------------------

#include "jacktools/jclient.cc"
#include "jacktools/jkmeter.cc"
#include "jacktools/kmeterdsp.cc"

// --------------------------------------------------------------------------------------------------------------------
