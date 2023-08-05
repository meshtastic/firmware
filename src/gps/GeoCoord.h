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

// Helper functions
// Raises a number to an exponent, handling negative exponents.
static inline double pow_neg(double base, double exponent)
{
    if (exponent == 0) {
        return 1;
    } else if (exponent > 0) {
        return pow(base, exponent);
    }
    return 1 / pow(base, -exponent);
}

static inline double toRadians(double deg)
{
    return deg * PI / 180;
}

static inline double toDegrees(double r)
{
    return r * 180 / PI;
}

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
    UTM _utm = {};
    MGRS _mgrs = {};
    OSGR _osgr = {};
    OLC _olc = {};

    bool _dirty = true;

    void setCoords();

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
    static float rangeRadiansToMeters(double range_radians);
    static float rangeMetersToRadians(double range_meters);

    // Point to point conversions
    int32_t distanceTo(const GeoCoord &pointB);
    int32_t bearingTo(const GeoCoord &pointB);
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

    // UTM getters
    uint8_t getUTMZone() const { return _utm.zone; }
    char getUTMBand() const { return _utm.band; }
    uint32_t getUTMEasting() const { return _utm.easting; }
    uint32_t getUTMNorthing() const { return _utm.northing; }

    // MGRS getters
    uint8_t getMGRSZone() const { return _mgrs.zone; }
    char getMGRSBand() const { return _mgrs.band; }
    char getMGRSEast100k() const { return _mgrs.east100k; }
    char getMGRSNorth100k() const { return _mgrs.north100k; }
    uint32_t getMGRSEasting() const { return _mgrs.easting; }
    uint32_t getMGRSNorthing() const { return _mgrs.northing; }

    // OSGR getters
    char getOSGRE100k() const { return _osgr.e100k; }
    char getOSGRN100k() const { return _osgr.n100k; }
    uint32_t getOSGREasting() const { return _osgr.easting; }
    uint32_t getOSGRNorthing() const { return _osgr.northing; }

    // OLC getter
    void getOLCCode(char *code) { strncpy(code, _olc.code, OLC_CODE_LEN + 1); } // +1 for null termination
};
