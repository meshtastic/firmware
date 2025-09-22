#pragma once
// Stub file for AccelerometerThread when sensors are disabled

class AccelerometerThread {
public:
    AccelerometerThread() {}
    void calibrate(int timeout) {}
    bool isCalibrating() { return false; }
    static bool hasAccelerometer() { return false; }
};

// Null pointer to avoid compilation errors
extern AccelerometerThread *accelerometerThread;