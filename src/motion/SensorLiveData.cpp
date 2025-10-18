#include "SensorLiveData.h"

QMI8658LiveData g_qmi8658Live;
QMC6310LiveData g_qmc6310Live;

#if !MESHTASTIC_EXCLUDE_GPS
#include "Fusion/GPSIMUFusion.h"

const GPSIMUFusionData* getGPSIMUFusionData() {
    if (g_gps_imu_fusion.isValid()) {
        return &g_gps_imu_fusion.getFusionData();
    }
    return nullptr;
}
#endif

