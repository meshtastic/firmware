#pragma once
// Stub definitions for disabled features in fakeTec critical build

#ifdef __cplusplus
// AccelerometerThread is already defined in src/motion/AccelerometerThread.h
// We just need to declare the pointer in StubDefinitions.cpp

#if MESHTASTIC_EXCLUDE_GPS || defined(EXCLUDE_GPS_MENU)
// GPS related stubs can go here
#endif
#endif // __cplusplus