/*
TinyGPS++ - a small GPS library for Arduino providing universal NMEA parsing
Based on work by and "distanceBetween" and "courseTo" courtesy of Maarten Lamers.
Suggestion to add satellites, courseTo(), and cardinal() by Matt Monson.
Location precision improvements suggested by Wayne Holder.
Copyright (C) 2008-2013 Mikal Hart
All rights reserved.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "TinyGPS++.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#define _GSVterm   "GSV"
#define _RMCterm   "RMC"
#define _GGAterm   "GGA"

TinyGPSPlus::TinyGPSPlus()
  :  parity(0)
  ,  flags(FLAG_DEFAULT)
  ,  curSentenceType(GPS_SENTENCE_OTHER)
  ,  curTermNumber(0)
  ,  curTermOffset(0)
  ,  fixQ(0)
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
  ,  customElts(0)
  ,  customCandidates(0)
#endif
#ifndef TINYGPSPLUS_OPTION_NO_STATISTICS
  ,  encodedCharCount(0)
  ,  sentencesWithFixCount(0)
  ,  failedChecksumCount(0)
  ,  passedChecksumCount(0)
#endif
{
  term[0] = '\0';
  memset(trackedSatellites, 0, sizeof(trackedSatellites));
  memset(gsaInfo, 0, sizeof(gsaInfo));
}

//
// public methods
//

bool TinyGPSPlus::encode(char c)
{
  ++encodedCharCount;

  switch(c)
  {
  case ',': // term terminators
    parity ^= (uint8_t)c;
    [[fallthrough]];
  case '\r':
  case '\n':
  case '*':
    {
      bool isValidSentence = false;
      if (curTermOffset == 0) {
        term[curTermOffset] = 0;
        isValidSentence = endOfTermHandler(false);

      } else if (curTermOffset < sizeof(term)) {
        term[curTermOffset] = 0;
        isValidSentence = endOfTermHandler(true);
      }
      ++curTermNumber;
      curTermOffset = 0;
      term[curTermOffset] = 0;
      if(c == '*')
      {
        flags |= FLAG_IS_CHECKSUM_TERM;
      }
      else
      {
        flags &= (~FLAG_IS_CHECKSUM_TERM);
      }
      return isValidSentence;
    }
    break;

  case '$': // sentence begin
    sentenceTime = millis();
    curTermNumber = curTermOffset = 0;
    parity = 0;
    curSentenceType = GPS_SENTENCE_OTHER;
    currentGSVSystem = TINYGPS_GNSS_UNKNOWN;
    trackedSatellitesIndex = -1;
    currentGSATalkerSystem = TINYGPS_GNSS_UNKNOWN;
    pendingGSASystem = TINYGPS_GNSS_UNKNOWN;
    pendingGSAUsed = 0;
    pendingGSAFixType = 0;
    pendingGSAPDOP = 0;
    pendingGSAHDOP = 0;
    pendingGSAVDOP = 0;
    pendingGLLStatus = 'V';
    pendingGLLMode = 'N';
    pendingZDADay = 0;
    pendingZDAMonth = 0;
    pendingZDAYear = 0;
    pendingZDAZoneHours = 0;
    pendingZDAZoneMinutes = 0;
    pendingAntennaStatus = TINYGPS_ANT_UNKNOWN;
    flags &= (~FLAG_IS_CHECKSUM_TERM);
    setSentenceHasFix(false);
    return false;

  default: // ordinary characters
    if (curTermOffset < sizeof(term) - 1)
      term[curTermOffset++] = c;
    if ((flags & FLAG_IS_CHECKSUM_TERM)==0)
      parity ^= c;
    return false;
  }

  return false;
}

int TinyGPSPlus::GGA(char *buf)
{
  char *end = buf;

  if (fixQ == 0) {
    end += sprintf(end, "$_GGA,,,,,,,,,,,,,,");
  } else {
    end += sprintf(end,
                   "$_GGA,%02d%02d%02d.%02d,%02d%10.7f,%c,%03d%10.7f,%c,%d,%02d,%.1f,%.3f,M,%.3f,M,,",
                   time.hour(), time.minute(), time.second(),
                   time.centisecond(), location.rawLat().deg,
                   (location.lat() - location.rawLat().deg) * 60,
                   location.rawLat().negative ? 'S' : 'N',
                   location.rawLng().deg,
                   (location.lng() - location.rawLng().deg) * 60,
                   location.rawLng().negative ? 'W' : 'E', fixQuality(),
                   satellites.value(), hdop.hdop(), altitude.meters(),
                   geoidHeight.meters());
  }

  char checksum = 0;
  for (char *ptr = buf + 1; *ptr != '\0'; ++ptr)
    checksum ^= *ptr;

  end += sprintf(end, "*%02X\x0D\x0A", checksum);
  return static_cast<int>(end - buf);
}

//
// internal utilities
//
int TinyGPSPlus::fromHex(char a)
{
  if (a >= 'A' && a <= 'F')
    return a - 'A' + 10;
  else if (a >= 'a' && a <= 'f')
    return a - 'a' + 10;
  else
    return a - '0';
}

// static
// Parse a (potentially negative) number with up to 2 decimal digits -xxxx.yy
int32_t TinyGPSPlus::parseDecimal(const char *term)
{
  bool negative = (*term == '-');
  if (negative) ++term;
  int32_t ret = 100 * (int32_t)atol(term);
  while (isdigit((unsigned char)*term)) ++term;
  if (*term == '.' && isdigit((unsigned char)term[1]))
  {
    ret += 10 * (term[1] - '0');
    if (isdigit((unsigned char)term[2]))
      ret += term[2] - '0';
  }
  return negative ? -ret : ret;
}

// static
// Parse degrees in that funny NMEA format DDMM.MMMM
void TinyGPSPlus::parseDegrees(const char *term, RawDegrees &deg)
{

  deg.deg = 181; // Set to invalid value
  if (!isdigit(*term) && *term != '.') {
    // An invalid character
    // TODO: Must check if the degree is allowed to start with a decimal point.
    return;
  }

  const uint32_t leftOfDecimal = (uint32_t)atol(term);

  while (isdigit(*term)) {
    ++term;
  }

  if (*term != '.') {
    // Degree must have a decimal point
    return;
  }

  deg.deg = (int16_t)(leftOfDecimal / 100);
  uint16_t minutes = (uint16_t)(leftOfDecimal % 100);
  uint32_t multiplier = 10000000UL;
  uint32_t tenMillionthsOfMinutes = minutes * multiplier;
 
  while (isdigit(*++term))
  {
    multiplier /= 10;
    tenMillionthsOfMinutes += (*term - '0') * multiplier;
  }

  deg.billionths = (5 * tenMillionthsOfMinutes + 1) / 3;
  deg.negative = false;
}

#define COMBINE(sentence_type, term_number) (((unsigned)(sentence_type) << 5) | term_number)

// Processes a just-completed term
// Returns true if new sentence has just passed checksum test and is validated
bool TinyGPSPlus::endOfTermHandler(bool termIsNotEmpty)
{
  // If it's the checksum term, and the checksum checks out, commit
  if ((flags & FLAG_IS_CHECKSUM_TERM) != 0)
  {
    byte checksum = 16 * fromHex(term[0]) + fromHex(term[1]);
    if (checksum == parity)
    {
      passedChecksumCount++;
      if (sentenceHasFix())
        ++sentencesWithFixCount;

      switch(curSentenceType)
      {
      case GPS_SENTENCE_RMC:
        if (date.isNotEmpty())
          date.commit(sentenceTime);
        if (time.isNotEmpty())
          time.commit(sentenceTime);
        if (sentenceHasFix())
        {
          if (location.isNotEmpty())
           location.commit(sentenceTime);
          if (speed.isNotEmpty())
           speed.commit(sentenceTime);
          if (course.isNotEmpty())
           course.commit(sentenceTime);
        }
        break;
      case GPS_SENTENCE_GGA:
        if (time.isNotEmpty())
          time.commit(sentenceTime);
        if (sentenceHasFix())
        {
          if (location.isNotEmpty())
            location.commit(sentenceTime);
          if (altitude.isNotEmpty())
            altitude.commit(sentenceTime);
          if (geoidHeight.isNotEmpty())
            geoidHeight.commit(sentenceTime);
        }
        if (satellites.isNotEmpty())
          satellites.commit(sentenceTime);
        if (hdop.isNotEmpty())
          hdop.commit(sentenceTime);
        break;

      case GPS_SENTENCE_GSA:
      {
        uint8_t system = pendingGSASystem;
        if (system == TINYGPS_GNSS_UNKNOWN)
          system = currentGSATalkerSystem;

        if (system >= TINYGPS_GNSS_GPS && system <= TINYGPS_GNSS_QZSS) {
          gsaInfo[system].system = system;
          gsaInfo[system].satellitesUsed = pendingGSAUsed;
          gsaInfo[system].fixType = pendingGSAFixType;
          gsaInfo[system].pdop = pendingGSAPDOP;
          gsaInfo[system].hdop = pendingGSAHDOP;
          gsaInfo[system].vdop = pendingGSAVDOP;
          gsaInfo[system].valid = true;
        }
        break;
      }

      case GPS_SENTENCE_GLL:
        if (gllTime.isNotEmpty())
          gllTime.commit(sentenceTime);
        if (pendingGLLStatus == 'A' && gllLocation.isNotEmpty())
          gllLocation.commit(sentenceTime);
        gllInfo.status = pendingGLLStatus;
        gllInfo.mode = pendingGLLMode;
        gllInfo.valid = pendingGLLStatus == 'A';
        gllInfo.lastUpdate = sentenceTime;
        break;

      case GPS_SENTENCE_ZDA:
        if (zdaTime.isNotEmpty())
          zdaTime.commit(sentenceTime);
        zdaInfo.year = pendingZDAYear;
        zdaInfo.month = pendingZDAMonth;
        zdaInfo.day = pendingZDADay;
        zdaInfo.localZoneHours = pendingZDAZoneHours;
        zdaInfo.localZoneMinutes = pendingZDAZoneMinutes;
        zdaInfo.valid = pendingZDAYear >= 2000 &&
                        pendingZDAMonth >= 1 && pendingZDAMonth <= 12 &&
                        pendingZDADay >= 1 && pendingZDADay <= 31;
        zdaInfo.lastUpdate = sentenceTime;
        break;

      case GPS_SENTENCE_TXT:
        if (pendingAntennaStatus != TINYGPS_ANT_UNKNOWN) {
          antInfo.status = pendingAntennaStatus;
          antInfo.valid = true;
          antInfo.lastUpdate = sentenceTime;
        }
        break;
      }

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
      // Commit all custom listeners of this sentence type
      for (TinyGPSCustom *p = customCandidates; p != NULL && strcmp(p->sentenceName, customCandidates->sentenceName) == 0; p = p->next)
         p->commit(sentenceTime);
#endif
      return true;
    }

    else
    {
      if (curSentenceType == GPS_SENTENCE_RMC) {
          // Bad checksum in GPRMC sentence, reset optional variables
          //   to prevent meshtastic-device issue #863
          course.newval = speed.newval = 0;
      }
      ++failedChecksumCount;
    }

    return false;
  }

  // The first term determines the sentence type.
  // xx = NMEA Talker ID:
  // GP=GPS, GL=GLONASS, GA=Galileo, GB/BD=BeiDou, GQ=QZSS, GN=mixed GNSS.
  if (curTermNumber == 0)
  {
    if (strlen(term) == 5 && !strncmp(term + 2, "RMC", 3))
      curSentenceType = GPS_SENTENCE_RMC;
    else if (strlen(term) == 5 && !strncmp(term + 2, "GGA", 3))
      curSentenceType = GPS_SENTENCE_GGA;
    else if (strlen(term) == 5 && !strncmp(term + 2, "GLL", 3))
      curSentenceType = GPS_SENTENCE_GLL;
    else if (strlen(term) == 5 && !strncmp(term + 2, "ZDA", 3))
      curSentenceType = GPS_SENTENCE_ZDA;
    else if (strlen(term) == 5 && !strncmp(term + 2, "TXT", 3))
      curSentenceType = GPS_SENTENCE_TXT;
    else if (strlen(term) == 5 && !strncmp(term + 2, "GSA", 3))
    {
      curSentenceType = GPS_SENTENCE_GSA;

      if (!strncmp(term, "GP", 2))
        currentGSATalkerSystem = TINYGPS_GNSS_GPS;
      else if (!strncmp(term, "GL", 2))
        currentGSATalkerSystem = TINYGPS_GNSS_GLONASS;
      else if (!strncmp(term, "GA", 2))
        currentGSATalkerSystem = TINYGPS_GNSS_GALILEO;
      else if (!strncmp(term, "GB", 2) || !strncmp(term, "BD", 2))
        currentGSATalkerSystem = TINYGPS_GNSS_BEIDOU;
      else if (!strncmp(term, "GQ", 2))
        currentGSATalkerSystem = TINYGPS_GNSS_QZSS;
      else
        currentGSATalkerSystem = TINYGPS_GNSS_UNKNOWN;
    }
    else if (strlen(term) == 5 && !strncmp(term + 2, "GSV", 3))
    {
      curSentenceType = GPS_SENTENCE_GSV;

      if (!strncmp(term, "GP", 2))
        currentGSVSystem = TINYGPS_GNSS_GPS;
      else if (!strncmp(term, "GL", 2))
        currentGSVSystem = TINYGPS_GNSS_GLONASS;
      else if (!strncmp(term, "GA", 2))
        currentGSVSystem = TINYGPS_GNSS_GALILEO;
      else if (!strncmp(term, "GB", 2) || !strncmp(term, "BD", 2))
        currentGSVSystem = TINYGPS_GNSS_BEIDOU;
      else if (!strncmp(term, "GQ", 2))
        currentGSVSystem = TINYGPS_GNSS_QZSS;
      else if (!strncmp(term, "GN", 2))
        currentGSVSystem = TINYGPS_GNSS_MIXED;
      else
        currentGSVSystem = TINYGPS_GNSS_UNKNOWN;
    }
    else
    {
      curSentenceType = GPS_SENTENCE_OTHER;
      currentGSVSystem = TINYGPS_GNSS_UNKNOWN;
    }

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // Any custom candidates of this sentence type?
    for (customCandidates = customElts; customCandidates != NULL && strcmp(customCandidates->sentenceName, term) < 0; customCandidates = customCandidates->next);
    if (customCandidates != NULL && strcmp(customCandidates->sentenceName, term) > 0)
       customCandidates = NULL;
#endif
    // Serial.printf("%s ENC:%i PAS:%i FAI:%i FIX:%i\n", term, encodedCharCount, passedChecksumCount, failedChecksumCount, sentencesWithFixCount);
    return false;
  }

  // GLL: redundant latitude/longitude, UTC, validity and mode.
  if (curSentenceType == GPS_SENTENCE_GLL) {
    switch (curTermNumber) {
    case 1:
      gllLocation.setNotEmpty(termIsNotEmpty);
      if (termIsNotEmpty) gllLocation.setLatitude(term);
      break;
    case 2:
      gllLocation.newval.lat.negative = termIsNotEmpty && term[0] == 'S';
      break;
    case 3:
      gllLocation.setNotEmpty(termIsNotEmpty);
      if (termIsNotEmpty) gllLocation.setLongitude(term);
      break;
    case 4:
      gllLocation.newval.lng.negative = termIsNotEmpty && term[0] == 'W';
      break;
    case 5:
      gllTime.setNotEmpty(termIsNotEmpty);
      if (termIsNotEmpty) gllTime.setTime(term);
      break;
    case 6:
      pendingGLLStatus = termIsNotEmpty ? term[0] : 'V';
      break;
    case 7:
      pendingGLLMode = termIsNotEmpty ? term[0] : 'N';
      break;
    }
  }

  // ZDA: UTC + complete date + local-zone offset.
  if (curSentenceType == GPS_SENTENCE_ZDA) {
    switch (curTermNumber) {
    case 1:
      zdaTime.setNotEmpty(termIsNotEmpty);
      if (termIsNotEmpty) zdaTime.setTime(term);
      break;
    case 2:
      if (termIsNotEmpty) pendingZDADay = (uint8_t)atoi(term);
      break;
    case 3:
      if (termIsNotEmpty) pendingZDAMonth = (uint8_t)atoi(term);
      break;
    case 4:
      if (termIsNotEmpty) pendingZDAYear = (uint16_t)atoi(term);
      break;
    case 5:
      if (termIsNotEmpty) pendingZDAZoneHours = (int8_t)atoi(term);
      break;
    case 6:
      if (termIsNotEmpty) pendingZDAZoneMinutes = (uint8_t)atoi(term);
      break;
    }
  }

  // L76K ANT output is carried in NMEA TXT:
  // $GPTXT,01,01,01,ANTENNA OK*35
  if (curSentenceType == GPS_SENTENCE_TXT && curTermNumber == 4 && termIsNotEmpty) {
    if (strstr(term, "ANTENNA OK"))
      pendingAntennaStatus = TINYGPS_ANT_OK;
    else if (strstr(term, "ANTENNA OPEN"))
      pendingAntennaStatus = TINYGPS_ANT_OPEN;
    else if (strstr(term, "ANTENNA SHORT"))
      pendingAntennaStatus = TINYGPS_ANT_SHORT;
  }

  // GSA: active satellites used in the navigation solution.
  // GNGSA terms 3..14 contain up to 12 used satellite IDs.
  // NMEA 4.x term 18 is the system ID:
  // 1=GPS, 2=GLONASS, 3=Galileo, 4=BeiDou, 5=QZSS.
  if (curSentenceType == GPS_SENTENCE_GSA) {
    if (curTermNumber == 2 && termIsNotEmpty) {
      pendingGSAFixType = (uint8_t)atoi(term);
    }
    else if (curTermNumber >= 3 && curTermNumber <= 14) {
      if (termIsNotEmpty)
        ++pendingGSAUsed;
    }
    else if (curTermNumber == 15 && termIsNotEmpty) pendingGSAPDOP = (uint16_t)parseDecimal(term);
    else if (curTermNumber == 16 && termIsNotEmpty) pendingGSAHDOP = (uint16_t)parseDecimal(term);
    else if (curTermNumber == 17 && termIsNotEmpty) pendingGSAVDOP = (uint16_t)parseDecimal(term);
    else if (curTermNumber == 18 && termIsNotEmpty) {
      switch (atoi(term)) {
      case 1: pendingGSASystem = TINYGPS_GNSS_GPS; break;
      case 2: pendingGSASystem = TINYGPS_GNSS_GLONASS; break;
      case 3: pendingGSASystem = TINYGPS_GNSS_GALILEO; break;
      case 4: pendingGSASystem = TINYGPS_GNSS_BEIDOU; break;
      case 5: pendingGSASystem = TINYGPS_GNSS_QZSS; break;
      default: pendingGSASystem = TINYGPS_GNSS_UNKNOWN; break;
      }
    }
  }

  // GSV: satellite ID, elevation, azimuth and C/N0.
  // Keep satellites from different constellations in the same array without
  // letting the first GSV sentence of the next constellation erase the others.
  if(curSentenceType == GPS_SENTENCE_GSV) {
    switch(curTermNumber)
    {
      case 2:
      {
        // Message number is 1-based. Some receivers may briefly emit 0 while starting.
        int msgId = atoi(term) - 1;
        if(msgId < 0 || msgId >= TINYGPS_MAX_SATS / 4) {
          trackedSatellitesIndex = -1;
        }
        else {
          // The first GSV sentence of a constellation starts a fresh snapshot
          // for that constellation only. Other GNSS systems stay intact.
          if(msgId == 0) {
            for(size_t i = 0; i < TINYGPS_MAX_SATS; ++i) {
              if(trackedSatellites[i].system == currentGSVSystem) {
                memset(&trackedSatellites[i], 0, sizeof(TinyGPSTrackedSattelites));
              }
            }
          }
          trackedSatellitesIndex = -1;
        }
        break;
      }

      // Satellite ID / PRN / SVID
      case 4:
      case 8:
      case 12:
      case 16:
      {
        trackedSatellitesIndex = -1;

        if(termIsNotEmpty) {
          const uint16_t prn = (uint16_t)atoi(term);
          int freeSlot = -1;

          // Reuse an existing entry for this constellation/SVID if present.
          // Otherwise remember the first empty slot.
          for(size_t i = 0; i < TINYGPS_MAX_SATS; ++i) {
            if(trackedSatellites[i].prn == prn &&
               trackedSatellites[i].system == currentGSVSystem) {
              trackedSatellitesIndex = (int8_t)i;
              break;
            }

            if(freeSlot < 0 && trackedSatellites[i].prn == 0)
              freeSlot = (int)i;
          }

          if(trackedSatellitesIndex < 0 && freeSlot >= 0)
            trackedSatellitesIndex = (int8_t)freeSlot;

          if(trackedSatellitesIndex >= 0) {
            TinyGPSTrackedSattelites &sat = trackedSatellites[trackedSatellitesIndex];
            sat.system = currentGSVSystem;
            sat.prn = prn;

            // Clear fields belonging to this satellite block before they are
            // filled by terms 5/6/7 (or 9/10/11, etc.).
            sat.elevation = 0;
            sat.azimuth = 0;
            sat.strength = 0;
            sat.tracked = false;
          }
        }
        break;
      }

      // Elevation, degrees above horizon
      case 5:
      case 9:
      case 13:
      case 17:
      {
        if(trackedSatellitesIndex >= 0 && termIsNotEmpty)
          trackedSatellites[trackedSatellitesIndex].elevation = (uint8_t)atoi(term);
        break;
      }

      // Azimuth, degrees from true north
      case 6:
      case 10:
      case 14:
      case 18:
      {
        if(trackedSatellitesIndex >= 0 && termIsNotEmpty)
          trackedSatellites[trackedSatellitesIndex].azimuth = (uint16_t)atoi(term);
        break;
      }

      // C/N0 / SNR, dB-Hz
      case 7:
      case 11:
      case 15:
      case 19:
      {
        if(trackedSatellitesIndex >= 0) {
          trackedSatellites[trackedSatellitesIndex].tracked = termIsNotEmpty;
          if(termIsNotEmpty)
            trackedSatellites[trackedSatellitesIndex].strength = (uint8_t)atoi(term);
        }
        break;
      }
    }
  }
  else if (curSentenceType == GPS_SENTENCE_GGA || curSentenceType == GPS_SENTENCE_RMC)
    switch(COMBINE(curSentenceType, curTermNumber))
  {
    case COMBINE(GPS_SENTENCE_RMC, 1): // Time in both sentences
    case COMBINE(GPS_SENTENCE_GGA, 1):
      time.setNotEmpty(termIsNotEmpty);
      time.setTime(term);
      break;
    case COMBINE(GPS_SENTENCE_RMC, 2): // GPRMC validity
      setSentenceHasFix(term[0] == 'A');
      break;
    case COMBINE(GPS_SENTENCE_RMC, 3): // Latitude
    case COMBINE(GPS_SENTENCE_GGA, 2):
      location.setNotEmpty(termIsNotEmpty);
      location.setLatitude(term);
      break;
    case COMBINE(GPS_SENTENCE_RMC, 4): // N/S
    case COMBINE(GPS_SENTENCE_GGA, 3):
      location.newval.lat.negative = term[0] == 'S';
      break;
    case COMBINE(GPS_SENTENCE_RMC, 5): // Longitude
    case COMBINE(GPS_SENTENCE_GGA, 4):
      location.setNotEmpty(termIsNotEmpty);
      location.setLongitude(term);
      break;
    case COMBINE(GPS_SENTENCE_RMC, 6): // E/W
    case COMBINE(GPS_SENTENCE_GGA, 5):
      location.newval.lng.negative = term[0] == 'W';
      break;
    case COMBINE(GPS_SENTENCE_RMC, 7): // Speed (GPRMC)
      speed.setNotEmpty(termIsNotEmpty);
      speed.set(term);
      break;
    case COMBINE(GPS_SENTENCE_RMC, 8): // Course (GPRMC)
      course.setNotEmpty(termIsNotEmpty);
      course.set(term);
      break;
    case COMBINE(GPS_SENTENCE_RMC, 9): // Date (GPRMC)
      date.setNotEmpty(termIsNotEmpty);
      date.setDate(term);
      break;
    case COMBINE(GPS_SENTENCE_GGA, 6): // Fix data (GPGGA)
      setSentenceHasFix(term[0] > '0');
      fixQ = term[0] - '0';
      break;
    case COMBINE(GPS_SENTENCE_GGA, 7): // Satellites used (GPGGA)
      satellites.setNotEmpty(termIsNotEmpty);
      satellites.set(term);
      break;
    case COMBINE(GPS_SENTENCE_GGA, 8): // HDOP
      hdop.setNotEmpty(termIsNotEmpty);
      hdop.set(term);
      break;
    case COMBINE(GPS_SENTENCE_GGA, 9): // Altitude (GPGGA)
      altitude.setNotEmpty(termIsNotEmpty);
      altitude.set(term);
      break;
    case COMBINE(GPS_SENTENCE_GGA, 11): // Height over Geoid
      geoidHeight.setNotEmpty(termIsNotEmpty);
      geoidHeight.set(term);
      break;
  }

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
  // Set custom values as needed
  for (TinyGPSCustom *p = customCandidates; p != NULL && strcmp(p->sentenceName, customCandidates->sentenceName) == 0 && p->termNumber <= curTermNumber; p = p->next)
    if (p->termNumber == curTermNumber)
         p->set(term);
#endif

  return false;
}

/* static */
double TinyGPSPlus::distanceBetween(double lat1, double long1, double lat2, double long2)
{
  // returns distance in meters between two positions, both specified
  // as signed decimal-degrees latitude and longitude. Uses great-circle
  // distance computation for hypothetical sphere of radius 6372795 meters.
  // Because Earth is no exact sphere, rounding errors may be up to 0.5%.
  // Courtesy of Maarten Lamers
  double delta = radians(long1-long2);
  double sdlong = sin(delta);
  double cdlong = cos(delta);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  double slat1 = sin(lat1);
  double clat1 = cos(lat1);
  double slat2 = sin(lat2);
  double clat2 = cos(lat2);
  delta = (clat1 * slat2) - (slat1 * clat2 * cdlong);
  delta = sq(delta);
  delta += sq(clat2 * sdlong);
  delta = sqrt(delta);
  double denom = (slat1 * slat2) + (clat1 * clat2 * cdlong);
  delta = atan2(delta, denom);
  return delta * 6372795;
}

