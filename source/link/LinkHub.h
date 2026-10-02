#pragma once

#include "LinkApi.h"

// Only the relay's file (and the Tests program) contains the hub; the panel looks this function up by name instead
#if defined(_WIN32)
    #define HS_LINK_EXPORT __declspec (dllexport)
#else
    #define HS_LINK_EXPORT __attribute__ ((visibility ("default")))
#endif

extern "C" HS_LINK_EXPORT const hs_link_api* heartseeker_link_get_api (uint32_t abi_version);
