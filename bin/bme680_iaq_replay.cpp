// Replays a captured BME680 CSV trace (gas_ohms,rh[,bsec_iaq]) through
// BME680IaqEstimator for offline tuning. See docs/bme680_iaq_replay.md.

#include "modules/Telemetry/Sensor/BME680IaqEstimator.h"

#include <cmath>
#include <cstdio>

namespace
{
// Same buckets the device UI uses (EnvironmentTelemetry drawFrame)
int band(int iaq)
{
    if (iaq <= 25)
        return 0; // Excellent
    if (iaq <= 50)
        return 1; // Good
    if (iaq <= 100)
        return 2; // Moderate
    if (iaq <= 150)
        return 3; // Poor
    if (iaq <= 200)
        return 4; // Unhealthy
    if (iaq <= 300)
        return 5; // Very Unhealthy
    return 6;     // Hazardous
}
} // namespace

int main(int argc, char **argv)
{
    FILE *in = stdin;
    if (argc > 1) {
        in = fopen(argv[1], "r");
        if (!in) {
            fprintf(stderr, "cannot open %s\n", argv[1]);
            return 1;
        }
    }

    BME680IaqEstimator est;
    char line[256];
    long lineNo = 0, n = 0, skipped = 0, produced = 0, compared = 0, bandHits = 0;
    double absErrSum = 0;

    printf("n,gas_ohms,rh,est_iaq,bsec_iaq\n");
    while (fgets(line, sizeof(line), in)) {
        lineNo++;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        float gas, rh, bsec = NAN;
        int fields = sscanf(line, "%f,%f,%f", &gas, &rh, &bsec);
        if (fields < 2) {
            // Tolerate one header row silently; anything else malformed is
            // reported so a damaged trace can't produce a quiet, biased summary
            if (lineNo > 1) {
                skipped++;
                fprintf(stderr, "skipping malformed line %ld: %s", lineNo, line);
            }
            continue;
        }
        n++;
        uint16_t iaq;
        bool got = est.update(gas, rh, &iaq);
        bool haveBsec = fields >= 3 && std::isfinite(bsec);

        printf("%ld,%.0f,%.2f,", n, gas, rh);
        if (got)
            printf("%u", (unsigned)iaq);
        if (haveBsec)
            printf(",%.0f\n", bsec);
        else
            printf(",\n");

        if (got) {
            produced++;
            if (haveBsec) {
                compared++;
                absErrSum += std::fabs((double)iaq - (double)bsec);
                if (band(iaq) == band((int)std::lround(bsec)))
                    bandHits++;
            }
        }
    }
    if (ferror(in)) {
        fprintf(stderr, "input read error at line %ld\n", lineNo);
        if (in != stdin)
            fclose(in);
        return 1;
    }

    fprintf(stderr, "samples: %ld, estimator outputs: %ld, malformed lines skipped: %ld\n", n, produced, skipped);
    if (compared) {
        fprintf(stderr, "vs BSEC (%ld comparable): mean abs error %.1f IAQ points, band agreement %.1f%%\n", compared,
                absErrSum / compared, 100.0 * bandHits / compared);
    }
    if (in != stdin)
        fclose(in);
    return 0;
}
