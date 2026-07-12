#include "modbus_crc.h"

uint16_t modbusCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}
