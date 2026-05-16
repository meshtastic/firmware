/*
 *
 * Copyright 2022-2023 Morse Micro
 */
/**
 * @ingroup MMWLAN_REGDB
 * @defgroup MMWLAN_REGDB_TEMPLATE Template S1G regulatory database
 *
 * \{
 *
 * @section MMWLAN_REGDB_TEMPLATE_DISCLAIMER Disclaimer
 *
 * While every effort has been made to maintain accuracy of this database, no guarantee is
 * given as to the accuracy of the information contained herein.
 *
 * @section MMWLAN_REGDB_TEMPLATE_COUNTRIES Country code list
 *
 * | Country Code | Country |
 * | ------------ | ------- |
 * | AU | Australia |
 * | EU | EU |
 * | IN | India |
 * | JP | Japan |
 * | KR | South Korea |
 * | NZ | New Zealand |
 * | SG | Singapore |
 * | US | USA |
 */

#include "mmwlan.h"

/** List of valid S1G channels for Australia. */
static const struct mmwlan_s1g_channel s1g_channels_AU[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  915500000, 10000, false, 68, 22, 27,  1,  30, 0, 0, 0 },
    {  916500000, 10000, false, 68, 22, 29,  1,  30, 0, 0, 0 },
    {  917500000, 10000, false, 68, 22, 31,  1,  30, 0, 0, 0 },
    {  918500000, 10000, false, 68, 22, 33,  1,  30, 0, 0, 0 },
    {  919500000, 10000, false, 68, 22, 35,  1,  30, 0, 0, 0 },
    {  920500000, 10000, false, 68, 22, 37,  1,  30, 0, 0, 0 },
    {  921500000, 10000, false, 68, 22, 39,  1,  30, 0, 0, 0 },
    {  922500000, 10000, false, 68, 22, 41,  1,  30, 0, 0, 0 },
    {  923500000, 10000, false, 68, 22, 43,  1,  30, 0, 0, 0 },
    {  924500000, 10000, false, 68, 22, 45,  1,  30, 0, 0, 0 },
    {  925500000, 10000, false, 68, 22, 47,  1,  30, 0, 0, 0 },
    {  926500000, 10000, false, 68, 22, 49,  1,  30, 0, 0, 0 },
    {  927500000, 10000, false, 68, 22, 51,  1,  30, 0, 0, 0 },
    {  917000000, 10000, false, 69, 23, 30,  2,  30, 0, 0, 0 },
    {  919000000, 10000, false, 69, 23, 34,  2,  30, 0, 0, 0 },
    {  921000000, 10000, false, 69, 23, 38,  2,  30, 0, 0, 0 },
    {  923000000, 10000, false, 69, 23, 42,  2,  30, 0, 0, 0 },
    {  925000000, 10000, false, 69, 23, 46,  2,  30, 0, 0, 0 },
    {  927000000, 10000, false, 69, 23, 50,  2,  30, 0, 0, 0 },
    {  918000000, 10000, false, 70, 24, 32,  4,  30, 0, 0, 0 },
    {  922000000, 10000, false, 70, 24, 40,  4,  30, 0, 0, 0 },
    {  926000000, 10000, false, 70, 24, 48,  4,  30, 0, 0, 0 },
    {  924000000, 10000, false, 71, 25, 44,  8,  30, 0, 0, 0 },
};

/** Channel list structure for Australia. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_AU = {
    .country_code = "AU",
    .num_channels = (sizeof(s1g_channels_AU)/sizeof(s1g_channels_AU[0])),
    .channels = s1g_channels_AU,
};

/** List of valid S1G channels for EU. */
static const struct mmwlan_s1g_channel s1g_channels_EU[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  863500000,   280, false, 66,  6,  1,  1,  16, 0, 0, 0 },
    {  864500000,   280, false, 66,  6,  3,  1,  16, 0, 0, 0 },
    {  865500000,   280, false, 66,  6,  5,  1,  16, 0, 0, 0 },
    {  866500000,   280, false, 66,  6,  7,  1,  16, 0, 0, 0 },
    {  867500000,   280, false, 66,  6,  9,  1,  16, 0, 0, 0 },
};

