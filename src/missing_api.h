#ifndef __MISSING_API_H__
#define __MISSING_API_H__


#include <c_types.h>
#include "espressif/user_interface.h"


// api definitions missing from sdk includes

bool sdk_wifi_set_opmode_current(uint8_t opmode);
bool sdk_wifi_station_set_config_current(struct sdk_station_config *config);


#endif
