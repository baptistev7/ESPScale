// Bouchon de include/config.hpp : seules les constantes lues par webconfig.cpp
// (FW_VERSION est passé à la compilation depuis le vrai config.hpp).
#pragma once
#include <cstdint>
#ifndef FW_VERSION
#define FW_VERSION "?"
#endif
#define PORTAL_TIMEOUT_MIN 10
#define TANK_FULL_KG 670.0
#define WIFI_HOSTNAME "pellet-scale"
