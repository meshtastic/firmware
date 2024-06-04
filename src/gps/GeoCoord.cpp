#include "GeoCoord.h"

GeoCoord::GeoCoord()
{
    _dirty = true;
}

GeoCoord::GeoCoord(int32_t lat, int32_t lon, int32_t alt) : _latitude(lat), _longitude(lon), _altitude(alt)
{
    GeoCoord::setCoords();
}

GeoCoord::GeoCoord(float lat, float lon, int32_t alt) : _altitude(alt)
{
    // Change decimial representation to int32_t. I.e., 12.345 becomes 123450000
    _latitude = int32_t(lat * 1e+7);
    _longitude = int32_t(lon * 1e+7);
    GeoCoord::setCoords();
}

GeoCoord::GeoCoord(double lat, double lon, int32_t alt) : _altitude(alt)
{
    // Change decimial representation to int32_t. I.e., 12.345 becomes 123450000
    _latitude = int32_t(lat * 1e+7);
    _longitude = int32_t(lon * 1e+7);
    GeoCoord::setCoords();
}

// Initialize all the coordinate systems
void GeoCoord::setCoords()
{
    double lat = _latitude * 1e-7;
    double lon = _longitude * 1e-7;
    GeoCoord::latLongToDMS(lat, lon, _dms);
    GeoCoord::latLongToUTM(lat, lon, _utm);
    GeoCoord::latLongToMGRS(lat, lon, _mgrs);
    GeoCoord::latLongToOSGR(lat, lon, _osgr);
    GeoCoord::latLongToOLC(lat, lon, _olc);
    _dirty = false;
}

void GeoCoord::updateCoords(int32_t lat, int32_t lon, int32_t alt)
{
    // If marked dirty or new coordinates
    if (_dirty || _latitude != lat || _longitude != lon || _altitude != alt) {
        _dirty = true;
        _latitude = lat;
        _longitude = lon;
        _altitude = alt;
        setCoords();
    }
}

void GeoCoord::updateCoords(const double lat, const double lon, const int32_t alt)
{
    int32_t iLat = lat * 1e+7;
    int32_t iLon = lon * 1e+7;
    // If marked dirty or new coordinates
    if (_dirty || _latitude != iLat || _longitude != iLon || _altitude != alt) {
        _dirty = true;
        _latitude = iLat;
        _longitude = iLon;
        _altitude = alt;
        setCoords();
    }
}

void GeoCoord::updateCoords(const float lat, const float lon, const int32_t alt)
{
    int32_t iLat = lat * 1e+7;
    int32_t iLon = lon * 1e+7;
    // If marked dirty or new coordinates
    if (_dirty || _latitude != iLat || _longitude != iLon || _altitude != alt) {
        _dirty = true;
        _latitude = iLat;
        _longitude = iLon;
        _altitude = alt;
        setCoords();
    }
}

/**
 * Converts lat long coordinates from decimal degrees to degrees minutes seconds format.
 * DD°MM'SS"C DDD°MM'SS"C
 */
void GeoCoord::latLongToDMS(const double lat, const double lon, DMS &dms)
{
    if (lat < 0)
        dms.latCP = 'S';
    else
        dms.latCP = 'N';

    double latDeg = lat;

    if (lat < 0)
        latDeg = latDeg * -1;

    dms.latDeg = floor(latDeg);
    double latMin = (latDeg - dms.latDeg) * 60;
    dms.latMin = floor(latMin);
    dms.latSec = (latMin - dms.latMin) * 60;

    if (lon < 0)
        dms.lonCP = 'W';
    else
        dms.lonCP = 'E';

    double lonDeg = lon;

    if (lon < 0)
        lonDeg = lonDeg * -1;

    dms.lonDeg = floor(lonDeg);
    double lonMin = (lonDeg - dms.lonDeg) * 60;
    dms.lonMin = floor(lonMin);
    dms.lonSec = (lonMin - dms.lonMin) * 60;
}

/**
 * Converts lat long coordinates to UTM.
 * based on this: https://github.com/walvok/LatLonToUTM/blob/master/latlon_utm.ino
 */
