
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