/** Channel list structure for EU. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_EU = {
    .country_code = "EU",
    .num_channels = (sizeof(s1g_channels_EU)/sizeof(s1g_channels_EU[0])),
    .channels = s1g_channels_EU,
};

/** List of valid S1G channels for India. */
static const struct mmwlan_s1g_channel s1g_channels_IN[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  865500000,   280, false, 66,  6,  5,  1,  16, 0, 0, 0 },
    {  866500000,   280, false, 66,  6,  7,  1,  16, 0, 0, 0 },
    {  867500000,   280, false, 66,  6,  9,  1,  16, 0, 0, 0 },
};

/** Channel list structure for India. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_IN = {
    .country_code = "IN",
    .num_channels = (sizeof(s1g_channels_IN)/sizeof(s1g_channels_IN[0])),
    .channels = s1g_channels_IN,
};

/** List of valid S1G channels for Japan. */
static const struct mmwlan_s1g_channel s1g_channels_JP[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  921000000,  1000, true, 73,  8,  9,  1,  16, 2000, 2000, 100000 },
    {  923000000,  1000, true, 73,  8, 13,  1,  16, 2000, 2000, 100000 },
    {  924000000,  1000, true, 73,  8, 15,  1,  16, 2000, 2000, 100000 },
    {  925000000,  1000, true, 73,  8, 17,  1,  16, 2000, 2000, 100000 },
    {  926000000,  1000, true, 73,  8, 19,  1,  16, 2000, 2000, 100000 },
    {  927000000,  1000, true, 73,  8, 21,  1,  16, 2000, 2000, 100000 },
    {  923500000,  1000, true, 64,  9,  2,  2,  16, 2000, 2000, 100000 },
    {  924500000,  1000, true, 64, 10,  4,  2,  16, 2000, 2000, 100000 },
    {  925500000,  1000, true, 64,  9,  6,  2,  16, 2000, 2000, 100000 },
    {  926500000,  1000, true, 64, 10,  8,  2,  16, 2000, 2000, 100000 },
    {  924500000,  1000, true, 65, 11, 36,  4,  16, 2000, 2000, 100000 },
    {  925500000,  1000, true, 65, 12, 38,  4,  16, 2000, 2000, 100000 },
};

/** Channel list structure for Japan. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_JP = {
    .country_code = "JP",
    .num_channels = (sizeof(s1g_channels_JP)/sizeof(s1g_channels_JP[0])),
    .channels = s1g_channels_JP,
};

/** List of valid S1G channels for South Korea. */
static const struct mmwlan_s1g_channel s1g_channels_KR[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  918000000, 10000, false, 74, 14,  1,  1,   4, 50000, 0, 4000000 },
    {  919000000, 10000, false, 74, 14,  3,  1,   4, 50000, 0, 4000000 },
    {  920000000, 10000, false, 74, 14,  5,  1,   4, 50000, 0, 4000000 },
    {  921000000, 10000, false, 74, 14,  7,  1,   4, 50000, 0, 4000000 },
    {  922000000, 10000, false, 74, 14,  9,  1,  10, 50000, 0, 4000000 },
    {  923000000, 10000, false, 74, 14, 11,  1,  10, 50000, 0, 4000000 },
    {  918500000, 10000, false, 75, 15,  2,  2,   4, 50000, 0, 4000000 },
    {  920500000, 10000, false, 75, 15,  6,  2,   4, 50000, 0, 4000000 },
    {  922500000, 10000, false, 75, 15, 10,  2,  10, 50000, 0, 4000000 },
    {  921500000, 10000, false, 76, 16,  8,  4,   4, 50000, 0, 4000000 },
    {  926500000, 10000, false, 74, 14, 18,  1,  17, 264, 0, 220000 }, /* Warning: regulatory requirements may not be met */
    {  927500000, 10000, false, 74, 14, 20,  1,  17, 264, 0, 220000 }, /* Warning: regulatory requirements may not be met */
    {  928500000, 10000, false, 74, 14, 22,  1,  17, 264, 0, 220000 }, /* Warning: regulatory requirements may not be met */
    {  929500000, 10000, false, 74, 14, 24,  1,  17, 264, 0, 220000 }, /* Warning: regulatory requirements may not be met */
    {  927000000, 10000, false, 75, 15, 19,  2,  20, 264, 0, 220000 }, /* Warning: regulatory requirements may not be met */
    {  929000000, 10000, false, 75, 15, 23,  2,  20, 264, 0, 220000 }, /* Warning: regulatory requirements may not be met */
};