void GeoCoord::latLongToUTM(const double lat, const double lon, UTM &utm)
{

    const std::string latBands = "CDEFGHJKLMNPQRSTUVWXX";
    utm.zone = int((lon + 180) / 6 + 1);
    utm.band = latBands[int(lat / 8 + 10)];
    double a = 6378137;                                                // WGS84 - equatorial radius
    double k0 = 0.9996;                                                // UTM point scale on the central meridian
    double eccSquared = 0.00669438;                                    // eccentricity squared
    double lonTemp = (lon + 180) - int((lon + 180) / 360) * 360 - 180; // Make sure the longitude is between -180.00 .. 179.9
    double latRad = toRadians(lat);
    double lonRad = toRadians(lonTemp);

    // Special Zones for Norway and Svalbard
    if (lat >= 56.0 && lat < 64.0 && lonTemp >= 3.0 && lonTemp < 12.0) // Norway
        utm.zone = 32;
    if (lat >= 72.0 && lat < 84.0) { // Svalbard
        if (lonTemp >= 0.0 && lonTemp < 9.0)
            utm.zone = 31;
        else if (lonTemp >= 9.0 && lonTemp < 21.0)
            utm.zone = 33;
        else if (lonTemp >= 21.0 && lonTemp < 33.0)
            utm.zone = 35;
        else if (lonTemp >= 33.0 && lonTemp < 42.0)
            utm.zone = 37;
    }

    double lonOrigin = (utm.zone - 1) * 6 - 180 + 3; // puts origin in middle of zone
    double lonOriginRad = toRadians(lonOrigin);
    double eccPrimeSquared = (eccSquared) / (1 - eccSquared);
    double N = a / sqrt(1 - eccSquared * sin(latRad) * sin(latRad));
    double T = tan(latRad) * tan(latRad);
    double C = eccPrimeSquared * cos(latRad) * cos(latRad);
    double A = cos(latRad) * (lonRad - lonOriginRad);
    double M =
        a * ((1 - eccSquared / 4 - 3 * eccSquared * eccSquared / 64 - 5 * eccSquared * eccSquared * eccSquared / 256) * latRad -
             (3 * eccSquared / 8 + 3 * eccSquared * eccSquared / 32 + 45 * eccSquared * eccSquared * eccSquared / 1024) *
                 sin(2 * latRad) +
             (15 * eccSquared * eccSquared / 256 + 45 * eccSquared * eccSquared * eccSquared / 1024) * sin(4 * latRad) -
             (35 * eccSquared * eccSquared * eccSquared / 3072) * sin(6 * latRad));
    utm.easting = (double)(k0 * N *
                               (A + (1 - T + C) * pow(A, 3) / 6 +
                                (5 - 18 * T + T * T + 72 * C - 58 * eccPrimeSquared) * A * A * A * A * A / 120) +
                           500000.0);
    utm.northing =
        (double)(k0 * (M + N * tan(latRad) *
                               (A * A / 2 + (5 - T + 9 * C + 4 * C * C) * A * A * A * A / 24 +
                                (61 - 58 * T + T * T + 600 * C - 330 * eccPrimeSquared) * A * A * A * A * A * A / 720)));

    if (lat < 0)
        utm.northing += 10000000.0; // 10000000 meter offset for southern hemisphere
}

// Converts lat long coordinates to an MGRS.
void GeoCoord::latLongToMGRS(const double lat, const double lon, MGRS &mgrs)
{
    const std::string e100kLetters[3] = {"ABCDEFGH", "JKLMNPQR", "STUVWXYZ"};
    const std::string n100kLetters[2] = {"ABCDEFGHJKLMNPQRSTUV", "FGHJKLMNPQRSTUVABCDE"};
    UTM utm;
    latLongToUTM(lat, lon, utm);
    mgrs.zone = utm.zone;
    mgrs.band = utm.band;
    double col = floor(utm.easting / 100000);
    mgrs.east100k = e100kLetters[(mgrs.zone - 1) % 3][col - 1];
    double row = (int32_t)floor(utm.northing / 100000.0) % 20;
    mgrs.north100k = n100kLetters[(mgrs.zone - 1) % 2][row];
    mgrs.easting = (int32_t)utm.easting % 100000;
    mgrs.northing = (int32_t)utm.northing % 100000;
}

/**
 * Converts lat long coordinates to Ordnance Survey Grid Reference (UK National Grid Ref).
 * Based on: https://www.movable-type.co.uk/scripts/latlong-os-gridref.html
 */
