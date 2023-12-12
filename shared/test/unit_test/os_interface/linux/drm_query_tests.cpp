/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/file_io.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/os_interface.h"
#include "shared/source/os_interface/product_helper.h"
#include "shared/test/common/helpers/default_hw_info.h"
#include "shared/test/common/libult/linux/drm_mock.h"
#include "shared/test/common/mocks/linux/mock_drm_allocation.h"
#include "shared/test/common/mocks/mock_execution_environment.h"
#include "shared/test/common/test_macros/hw_test.h"

#include "gtest/gtest.h"

using namespace NEO;

using HwConfigTopologyQuery = ::testing::Test;

HWTEST2_F(HwConfigTopologyQuery, WhenGettingTopologyFailsThenSetMaxValuesBasedOnSubsliceIoctlQuery, MatchAny) {
    auto executionEnvironment = std::make_unique<MockExecutionEnvironment>();

    auto drm = new DrmMock(*executionEnvironment->rootDeviceEnvironments[0]);

    executionEnvironment->rootDeviceEnvironments[0]->osInterface = std::make_unique<OSInterface>();
    auto osInterface = executionEnvironment->rootDeviceEnvironments[0]->osInterface.get();
    osInterface->setDriverModel(std::unique_ptr<Drm>(drm));

    drm->failRetTopology = true;

    auto hwInfo = *executionEnvironment->rootDeviceEnvironments[0]->getHardwareInfo();
    HardwareInfo outHwInfo;

    hwInfo.gtSystemInfo.MaxSlicesSupported = 0;
    hwInfo.gtSystemInfo.MaxSubSlicesSupported = 0;
    hwInfo.gtSystemInfo.MaxEuPerSubSlice = 6;

    auto &productHelper = executionEnvironment->rootDeviceEnvironments[0]->getHelper<ProductHelper>();
    int ret = productHelper.configureHwInfoDrm(&hwInfo, &outHwInfo, *executionEnvironment->rootDeviceEnvironments[0].get());
    EXPECT_NE(-1, ret);

    EXPECT_EQ(6u, outHwInfo.gtSystemInfo.MaxEuPerSubSlice);
    EXPECT_EQ(outHwInfo.gtSystemInfo.SubSliceCount, outHwInfo.gtSystemInfo.MaxSubSlicesSupported);
    EXPECT_EQ(hwInfo.gtSystemInfo.SliceCount, outHwInfo.gtSystemInfo.MaxSlicesSupported);

    EXPECT_EQ(static_cast<uint32_t>(drm->storedEUVal), outHwInfo.gtSystemInfo.EUCount);
    EXPECT_EQ(static_cast<uint32_t>(drm->storedSSVal), outHwInfo.gtSystemInfo.SubSliceCount);
}

TEST(DrmQueryTest, WhenCallingIsDebugAttachAvailableThenReturnValueIsFalse) {
    auto executionEnvironment = std::make_unique<MockExecutionEnvironment>();
    DrmMock drm{*executionEnvironment->rootDeviceEnvironments[0]};
    drm.allowDebugAttachCallBase = true;

    EXPECT_FALSE(drm.isDebugAttachAvailable());
}

TEST(DrmQueryTest, WhenCallingQueryPageFaultSupportThenReturnFalse) {
    auto executionEnvironment = std::make_unique<MockExecutionEnvironment>();
    DrmMock drm{*executionEnvironment->rootDeviceEnvironments[0]};

    drm.queryPageFaultSupport();

    EXPECT_FALSE(drm.hasPageFaultSupport());
}

TEST(DrmQueryTest, givenDrmAllocationWhenShouldAllocationFaultIsCalledThenReturnFalse) {
    auto executionEnvironment = std::make_unique<MockExecutionEnvironment>();
    DrmMock drm{*executionEnvironment->rootDeviceEnvironments[0]};

    MockDrmAllocation allocation(0u, AllocationType::buffer, MemoryPool::memoryNull);
    EXPECT_FALSE(allocation.shouldAllocationPageFault(&drm));
}
