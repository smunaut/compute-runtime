#
# Copyright (C) 2020-2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

set(target_name mock_gmm)
project(${target_name})

# Setting up our local list of test files
set(NEO_SHARED_TESTS_mock_gmm
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_gmm.cpp
)
add_library(${target_name} EXCLUDE_FROM_ALL OBJECT ${NEO_SHARED_TESTS_mock_gmm})

target_include_directories(${target_name} PRIVATE
                           ${WDK_INCLUDE_PATHS}
                           ${NEO__GMM_INCLUDE_DIR}
)

create_project_source_tree(${target_name})
set_target_properties(${target_name} PROPERTIES FOLDER "test mocks")