double TinyGPSPlus::courseTo(double lat1, double long1, double lat2, double long2)
{
  // returns course in degrees (North=0, West=270) from position 1 to position 2,
  // both specified as signed decimal-degrees latitude and longitude.
  // Because Earth is no exact sphere, calculated course may be off by a tiny fraction.
  // Courtesy of Maarten Lamers
  double dlon = radians(long2-long1);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  double a1 = sin(dlon) * cos(lat2);
  double a2 = sin(lat1) * cos(lat2) * cos(dlon);
  a2 = cos(lat1) * sin(lat2) - a2;
  a2 = atan2(a1, a2);
  if (a2 < 0.0)
  {
    a2 += TWO_PI;
  }
  return degrees(a2);
}

const char *TinyGPSPlus::cardinal(double course)
{
  static const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int direction = (int)((course + 11.25f) / 22.5f);
  return directions[direction % 16];
}

void TinyGPSLocation::commit(uint32_t timestamp)
{
   createTime = timestamp;
   val.lat = newval.lat;
   val.lng = newval.lng;
   flags |= (FLAG_VALID|FLAG_UPDATED);
}

void TinyGPSLocation::setLatitude(const char *term)
{
   TinyGPSPlus::parseDegrees(term, newval.lat);
}

void TinyGPSLocation::setLongitude(const char *term)
{
   TinyGPSPlus::parseDegrees(term, newval.lng);
}

