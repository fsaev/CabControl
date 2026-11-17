#ifndef FLAP_H
#define FLAP_H

#include <stdint.h>
#include <Adafruit_MCP4725.h>

class Flap {
public:
    Flap();
    void init();
    void set_flap(uint8_t flap);
    uint8_t get_flap() const;
    bool is_dac_present() const { return m_dac_initialized; }
    void set_dac_direct(uint16_t value);
    void write_flap_value();
private:
    uint8_t m_flap_value;
    bool m_flap_updated;
    bool m_dac_initialized;
    Adafruit_MCP4725 m_dac;
};

#endif