/** Channel list structure for South Korea. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_KR = {
    .country_code = "KR",
    .num_channels = (sizeof(s1g_channels_KR)/sizeof(s1g_channels_KR[0])),
    .channels = s1g_channels_KR,
};

/** List of valid S1G channels for New Zealand. */
static const struct mmwlan_s1g_channel s1g_channels_NZ[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  915500000, 10000, false, 68, 26, 27,  1,  30, 0, 0, 0 },
    {  916500000, 10000, false, 68, 26, 29,  1,  30, 0, 0, 0 },
    {  917500000, 10000, false, 68, 26, 31,  1,  30, 0, 0, 0 },
    {  918500000, 10000, false, 68, 26, 33,  1,  30, 0, 0, 0 },
    {  919500000, 10000, false, 68, 26, 35,  1,  30, 0, 0, 0 },
    {  920500000, 10000, false, 68, 26, 37,  1,  36, 0, 0, 0 },
    {  921500000, 10000, false, 68, 26, 39,  1,  36, 0, 0, 0 },
    {  922500000, 10000, false, 68, 26, 41,  1,  36, 0, 0, 0 },
    {  923500000, 10000, false, 68, 26, 43,  1,  36, 0, 0, 0 },
    {  924500000, 10000, false, 68, 26, 45,  1,  36, 0, 0, 0 },
    {  925500000, 10000, false, 68, 26, 47,  1,  36, 0, 0, 0 },
    {  926500000, 10000, false, 68, 26, 49,  1,  36, 0, 0, 0 },
    {  927500000, 10000, false, 68, 26, 51,  1,  36, 0, 0, 0 },
    {  917000000, 10000, false, 69, 27, 30,  2,  30, 0, 0, 0 },
    {  919000000, 10000, false, 69, 27, 34,  2,  30, 0, 0, 0 },
    {  921000000, 10000, false, 69, 27, 38,  2,  36, 0, 0, 0 },
    {  923000000, 10000, false, 69, 27, 42,  2,  36, 0, 0, 0 },
    {  925000000, 10000, false, 69, 27, 46,  2,  36, 0, 0, 0 },
    {  927000000, 10000, false, 69, 27, 50,  2,  36, 0, 0, 0 },
    {  918000000, 10000, false, 70, 28, 32,  4,  30, 0, 0, 0 },
    {  922000000, 10000, false, 70, 28, 40,  4,  36, 0, 0, 0 },
    {  926000000, 10000, false, 70, 28, 48,  4,  36, 0, 0, 0 },
    {  924000000, 10000, false, 71, 29, 44,  8,  36, 0, 0, 0 },
};

