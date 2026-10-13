#pragma once
#include <cstdint>
inline void sensorsInit() {}
inline void sensorsApplyCalibration() {}
inline bool sensorsReadFoot(uint8_t, float&, float&, uint8_t = 1) { return false; }