void GeoCoord::latLongToOSGR(const double lat, const double lon, OSGR &osgr)
{
    const char letter[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ"; // No 'I' in OSGR
    double a = 6377563.396;                            // Airy 1830 semi-major axis
    double b = 6356256.909;                            // Airy 1830 semi-minor axis
    double f0 = 0.9996012717;                          // National Grid point scale factor on the central meridian
    double phi0 = toRadians(49);
    double lambda0 = toRadians(-2);
    double n0 = -100000;
    double e0 = 400000;
    double e2 = 1 - (b * b) / (a * a); // eccentricity squared
    double n = (a - b) / (a + b);

    double osgb_Latitude;
    double osgb_Longitude;
    convertWGS84ToOSGB36(lat, lon, osgb_Latitude, osgb_Longitude);
    double phi = osgb_Latitude;     // already in radians
    double lambda = osgb_Longitude; // already in radians
    double v = a * f0 / sqrt(1 - e2 * sin(phi) * sin(phi));
    double rho = a * f0 * (1 - e2) / pow(1 - e2 * sin(phi) * sin(phi), 1.5);
    double eta2 = v / rho - 1;
    double mA = (1 + n + (5 / 4) * n * n + (5 / 4) * n * n * n) * (phi - phi0);
    double mB = (3 * n + 3 * n * n + (21 / 8) * n * n * n) * sin(phi - phi0) * cos(phi + phi0);
    // loss of precision in mC & mD due to floating point rounding can cause inaccuracy of northing by a few meters
    double mC = (15 / 8 * n * n + 15 / 8 * n * n * n) * sin(2 * (phi - phi0)) * cos(2 * (phi + phi0));
    double mD = (35 / 24) * n * n * n * sin(3 * (phi - phi0)) * cos(3 * (phi + phi0));
    double m = b * f0 * (mA - mB + mC - mD);

    double cos3Phi = cos(phi) * cos(phi) * cos(phi);
    double cos5Phi = cos3Phi * cos(phi) * cos(phi);
    double tan2Phi = tan(phi) * tan(phi);
    double tan4Phi = tan2Phi * tan2Phi;
    double I = m + n0;
    double II = (v / 2) * sin(phi) * cos(phi);
    double III = (v / 24) * sin(phi) * cos3Phi * (5 - tan2Phi + 9 * eta2);
    double IIIA = (v / 720) * sin(phi) * cos5Phi * (61 - 58 * tan2Phi + tan4Phi);
    double IV = v * cos(phi);
    double V = (v / 6) * cos3Phi * (v / rho - tan2Phi);
    double VI = (v / 120) * cos5Phi * (5 - 18 * tan2Phi + tan4Phi + 14 * eta2 - 58 * tan2Phi * eta2);

    double deltaLambda = lambda - lambda0;
    double deltaLambda2 = deltaLambda * deltaLambda;
    double northing =
        I + II * deltaLambda2 + III * deltaLambda2 * deltaLambda2 + IIIA * deltaLambda2 * deltaLambda2 * deltaLambda2;
    double easting = e0 + IV * deltaLambda + V * deltaLambda2 * deltaLambda + VI * deltaLambda2 * deltaLambda2 * deltaLambda;

    if (easting < 0 || easting > 700000 || northing < 0 || northing > 1300000) // Check if out of boundaries
        osgr = {'I', 'I', 0, 0};
    else {
        uint32_t e100k = floor(easting / 100000);
        uint32_t n100k = floor(northing / 100000);
        int8_t l1 = (19 - n100k) - (19 - n100k) % 5 + floor((e100k + 10) / 5);
        int8_t l2 = (19 - n100k) * 5 % 25 + e100k % 5;
        osgr.e100k = letter[l1];
        osgr.n100k = letter[l2];
        osgr.easting = floor((int)easting % 100000);
        osgr.northing = floor((int)northing % 100000);
    }
}

/**
 * Converts lat long coordinates to Open Location Code.
 * Based on: https://github.com/google/open-location-code/blob/main/c/src/olc.c
 */
void GeoCoord::latLongToOLC(double lat, double lon, OLC &olc)
{
    char tempCode[] = "1234567890abc";
    const char kAlphabet[] = "23456789CFGHJMPQRVWX";
    double latitude;
    double longitude = lon;
    double latitude_degrees = std::min(90.0, std::max(-90.0, lat));

    if (latitude_degrees < 90) // Check latitude less than lat max
        latitude = latitude_degrees;
    else {
        double precision;
        if (OLC_CODE_LEN <= 10)
            precision = pow_neg(20, floor((OLC_CODE_LEN / -2) + 2));
        else
            precision = pow_neg(20, -3) / pow(5, OLC_CODE_LEN - 10);
        latitude = latitude_degrees - precision / 2;
    }
    while (longitude < -180) // Normalize longitude
        longitude += 360;
    while (longitude >= 180)
        longitude -= 360;
    int64_t lat_val = 90 * 2.5e7;
    int64_t lng_val = 180 * 8.192e6;
    lat_val += latitude * 2.5e7;
    lng_val += longitude * 8.192e6;
    size_t pos = OLC_CODE_LEN;

    if (OLC_CODE_LEN > 10) { // Compute grid part of code if needed
        for (size_t i = 0; i < 5; i++) {
            int lat_digit = lat_val % 5;
            int lng_digit = lng_val % 4;
            int ndx = lat_digit * 4 + lng_digit;
            tempCode[pos--] = kAlphabet[ndx];
            lat_val /= 5;
            lng_val /= 4;
        }
    } else {
        lat_val /= pow(5, 5);
        lng_val /= pow(4, 5);
    }

    pos = 10;

    for (size_t i = 0; i < 5; i++) { // Compute pair section of code
        int lat_ndx = lat_val % 20;
        int lng_ndx = lng_val % 20;
        tempCode[pos--] = kAlphabet[lng_ndx];
        tempCode[pos--] = kAlphabet[lat_ndx];
        lat_val /= 20;
        lng_val /= 20;

        if (i == 0)
            tempCode[pos--] = '+';
    }

    if (OLC_CODE_LEN < 9) { // Add padding if needed
        for (size_t i = OLC_CODE_LEN; i < 9; i++)
            tempCode[i] = '0';
        tempCode[9] = '+';
    }

    size_t char_count = OLC_CODE_LEN;
    if (10 > char_count) {
        char_count = 10;
    }
    for (size_t i = 0; i < char_count; i++) {
        olc.code[i] = tempCode[i];
    }
    olc.code[char_count] = '\0';
}

// Converts the coordinate in WGS84 datum to the OSGB36 datum.
void GeoCoord::convertWGS84ToOSGB36(const double lat, const double lon, double &osgb_Latitude, double &osgb_Longitude)
{
    // Convert lat long to cartesian
    double phi = toRadians(lat);
    double lambda = toRadians(lon);
    double h = 0.0;                  // No OSTN height data used, some loss of accuracy (up to 5m)
    double wgsA = 6378137;           // WGS84 datum semi major axis
    double wgsF = 1 / 298.257223563; // WGS84 datum flattening
    double ecc = 2 * wgsF - wgsF * wgsF;
    double vee = wgsA / sqrt(1 - ecc * pow(sin(phi), 2));
    double wgsX = (vee + h) * cos(phi) * cos(lambda);
    double wgsY = (vee + h) * cos(phi) * sin(lambda);
    double wgsZ = ((1 - ecc) * vee + h) * sin(phi);

    // 7-parameter Helmert transform
    double tx = -446.448;                  // x shift in meters
    double ty = 125.157;                   // y shift in meters
    double tz = -542.060;                  // z shift in meters
    double s = 20.4894 / 1e6 + 1;          // scale normalized parts per million to (s + 1)
    double rx = toRadians(-0.1502 / 3600); // x rotation normalize arcseconds to radians
    double ry = toRadians(-0.2470 / 3600); // y rotation normalize arcseconds to radians
    double rz = toRadians(-0.8421 / 3600); // z rotation normalize arcseconds to radians
    double osgbX = tx + wgsX * s - wgsY * rz + wgsZ * ry;
    double osgbY = ty + wgsX * rz + wgsY * s - wgsZ * rx;
    double osgbZ = tz - wgsX * ry + wgsY * rx + wgsZ * s;

    // Convert cartesian to lat long
    double airyA = 6377563.396;     // Airy1830 datum semi major axis
    double airyB = 6356256.909;     // Airy1830 datum semi minor axis
    double airyF = 1 / 299.3249646; // Airy1830 datum flattening
    double airyEcc = 2 * airyF - airyF * airyF;
    double airyEcc2 = airyEcc / (1 - airyEcc);
    double p = sqrt(osgbX * osgbX + osgbY * osgbY);
    double R = sqrt(p * p + osgbZ * osgbZ);
    double tanBeta = (airyB * osgbZ) / (airyA * p) * (1 + airyEcc2 * airyB / R);
    double sinBeta = tanBeta / sqrt(1 + tanBeta * tanBeta);
    double cosBeta = sinBeta / tanBeta;
    osgb_Latitude = atan2(osgbZ + airyEcc2 * airyB * sinBeta * sinBeta * sinBeta,
                          p - airyEcc * airyA * cosBeta * cosBeta * cosBeta); // leave in radians
    osgb_Longitude = atan2(osgbY, osgbX);                                     // leave in radians
    // osgb height = p*cos(osgb.latitude) + osgbZ*sin(osgb.latitude) -
    //(airyA*airyA/(airyA / sqrt(1 - airyEcc*sin(osgb.latitude)*sin(osgb.latitude)))); // Not used, no OSTN data
}

/// Ported from my old java code, returns distance in meters along the globe
/// surface (by Haversine formula)
float GeoCoord::latLongToMeter(double lat_a, double lng_a, double lat_b, double lng_b)
{
    // Don't do math if the points are the same
    if (lat_a == lat_b && lng_a == lng_b)
        return 0.0;

    double a1 = lat_a / DEG_CONVERT;
    double a2 = lng_a / DEG_CONVERT;
    double b1 = lat_b / DEG_CONVERT;
    double b2 = lng_b / DEG_CONVERT;
    double cos_b1 = cos(b1);
    double cos_a1 = cos(a1);
    double t1 = cos_a1 * cos(a2) * cos_b1 * cos(b2);
    double t2 = cos_a1 * sin(a2) * cos_b1 * sin(b2);
    double t3 = sin(a1) * sin(b1);
    double tt = acos(t1 + t2 + t3);
    if (std::isnan(tt))
        tt = 0.0; // Must have been the same point?

    return (float)(6366000 * tt);
}

/**
 * Computes the bearing in degrees between two points on Earth.  Ported from my
 * old Gaggle android app.
 *
 * @param lat1
 * Latitude of the first point
 * @param lon1
 * Longitude of the first point
 * @param lat2
 * Latitude of the second point
 * @param lon2
 * Longitude of the second point
 * @return Bearing from point 1 to point 2 in radians. A value of 0 means due
 * north.
 */
float GeoCoord::bearing(double lat1, double lon1, double lat2, double lon2)
{
    double lat1Rad = toRadians(lat1);
    double lat2Rad = toRadians(lat2);
    double deltaLonRad = toRadians(lon2 - lon1);
    double y = sin(deltaLonRad) * cos(lat2Rad);
    double x = cos(lat1Rad) * sin(lat2Rad) - (sin(lat1Rad) * cos(lat2Rad) * cos(deltaLonRad));
    return atan2(y, x);
}

/**
 * Ported from http://www.edwilliams.org/avform147.htm#Intro
 * @brief Convert from meters to range in radians on a great circle
 * @param range_meters
 * The range in meters
 * @return range in radians on a great circle
 */
float GeoCoord::rangeMetersToRadians(double range_meters)
{
    // 1 nm is 1852 meters
    double distance_nm = range_meters * 1852;
    return (PI / (180 * 60)) * distance_nm;
}

/**
 * Ported from http://www.edwilliams.org/avform147.htm#Intro
 * @brief Convert from radians to range in meters on a great circle
 * @param range_radians
 * The range in radians
 * @return Range in meters on a great circle
 */
float GeoCoord::rangeRadiansToMeters(double range_radians)
{
    double distance_nm = ((180 * 60) / PI) * range_radians;
    // 1 meter is 0.000539957 nm
    return distance_nm * 0.000539957;
}

// Find distance from point to passed in point
int32_t GeoCoord::distanceTo(const GeoCoord &pointB)
{
    return latLongToMeter(this->getLatitude() * 1e-7, this->getLongitude() * 1e-7, pointB.getLatitude() * 1e-7,
                          pointB.getLongitude() * 1e-7);
}

// Find bearing from point to passed in point
int32_t GeoCoord::bearingTo(const GeoCoord &pointB)
{
    return bearing(this->getLatitude() * 1e-7, this->getLongitude() * 1e-7, pointB.getLatitude() * 1e-7,
                   pointB.getLongitude() * 1e-7);
}

/**
 * Create a new point bassed on the passed in poin
 * Ported from http://www.edwilliams.org/avform147.htm#LL
 * @param bearing
 * The bearing in raidans
 * @param range_meters
 * range in meters
 * @return GeoCoord object of point at bearing and range from initial point
 */
std::shared_ptr<GeoCoord> GeoCoord::pointAtDistance(double bearing, double range_meters)
{
    double range_radians = rangeMetersToRadians(range_meters);
    double lat1 = this->getLatitude() * 1e-7;
    double lon1 = this->getLongitude() * 1e-7;
    double lat = asin(sin(lat1) * cos(range_radians) + cos(lat1) * sin(range_radians) * cos(bearing));
    double dlon = atan2(sin(bearing) * sin(range_radians) * cos(lat1), cos(range_radians) - sin(lat1) * sin(lat));
    double lon = fmod(lon1 - dlon + PI, 2 * PI) - PI;

    return std::make_shared<GeoCoord>(double(lat), double(lon), this->getAltitude());
}

/**
 * Convert bearing to degrees
 * @param bearing
 * The bearing in string format
 * @return Bearing in degrees
 */
uint GeoCoord::bearingToDegrees(const char *bearing)
{
    if (strcmp(bearing, "N") == 0)
        return 0;
    else if (strcmp(bearing, "NNE") == 0)
        return 22;
    else if (strcmp(bearing, "NE") == 0)
        return 45;
    else if (strcmp(bearing, "ENE") == 0)
        return 67;
    else if (strcmp(bearing, "E") == 0)
        return 90;
    else if (strcmp(bearing, "ESE") == 0)
        return 112;
    else if (strcmp(bearing, "SE") == 0)
        return 135;
    else if (strcmp(bearing, "SSE") == 0)
        return 157;
    else if (strcmp(bearing, "S") == 0)
        return 180;
    else if (strcmp(bearing, "SSW") == 0)
        return 202;
    else if (strcmp(bearing, "SW") == 0)
        return 225;
    else if (strcmp(bearing, "WSW") == 0)
        return 247;
    else if (strcmp(bearing, "W") == 0)
        return 270;
    else if (strcmp(bearing, "WNW") == 0)
        return 292;
    else if (strcmp(bearing, "NW") == 0)
        return 315;
    else if (strcmp(bearing, "NNW") == 0)
        return 337;
    else
        return 0;
}

/**
 * Convert bearing to string
 * @param degrees
 * The bearing in degrees
 * @return Bearing in string format
 */
const char *GeoCoord::degreesToBearing(uint degrees)
{
    if (degrees >= 348 || degrees < 11)
        return "N";
    else if (degrees >= 11 && degrees < 34)
        return "NNE";
    else if (degrees >= 34 && degrees < 56)
        return "NE";
    else if (degrees >= 56 && degrees < 79)
        return "ENE";
    else if (degrees >= 79 && degrees < 101)
        return "E";
    else if (degrees >= 101 && degrees < 124)
        return "ESE";
    else if (degrees >= 124 && degrees < 146)
        return "SE";
    else if (degrees >= 146 && degrees < 169)
        return "SSE";
    else if (degrees >= 169 && degrees < 191)
        return "S";
    else if (degrees >= 191 && degrees < 214)
        return "SSW";
    else if (degrees >= 214 && degrees < 236)
        return "SW";
    else if (degrees >= 236 && degrees < 259)
        return "WSW";
    else if (degrees >= 259 && degrees < 281)
        return "W";
    else if (degrees >= 281 && degrees < 304)
        return "WNW";
    else if (degrees >= 304 && degrees < 326)
        return "NW";
    else if (degrees >= 326 && degrees < 348)
        return "NNW";
    else
        return "N";
}
