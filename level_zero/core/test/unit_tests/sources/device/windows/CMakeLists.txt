#
# Copyright (C) 2022-2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

if(WIN32)
  target_sources(${TARGET_NAME} PRIVATE
                 ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_device_pci_bus_info_windows.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_device_pci_speed_info_windows.cpp
  )
endif()
