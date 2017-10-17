#ifndef _SUN_MOON_H
#define _SUN_MOON_H

#include "TimeLib.h"
time_t suntime(time_t time, double lat, double lon, bool sunrise, int tz);

#endif
