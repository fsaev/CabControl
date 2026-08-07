#ifndef RGBLED_H
#define RGBLED_H

#include <stdint.h>

class RGBLED {
public:
    RGBLED() = default;
    void init();
    enum class Colors {
        RED = 0xFF0000,
        GREEN = 0x00FF00,
        BLUE = 0x0000FF,
        YELLOW = 0xFFFF00,
        PURPLE = 0xFF00FF,
        CYAN = 0x00FFFF,
        OFF = 0x000000
    };
    void set_color(uint8_t red, uint8_t green, uint8_t blue);
    void set_color(uint32_t color);
    void set_color(Colors color) {
        set_color(static_cast<uint32_t>(color));
    }
    void turn_off();

};

#endif