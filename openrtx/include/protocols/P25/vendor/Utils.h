/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SWAR popcount helpers used by the P25 modem (MMDVM lineage),
 * vendored so the P25 stack does not depend on the D-STAR tree.
 */
#ifndef P25_VENDOR_UTILS_H
#define P25_VENDOR_UTILS_H

#include <stdint.h>

static inline uint32_t countBits32(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555U);
    v = (v & 0x33333333U) + ((v >> 2) & 0x33333333U);
    v = (v + (v >> 4)) & 0x0F0F0F0FU;
    return (v * 0x01010101U) >> 24;
}

static inline uint32_t countBits8(uint8_t v)
{
    static const uint8_t nibble[16] = { 0, 1, 1, 2, 1, 2, 2, 3,
                                        1, 2, 2, 3, 2, 3, 3, 4 };
    return uint32_t(nibble[v & 0x0FU] + nibble[v >> 4]);
}

#endif /* P25_VENDOR_UTILS_H */