double TinyGPSLocation::lat()
{
   flags &= (~FLAG_UPDATED);
   double ret = (double)val.lat.deg + ((double)val.lat.billionths / (double)1000000000.0);
   return val.lat.negative ? -ret : ret;
}

double TinyGPSLocation::lng()
{
   flags &= (~FLAG_UPDATED);
   double ret = (double)val.lng.deg + ((double)val.lng.billionths / (double)1000000000.0);
   return val.lng.negative ? -ret : ret;
}

void TinyGPSDate::commit(uint32_t timestamp)
{
   if (!isNotNull) {
      flags = FLAG_DEFAULT;
      return;
   }
   createTime = timestamp;
   val = newval;
   flags |= (FLAG_VALID|FLAG_UPDATED);
   isNotNull = false;
}

void TinyGPSTime::commit(uint32_t timestamp)
{
   if (!isNotNull) {
      flags = FLAG_DEFAULT;
      return;
   }
   createTime = timestamp;
   val = newval;
   flags |= (FLAG_VALID|FLAG_UPDATED);
   isNotNull = false;
}

void TinyGPSTime::setTime(const char *term)
{
   isNotNull = true;
   newval = (uint32_t)TinyGPSPlus::parseDecimal(term);
}

void TinyGPSDate::setDate(const char *term)
{
   isNotNull = true;
   newval = atol(term);
}