/** Channel list structure for New Zealand. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_NZ = {
    .country_code = "NZ",
    .num_channels = (sizeof(s1g_channels_NZ)/sizeof(s1g_channels_NZ[0])),
    .channels = s1g_channels_NZ,
};

/** List of valid S1G channels for Singapore. */
static const struct mmwlan_s1g_channel s1g_channels_SG[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  866500000,   277, false, 66, 17,  7,  1,  29, 100000, 0, 1000000 },
    {  867500000,   277, false, 66, 17,  9,  1,  29, 100000, 0, 1000000 },
    {  868500000,   277, false, 66, 17, 11,  1,  29, 100000, 0, 1000000 },
    {  868000000,   277, false, 67, 19, 10,  2,  29, 100000, 0, 1000000 },
    {  920500000, 10000, false, 68, 18, 37,  1,  22, 0, 0, 0 },
    {  921500000, 10000, false, 68, 18, 39,  1,  22, 0, 0, 0 },
    {  922500000, 10000, false, 68, 18, 41,  1,  22, 0, 0, 0 },
    {  923500000, 10000, false, 68, 18, 43,  1,  22, 0, 0, 0 },
    {  924500000, 10000, false, 68, 18, 45,  1,  22, 0, 0, 0 },
    {  921000000, 10000, false, 69, 20, 38,  2,  22, 0, 0, 0 },
    {  923000000, 10000, false, 69, 20, 42,  2,  22, 0, 0, 0 },
    {  922000000, 10000, false, 70, 21, 40,  4,  22, 0, 0, 0 },
};

/** Channel list structure for Singapore. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_SG = {
    .country_code = "SG",
    .num_channels = (sizeof(s1g_channels_SG)/sizeof(s1g_channels_SG[0])),
    .channels = s1g_channels_SG,
};

/** List of valid S1G channels for USA. */
static const struct mmwlan_s1g_channel s1g_channels_US[] = {
    /* Ctr Freq (Hz), Duty Cycle (%/100), Omit Control Response, Global Op Class, S1G Op Class, S1G Chan #, Op BW, Max Tx EIRP (dBm), Min Packet Spacing Window (microsec), airtime_min (microsec), airtime_max (microsec) */
    {  902500000, 10000, false, 68,  1,  1,  1,  36, 0, 0, 0 }, /* Warning: regulatory requirements may not be met */
    {  903500000, 10000, false, 68,  1,  3,  1,  36, 0, 0, 0 },
    {  904500000, 10000, false, 68,  1,  5,  1,  36, 0, 0, 0 },
    {  905500000, 10000, false, 68,  1,  7,  1,  36, 0, 0, 0 },
    {  906500000, 10000, false, 68,  1,  9,  1,  36, 0, 0, 0 },
    {  907500000, 10000, false, 68,  1, 11,  1,  36, 0, 0, 0 },
    {  908500000, 10000, false, 68,  1, 13,  1,  36, 0, 0, 0 },
    {  909500000, 10000, false, 68,  1, 15,  1,  36, 0, 0, 0 },
    {  910500000, 10000, false, 68,  1, 17,  1,  36, 0, 0, 0 },
    {  911500000, 10000, false, 68,  1, 19,  1,  36, 0, 0, 0 },
    {  912500000, 10000, false, 68,  1, 21,  1,  36, 0, 0, 0 },
    {  913500000, 10000, false, 68,  1, 23,  1,  36, 0, 0, 0 },
    {  914500000, 10000, false, 68,  1, 25,  1,  36, 0, 0, 0 },
    {  915500000, 10000, false, 68,  1, 27,  1,  36, 0, 0, 0 },
    {  916500000, 10000, false, 68,  1, 29,  1,  36, 0, 0, 0 },
    {  917500000, 10000, false, 68,  1, 31,  1,  36, 0, 0, 0 },
    {  918500000, 10000, false, 68,  1, 33,  1,  36, 0, 0, 0 },
    {  919500000, 10000, false, 68,  1, 35,  1,  36, 0, 0, 0 },
    {  920500000, 10000, false, 68,  1, 37,  1,  36, 0, 0, 0 },
    {  921500000, 10000, false, 68,  1, 39,  1,  36, 0, 0, 0 },
    {  922500000, 10000, false, 68,  1, 41,  1,  36, 0, 0, 0 },
    {  923500000, 10000, false, 68,  1, 43,  1,  36, 0, 0, 0 },
    {  924500000, 10000, false, 68,  1, 45,  1,  36, 0, 0, 0 },
    {  925500000, 10000, false, 68,  1, 47,  1,  36, 0, 0, 0 },
    {  926500000, 10000, false, 68,  1, 49,  1,  36, 0, 0, 0 },
    {  927500000, 10000, false, 68,  1, 51,  1,  36, 0, 0, 0 },
    {  903000000, 10000, false, 69,  2,  2,  2,  36, 0, 0, 0 }, /* Warning: regulatory requirements may not be met */
    {  905000000, 10000, false, 69,  2,  6,  2,  36, 0, 0, 0 },
    {  907000000, 10000, false, 69,  2, 10,  2,  36, 0, 0, 0 },
    {  909000000, 10000, false, 69,  2, 14,  2,  36, 0, 0, 0 },
    {  911000000, 10000, false, 69,  2, 18,  2,  36, 0, 0, 0 },
    {  913000000, 10000, false, 69,  2, 22,  2,  36, 0, 0, 0 },
    {  915000000, 10000, false, 69,  2, 26,  2,  36, 0, 0, 0 },
    {  917000000, 10000, false, 69,  2, 30,  2,  36, 0, 0, 0 },
    {  919000000, 10000, false, 69,  2, 34,  2,  36, 0, 0, 0 },
    {  921000000, 10000, false, 69,  2, 38,  2,  36, 0, 0, 0 },
    {  923000000, 10000, false, 69,  2, 42,  2,  36, 0, 0, 0 },
    {  925000000, 10000, false, 69,  2, 46,  2,  36, 0, 0, 0 },
    {  927000000, 10000, false, 69,  2, 50,  2,  36, 0, 0, 0 },
    {  906000000, 10000, false, 70,  3,  8,  4,  36, 0, 0, 0 },
    {  910000000, 10000, false, 70,  3, 16,  4,  36, 0, 0, 0 },
    {  914000000, 10000, false, 70,  3, 24,  4,  36, 0, 0, 0 },
    {  918000000, 10000, false, 70,  3, 32,  4,  36, 0, 0, 0 },
    {  922000000, 10000, false, 70,  3, 40,  4,  36, 0, 0, 0 },
    {  926000000, 10000, false, 70,  3, 48,  4,  36, 0, 0, 0 },
    {  908000000, 10000, false, 71,  4, 12,  8,  36, 0, 0, 0 },
    {  916000000, 10000, false, 71,  4, 28,  8,  36, 0, 0, 0 },
    {  924000000, 10000, false, 71,  4, 44,  8,  36, 0, 0, 0 },
};

