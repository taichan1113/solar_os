#pragma once
#include "solar_os_fonts.h"

/* Shared resolver: real generated faces, never bitmap magnification. */
static inline const uint8_t *solar_os_font_face(unsigned px, unsigned style)
{
    switch (px) {
    case 10:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_10_tf;
        case 1: return u8g2_font_solar_os_default_b_10_tf;
        case 2: return u8g2_font_solar_os_default_i_10_tf;
        default: return u8g2_font_solar_os_default_bi_10_tf;
        }
    case 12:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_12_tf;
        case 1: return u8g2_font_solar_os_default_b_12_tf;
        case 2: return u8g2_font_solar_os_default_i_12_tf;
        default: return u8g2_font_solar_os_default_bi_12_tf;
        }
    case 14:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_14_tf;
        case 1: return u8g2_font_solar_os_default_b_14_tf;
        case 2: return u8g2_font_solar_os_default_i_14_tf;
        default: return u8g2_font_solar_os_default_bi_14_tf;
        }
    case 16:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_16_tf;
        case 1: return u8g2_font_solar_os_default_b_16_tf;
        case 2: return u8g2_font_solar_os_default_i_16_tf;
        default: return u8g2_font_solar_os_default_bi_16_tf;
        }
    case 17:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_17_tf;
        case 1: return u8g2_font_solar_os_default_b_17_tf;
        case 2: return u8g2_font_solar_os_default_i_17_tf;
        default: return u8g2_font_solar_os_default_bi_17_tf;
        }
    case 18:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_18_tf;
        case 1: return u8g2_font_solar_os_default_b_18_tf;
        case 2: return u8g2_font_solar_os_default_i_18_tf;
        default: return u8g2_font_solar_os_default_bi_18_tf;
        }
    case 20:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_20_tf;
        case 1: return u8g2_font_solar_os_default_b_20_tf;
        case 2: return u8g2_font_solar_os_default_i_20_tf;
        default: return u8g2_font_solar_os_default_bi_20_tf;
        }
    case 22:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_22_tf;
        case 1: return u8g2_font_solar_os_default_b_22_tf;
        case 2: return u8g2_font_solar_os_default_i_22_tf;
        default: return u8g2_font_solar_os_default_bi_22_tf;
        }
    case 25:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_25_tf;
        case 1: return u8g2_font_solar_os_default_b_25_tf;
        case 2: return u8g2_font_solar_os_default_i_25_tf;
        default: return u8g2_font_solar_os_default_bi_25_tf;
        }
    case 28:
        switch (style) {
        case 0: return u8g2_font_solar_os_default_r_28_tf;
        case 1: return u8g2_font_solar_os_default_b_28_tf;
        case 2: return u8g2_font_solar_os_default_i_28_tf;
        default: return u8g2_font_solar_os_default_bi_28_tf;
        }
    default: return u8g2_font_solar_os_default_r_14_tf;
    }
}
