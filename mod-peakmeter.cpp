
#include "jacktools/jclient.cc"
#include "jacktools/jkmeter.cc"
#include "jacktools/kmeterdsp.cc"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>

#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include "i2c-dev.h"
}

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

// Custom MOD
#define PCA9685_BUS     4
#define PCA9685_ADDR    0x41
#define PCA9685_GPIO_ID 5
#define PCA9685_GPIO_OE "gpio5_pa8"

// Change this value if light is too bright.
// Maximum is 4095
#define MAX_BRIGHTNESS 4095

// Change this to false according to inverted status
static const bool kInverted = false;

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

    return (i2c_smbus_write_byte_data(bus, PCA9685_LED0_ON_L  + channel, 0) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_ON_H  + channel, 0) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_L + channel, value & 0xFF) >= 0 &&
            i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_H + channel, value >> 8)   >= 0);
}

#if 0
void setPWMFreq(int bus, float freq)
{
    printf("Set PWM frequency %f\n", freq);

    float prescaleval = 25000000.0f; // 25MHz
    prescaleval /= 4096.0f;          // 12-bit
    prescaleval /= freq;
    prescaleval -= 1.0f;
    printf("Setting PWM frequency to %f Hz\n", freq);
    printf("Estimated pre-scale: %f\n", prescaleval);

    const int prescale = std::floor(prescaleval + 0.5f);
    printf("Final pre-scale: %d\n", prescale);

    const int oldmode = i2c_smbus_read_byte_data(bus, PCA9685_MODE1);
    /* */ int newmode = (oldmode & 0x7F) | PCA9685_SLEEP;
    i2c_smbus_write_byte_data(bus, PCA9685_MODE1, newmode); // go to sleep
    i2c_smbus_write_byte_data(bus, PCA9685_PRESCALE, prescale);
    i2c_smbus_write_byte_data(bus, PCA9685_MODE1, oldmode);
    usleep(5*1000);
    i2c_smbus_write_byte_data(bus, PCA9685_MODE1, oldmode | PCA9685_RESTART);
}

int setPWM(int bus, uint8_t channel, uint16_t on, uint16_t off)
{
    printf("Set a single PWM channel %i %i %i\n", channel, on, off);
    if (i2c_smbus_write_byte_data(bus, PCA9685_LED0_ON_L  + channel*4, on & 0xFF)  < 0 ||
        i2c_smbus_write_byte_data(bus, PCA9685_LED0_ON_H  + channel*4, on >> 8)    < 0 ||
        i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_L + channel*4, off & 0xFF) < 0 ||
        i2c_smbus_write_byte_data(bus, PCA9685_LED0_OFF_H + channel*4, off >> 8)   < 0)
        return -1;
    return 0;
}

int setAllPWM(int bus, uint16_t on, uint16_t off)
{
    printf("Set all PWM channels %i %i\n", on, off);
    if (i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_ON_L,  on & 0xFF)  < 0 ||
        i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_ON_H,  on >> 8)    < 0 ||
        i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_OFF_L, off & 0xFF) < 0 ||
        i2c_smbus_write_byte_data(bus, PCA9685_ALL_LED_OFF_H, off >> 8)   < 0)
        return -1;
    return 0;
}
#endif

int main()
{
    // ----------------------------------------------------------------------------------------------------------------
    // Export and configure GPIO

    if (FILE* const fd = fopen("/sys/class/gpio/export", "w"))
    {
        fprintf(fd, "%i\n", PCA9685_GPIO_ID);
        fclose(fd);
    }

    if (FILE* const fd = fopen("/sys/class/gpio/" PCA9685_GPIO_OE "/direction", "w"))
    {
        fprintf(fd, "out\n");
        fclose(fd);
    }

    if (FILE* const fd = fopen("/sys/class/gpio/" PCA9685_GPIO_OE "/value", "w"))
    {
        fprintf(fd, kInverted ? "0\n" : "1\n");
        fclose(fd);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Open i2c bus

    const int bus = open("/dev/i2c-4", O_RDWR);

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

    if (i2c_smbus_write_byte_data(bus, PCA9685_MODE2, PCA9685_INVRT|PCA9685_OUTDRV) < 0)
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
    // Monitor audio, let's start the fun!

    float pks[4];

    LED_ID colorIdMap[4] = {
        kLedIn1,
        kLedIn2,
        kLedOut1,
        kLedOut2,
    };

    Jkmeter meter("meter", 4, pks);

    while (meter.get_levels() == Jkmeter::PROCESS)
    {
        for (int i=0; i<4; ++i)
        {
            if (pks[i] < 0.01f)
            {
                set_led_color(bus, colorIdMap[i], kLedColorRed, 0);
                set_led_color(bus, colorIdMap[i], kLedColorGreen, MAX_BRIGHTNESS);
            }
            else
            {
                const int diff = pks[i]*MAX_BRIGHTNESS;
                if (diff < 0)
                {
                    printf("error in pks diff\n");
                    continue;
                }

                if (diff > MAX_BRIGHTNESS)
                {
                    set_led_color(bus, colorIdMap[i], kLedColorRed, MAX_BRIGHTNESS);
                    set_led_color(bus, colorIdMap[i], kLedColorGreen, 0);
                }
                else
                {
                    set_led_color(bus, colorIdMap[i], kLedColorRed, diff);
                    set_led_color(bus, colorIdMap[i], kLedColorGreen, MAX_BRIGHTNESS-diff);
                }
            }
        }
        usleep(25*1000);
    }

/*
jack_connect system:capture_1 meter:in_1
jack_connect system:capture_2 meter:in_2
jack_connect mod-host:monitor-out_1 meter:in_3
jack_connect mod-host:monitor-out_2 meter:in_4
*/

    close(bus);

    return 0;
}