/** Channel list structure for USA. */
static const struct mmwlan_s1g_channel_list s1g_channel_list_US = {
    .country_code = "US",
    .num_channels = (sizeof(s1g_channels_US)/sizeof(s1g_channels_US[0])),
    .channels = s1g_channels_US,
};

/** Array of all channel list structs used for the regulatory database. */

static const struct mmwlan_s1g_channel_list *regulatory_db_domains[] = {
    &s1g_channel_list_AU,
    &s1g_channel_list_EU,
    &s1g_channel_list_IN,
    &s1g_channel_list_JP,
    &s1g_channel_list_KR,
    &s1g_channel_list_NZ,
    &s1g_channel_list_SG,
    &s1g_channel_list_US,
};

/** Regulatory database. */
static const struct mmwlan_regulatory_db regulatory_db = {
    .num_domains = (sizeof(regulatory_db_domains)/sizeof(regulatory_db_domains[0])),
    .domains = regulatory_db_domains,
};

/**
 * Get a pointer to regulatory_db. This function isn't strictly necessary, since regulatory_db
 * can be accessed directly, but will prevent the compiler from generated warnings about
 * regulatory_db being unused.
 *
 * @return Reference to the regulatory database
 */
static inline const struct mmwlan_regulatory_db *get_regulatory_db(void)
{
    return &regulatory_db;
}


/** \} */
