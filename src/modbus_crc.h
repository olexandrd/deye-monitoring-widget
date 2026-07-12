#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t modbusCrc16(const uint8_t* data, size_t len);
