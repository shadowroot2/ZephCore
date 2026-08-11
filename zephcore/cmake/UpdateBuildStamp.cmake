# SPDX-License-Identifier: MIT

if(NOT DEFINED ZEPHCORE_BUILD_STAMP)
	message(FATAL_ERROR "ZEPHCORE_BUILD_STAMP is required")
endif()

# No UTC flag: the firmware displays the local build date.
string(TIMESTAMP ZEPHCORE_BUILD_DATE "%Y %b %d")

set(ZEPHCORE_BUILD_STAMP_CONTENT
"/* Generated at build time. Do not edit. */\n#pragma once\n#define FIRMWARE_BUILD_DATE \"${ZEPHCORE_BUILD_DATE}\"\n")

set(ZEPHCORE_BUILD_STAMP_OLD "")
if(EXISTS "${ZEPHCORE_BUILD_STAMP}")
	file(READ "${ZEPHCORE_BUILD_STAMP}" ZEPHCORE_BUILD_STAMP_OLD)
endif()

# Preserve the timestamp within one day, avoiding needless full rebuilds.
if(NOT ZEPHCORE_BUILD_STAMP_OLD STREQUAL ZEPHCORE_BUILD_STAMP_CONTENT)
	file(WRITE "${ZEPHCORE_BUILD_STAMP}" "${ZEPHCORE_BUILD_STAMP_CONTENT}")
endif()