uint16_t TinyGPSDate::year()
{
   flags &= (~FLAG_UPDATED);
   uint16_t year = val % 100;
   return year + 2000;
}

uint8_t TinyGPSDate::month()
{
   flags &= (~FLAG_UPDATED);
   return (val / 100) % 100;
}

uint8_t TinyGPSDate::day()
{
   flags &= (~FLAG_UPDATED);
   return val / 10000;
}

uint8_t TinyGPSTime::hour()
{
   flags &= (~FLAG_UPDATED);
   return val / 1000000;
}

uint8_t TinyGPSTime::minute()
{
   flags &= (~FLAG_UPDATED);
   return (val / 10000) % 100;
}

uint8_t TinyGPSTime::second()
{
   flags &= (~FLAG_UPDATED);
   return (val / 100) % 100;
}

uint8_t TinyGPSTime::centisecond()
{
   flags &= (~FLAG_UPDATED);
   return val % 100;
}

void TinyGPSDecimal::commit(uint32_t timestamp)
{
   createTime = timestamp;
   val = newval;
   flags |= (FLAG_VALID|FLAG_UPDATED);
}

void TinyGPSDecimal::set(const char *term)
{
   newval = TinyGPSPlus::parseDecimal(term);
}

void TinyGPSAltitude::set(const char *term)
{
   newval = TinyGPSPlus::parseDecimal(term);
}

