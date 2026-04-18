/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.3.0
 */

#if 1 /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  16
#define LV_DPI_DEF 130

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_USE_OS   LV_OS_FREERTOS

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/
#define LV_USE_LOG 0

/*-------------
 * Asserts
 *-----------*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*==================
 * FONT USAGE
 *==================*/
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*==================
 * WIDGETS
 *==================*/
#define LV_USE_LABEL    1
#define LV_USE_BTN      1
#define LV_USE_ARC      1
#define LV_USE_BAR      1
#define LV_USE_SLIDER   1
#define LV_USE_SWITCH   1
#define LV_USE_IMAGE    1
#define LV_USE_CANVAS   1

/*==================
 * THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT 1

/*==================
 * OTHERS
 *==================*/
#define LV_USE_SYSMON               1
#define LV_SYSMON_GET_IDLE          lv_timer_get_idle
#define LV_USE_PERF_MONITOR         1
#define LV_USE_PERF_MONITOR_POS     LV_ALIGN_TOP_RIGHT

/*==================
 * FILESYSTEM
 *==================*/
#define LV_USE_FS_MEMFS 1
#if LV_USE_FS_MEMFS
    #define LV_FS_MEMFS_LETTER 'M'
#endif

/*--END OF LV_CONF_H--*/

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/
