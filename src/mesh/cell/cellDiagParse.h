#pragma once

#include <stddef.h>
#include <stdint.h>

// Parsers for the modem's diagnostic responses. Kept free of Arduino types and of HAS_CELLULAR
// so the native test suite can link them; each takes a whole response and locates the expected-prefix line.

// +CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>. False when either LTE field
// reads 255 (not available) or the line is malformed.
bool parseCesq(const char *resp, int16_t *rsrpDbm, float *rsrqDb);

// +CEREG: <n>,<stat>[,...] - 1 home, 5 roaming. Returns -1 when unparseable.
int parseCeregStat(const char *resp);

// *BANDIND: <n>,<band>,<AcT> - AcT 7 is E-UTRAN.
bool parseBandInd(const char *resp, uint8_t *band, uint8_t *act);

// +CBC: <bcs>,<bcl>,<mV> - modem VBAT, not the host battery.
bool parseCbcMillivolts(const char *resp, uint16_t *mv);

// +COPS: <mode>,<format>,"<oper>"[,<AcT>]. Only accepts format 2, the numeric
// MCC/MNC; the test unit's PLMN name table is incomplete, so a name is refused
// rather than shown.
bool parseCopsNumeric(const char *resp, char *out, size_t outLen);

// ^SYSCONFIG: <mode>,<acqorder>,<roam>,<srvdomain>. Deliberately does not report <roam> - callers
// read this back only to preserve mode/acqorder/srvdomain across a roaming-only rewrite.
bool parseSysConfig(const char *resp, uint8_t *mode, uint8_t *acqorder, uint8_t *srvdomain);