void TinyGPSAltitude::commit(uint32_t timestamp)
{
   createTime = timestamp;
   val = newval;
   flags |= (FLAG_VALID|FLAG_UPDATED);
}

void TinyGPSInteger::commit(uint32_t timestamp)
{
   createTime = timestamp;
   val = newval;
   flags |= (FLAG_VALID|FLAG_UPDATED);
}

void TinyGPSInteger::set(const char *term)
{
   newval = atol(term);
}

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
TinyGPSCustom::TinyGPSCustom(TinyGPSPlus &gps, const char *_sentenceName, int _termNumber)
{
   begin(gps, _sentenceName, _termNumber);
}

void TinyGPSCustom::begin(TinyGPSPlus &gps, const char *_sentenceName, int _termNumber)
{
   createTime = millis();
   flags &= (~(FLAG_UPDATED|FLAG_VALID));
   sentenceName = _sentenceName;
   termNumber = _termNumber;
   memset(stagingBuffer, '\0', sizeof(stagingBuffer));
   memset(buffer, '\0', sizeof(buffer));

   // Insert this item into the GPS tree
   gps.insertCustom(this, _sentenceName, _termNumber);
}

void TinyGPSCustom::commit(uint32_t timestamp)
{
   createTime = timestamp;
   strcpy(this->buffer, this->stagingBuffer);
   flags |= (FLAG_VALID|FLAG_UPDATED);
}

void TinyGPSCustom::set(const char *term)
{
   strncpy(this->stagingBuffer, term, sizeof(this->stagingBuffer) - 1);
}

void TinyGPSPlus::insertCustom(TinyGPSCustom *pElt, const char *sentenceName, int termNumber)
{
   TinyGPSCustom **ppelt;

   for (ppelt = &this->customElts; *ppelt != NULL; ppelt = &(*ppelt)->next)
   {
      int cmp = strcmp(sentenceName, (*ppelt)->sentenceName);
      if (cmp < 0 || (cmp == 0 && termNumber < (*ppelt)->termNumber))
         break;
   }

   pElt->next = *ppelt;
   *ppelt = pElt;
}
#endif
