#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <math.h>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <string>

#define PI 3.1415926535897932384626433832795
#define OLC_CODE_LEN 11
#define DEG_CONVERT (180 / PI)

// GeoCoord structs/classes
// A struct to hold the data for a DMS coordinate.
struct DMS {
    uint8_t latDeg;
    uint8_t latMin;
    uint32_t latSec;
    char latCP;
    uint8_t lonDeg;
    uint8_t lonMin;
    uint32_t lonSec;
    char lonCP;
};

// A struct to hold the data for a UTM coordinate, this is also used when creating an MGRS coordinate.
struct UTM {
    uint8_t zone;
    char band;
    uint32_t easting;
    uint32_t northing;
};

// A struct to hold the data for a MGRS coordinate.
struct MGRS {
    uint8_t zone;
    char band;
    char east100k;
    char north100k;
    uint32_t easting;
    uint32_t northing;
};

// A struct to hold the data for a OSGR coordinate
struct OSGR {
    char e100k;
    char n100k;
    uint32_t easting;
    uint32_t northing;
};

// A struct to hold the data for a OLC coordinate
struct OLC {
    char code[OLC_CODE_LEN + 1]; // +1 for null termination
};

class GeoCoord
{
  private:
    int32_t _latitude = 0;
    int32_t _longitude = 0;
    int32_t _altitude = 0;

    DMS _dms = {};
    // Computed lazily on first access via ensure*() below; mutable so const getters can populate
    // them on demand.
    mutable UTM _utm = {};
    mutable MGRS _mgrs = {};
    mutable OSGR _osgr = {};
    mutable OLC _olc = {};
    mutable bool _utmValid = false;
    mutable bool _mgrsValid = false;
    mutable bool _osgrValid = false;
    mutable bool _olcValid = false;

    bool _dirty = true;

    void setCoords();
    void ensureUTM() const;
    void ensureMGRS() const;
    void ensureOSGR() const;
    void ensureOLC() const;

  public:
    GeoCoord();
    GeoCoord(int32_t lat, int32_t lon, int32_t alt);
    GeoCoord(double lat, double lon, int32_t alt);
    GeoCoord(float lat, float lon, int32_t alt);

    void updateCoords(const int32_t lat, const int32_t lon, const int32_t alt);
    void updateCoords(const double lat, const double lon, const int32_t alt);
    void updateCoords(const float lat, const float lon, const int32_t alt);

    // Conversions
    static void latLongToDMS(const double lat, const double lon, DMS &dms);
    static void latLongToUTM(const double lat, const double lon, UTM &utm);
    static void latLongToMGRS(const double lat, const double lon, MGRS &mgrs);
    static void latLongToOSGR(const double lat, const double lon, OSGR &osgr);
    static void latLongToOLC(const double lat, const double lon, OLC &olc);
    static void convertWGS84ToOSGB36(const double lat, const double lon, double &osgb_Latitude, double &osgb_Longitude);
    static float latLongToMeter(double lat_a, double lng_a, double lat_b, double lng_b);
    static float bearing(double lat1, double lon1, double lat2, double lon2);
    static float rangeMetersToRadians(double range_meters);
    static unsigned int bearingToDegrees(const char *bearing);
    static const char *degreesToBearing(unsigned int degrees);

    // Raises a number to an exponent, handling negative exponents.
    static double pow_neg(double base, double exponent);
    static double toRadians(double deg);
    static double toDegrees(double r);

    // Point to point conversions
    std::shared_ptr<GeoCoord> pointAtDistance(double bearing, double range);

    // Lat lon alt getters
    int32_t getLatitude() const { return _latitude; }
    int32_t getLongitude() const { return _longitude; }
    int32_t getAltitude() const { return _altitude; }

    // DMS getters
    uint8_t getDMSLatDeg() const { return _dms.latDeg; }
    uint8_t getDMSLatMin() const { return _dms.latMin; }
    uint32_t getDMSLatSec() const { return _dms.latSec; }
    char getDMSLatCP() const { return _dms.latCP; }
    uint8_t getDMSLonDeg() const { return _dms.lonDeg; }
    uint8_t getDMSLonMin() const { return _dms.lonMin; }
    uint32_t getDMSLonSec() const { return _dms.lonSec; }
    char getDMSLonCP() const { return _dms.lonCP; }

    // UTM getters (computed on first access - see ensureUTM())
    uint8_t getUTMZone() const
    {
        ensureUTM();
        return _utm.zone;
    }
    char getUTMBand() const
    {
        ensureUTM();
        return _utm.band;
    }
    uint32_t getUTMEasting() const
    {
        ensureUTM();
        return _utm.easting;
    }
    uint32_t getUTMNorthing() const
    {
        ensureUTM();
        return _utm.northing;
    }

    // MGRS getters (computed on first access - see ensureMGRS())
    uint8_t getMGRSZone() const
    {
        ensureMGRS();
        return _mgrs.zone;
    }
    char getMGRSBand() const
    {
        ensureMGRS();
        return _mgrs.band;
    }
    char getMGRSEast100k() const
    {
        ensureMGRS();
        return _mgrs.east100k;
    }
    char getMGRSNorth100k() const
    {
        ensureMGRS();
        return _mgrs.north100k;
    }
    uint32_t getMGRSEasting() const
    {
        ensureMGRS();
        return _mgrs.easting;
    }
    uint32_t getMGRSNorthing() const
    {
        ensureMGRS();
        return _mgrs.northing;
    }

    // OSGR getters (computed on first access - see ensureOSGR())
    char getOSGRE100k() const
    {
        ensureOSGR();
        return _osgr.e100k;
    }
    char getOSGRN100k() const
    {
        ensureOSGR();
        return _osgr.n100k;
    }
    uint32_t getOSGREasting() const
    {
        ensureOSGR();
        return _osgr.easting;
    }
    uint32_t getOSGRNorthing() const
    {
        ensureOSGR();
        return _osgr.northing;
    }

    // OLC getter (computed on first access - see ensureOLC())
    void getOLCCode(char *code)
    {
        ensureOLC();
        strncpy(code, _olc.code, OLC_CODE_LEN + 1); // +1 for null termination
    }
};