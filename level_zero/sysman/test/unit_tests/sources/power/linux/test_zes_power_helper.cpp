/*
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/sysman/test/unit_tests/sources/linux/mocks/mock_sysman_product_helper.h"
#include "level_zero/sysman/test/unit_tests/sources/power/linux/mock_sysfs_power.h"
namespace L0 {
namespace Sysman {
namespace ult {

constexpr uint32_t powerHandleComponentCount = 1u;
using SysmanDevicePowerFixtureHelper = SysmanDevicePowerFixtureI915;

TEST_F(SysmanDevicePowerFixtureHelper, GivenValidPowerHandleWhenGettingPowerEnergyCounterThenValidPowerReadingsRetrieved) {

    auto handles = getPowerHandles(powerHandleComponentCount);
    for (auto handle : handles) {
        zes_power_energy_counter_t energyCounter = {};
        ASSERT_EQ(ZE_RESULT_SUCCESS, zesPowerGetEnergyCounter(handle, &energyCounter));
        EXPECT_EQ(energyCounter.energy, expectedEnergyCounter);
    }
}

constexpr uint32_t powerHandleComponentCountMultiDevice = 3u;
using SysmanDevicePowerMultiDeviceFixtureHelper = SysmanDevicePowerMultiDeviceFixture;

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidDeviceHandlesAndHwmonInterfaceExistThenSuccessIsReturned) {
    auto subDeviceCount = pLinuxSysmanImp->getSubDeviceCount();
    uint32_t subdeviceId = 0;
    do {
        ze_bool_t onSubdevice = (subDeviceCount == 0) ? false : true;
        PublicLinuxPowerImp *pPowerImp = new PublicLinuxPowerImp(pOsSysman, onSubdevice, subdeviceId, ZES_POWER_DOMAIN_PACKAGE);
        EXPECT_TRUE(pPowerImp->isPowerModuleSupported());
        delete pPowerImp;

    } while (++subdeviceId < subDeviceCount);
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenInvalidComponentCountWhenEnumeratingPowerDomainsThenValidCountIsReturnedAndVerifySysmanPowerGetCallSucceeds) {
    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumPowerDomains(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, powerHandleComponentCountMultiDevice);

    count = count + 1;
    EXPECT_EQ(zesDeviceEnumPowerDomains(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, powerHandleComponentCountMultiDevice);
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerPointerWhenGettingCardPowerDomainThenFailureIsReturned) {
    zes_pwr_handle_t phPower = {};
    EXPECT_EQ(zesDeviceGetCardPowerDomain(device->toHandle(), &phPower), ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerHandleWhenGettingPowerPropertiesThenCallSucceeds) {
    MockSysmanProductHelper *pMockSysmanProductHelper = new MockSysmanProductHelper();
    pMockSysmanProductHelper->isPowerSetLimitSupportedResult = true;
    std::unique_ptr<SysmanProductHelper> pSysmanProductHelper(static_cast<SysmanProductHelper *>(pMockSysmanProductHelper));
    std::swap(pLinuxSysmanImp->pSysmanProductHelper, pSysmanProductHelper);

    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        zes_power_properties_t properties = {};
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));
        if (properties.onSubdevice) {
            EXPECT_FALSE(properties.canControl);
            EXPECT_EQ(properties.defaultLimit, -1);
            EXPECT_EQ(properties.maxLimit, -1);
            EXPECT_EQ(properties.minLimit, -1);
        } else {
            EXPECT_EQ(properties.canControl, true);
            EXPECT_EQ(properties.defaultLimit, (int32_t)(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.maxLimit, (int32_t)(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.minLimit, -1);
        }
        EXPECT_EQ(properties.isEnergyThresholdSupported, false);
    }
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerHandleWhenGettingPowerPropertiesAndExtPropertiesThenCallSucceeds) {
    MockSysmanProductHelper *pMockSysmanProductHelper = new MockSysmanProductHelper();
    pMockSysmanProductHelper->isPowerSetLimitSupportedResult = true;
    std::unique_ptr<SysmanProductHelper> pSysmanProductHelper(static_cast<SysmanProductHelper *>(pMockSysmanProductHelper));
    std::swap(pLinuxSysmanImp->pSysmanProductHelper, pSysmanProductHelper);

    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_power_properties_t properties = {};
        zes_power_ext_properties_t extProperties = {};
        zes_power_limit_ext_desc_t defaultLimit = {};

        extProperties.defaultLimit = &defaultLimit;
        extProperties.stype = ZES_STRUCTURE_TYPE_POWER_EXT_PROPERTIES;
        properties.pNext = &extProperties;
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));
        EXPECT_TRUE(defaultLimit.limitValueLocked);
        EXPECT_TRUE(defaultLimit.enabledStateLocked);
        EXPECT_TRUE(defaultLimit.intervalValueLocked);
        EXPECT_EQ(defaultLimit.level, ZES_POWER_LEVEL_UNKNOWN);
        EXPECT_EQ(defaultLimit.source, ZES_POWER_SOURCE_ANY);
        EXPECT_EQ(defaultLimit.limitUnit, ZES_LIMIT_UNIT_POWER);
        EXPECT_EQ(extProperties.domain, ZES_POWER_DOMAIN_PACKAGE);
        if (properties.onSubdevice) {
            EXPECT_FALSE(properties.canControl);
            EXPECT_EQ(defaultLimit.limit, -1);
        } else {
            EXPECT_TRUE(properties.canControl);
            EXPECT_EQ(defaultLimit.limit, static_cast<int32_t>(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.defaultLimit, (int32_t)(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.maxLimit, (int32_t)(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.minLimit, -1);
        }
        EXPECT_EQ(properties.isEnergyThresholdSupported, false);
    }
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerHandleAndExtPropertiesWithNullDescWhenGettingPowerPropertiesThenCallSucceeds) {
    MockSysmanProductHelper *pMockSysmanProductHelper = new MockSysmanProductHelper();
    pMockSysmanProductHelper->isPowerSetLimitSupportedResult = true;
    std::unique_ptr<SysmanProductHelper> pSysmanProductHelper(static_cast<SysmanProductHelper *>(pMockSysmanProductHelper));
    std::swap(pLinuxSysmanImp->pSysmanProductHelper, pSysmanProductHelper);

    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_power_properties_t properties = {};
        zes_power_ext_properties_t extProperties = {};

        properties.pNext = &extProperties;
        extProperties.stype = ZES_STRUCTURE_TYPE_POWER_EXT_PROPERTIES;
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));
        EXPECT_EQ(extProperties.domain, ZES_POWER_DOMAIN_PACKAGE);
        if (properties.onSubdevice) {
            EXPECT_FALSE(properties.canControl);
            EXPECT_EQ(properties.maxLimit, -1);
            EXPECT_EQ(properties.minLimit, -1);
            EXPECT_EQ(properties.defaultLimit, -1);
        } else {
            EXPECT_TRUE(properties.canControl);
            EXPECT_EQ(properties.defaultLimit, (int32_t)(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.maxLimit, (int32_t)(mockDefaultPowerLimitVal / milliFactor));
            EXPECT_EQ(properties.minLimit, -1);
        }
        EXPECT_EQ(properties.isEnergyThresholdSupported, false);
    }
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenScanDirectoriesFailAndTelemetrySupportNotAvailableWhenGettingCardPowerThenFailureIsReturned) {

    pSysfsAccess->mockscanDirEntriesResult = ZE_RESULT_ERROR_NOT_AVAILABLE;

    for (const auto &handle : pSysmanDeviceImp->pPowerHandleContext->handleList) {
        delete handle;
    }
    pSysmanDeviceImp->pPowerHandleContext->handleList.clear();
    pSysmanDeviceImp->pPowerHandleContext->init(pLinuxSysmanImp->getSubDeviceCount());

    zes_pwr_handle_t phPower = {};
    EXPECT_EQ(zesDeviceGetCardPowerDomain(device->toHandle(), &phPower), ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenPowerHandleDomainIsNotCardWhenGettingCardPowerDomainThenErrorIsReturned) {
    uint32_t count = 0;
    EXPECT_EQ(zesDeviceEnumPowerDomains(device->toHandle(), &count, nullptr), ZE_RESULT_SUCCESS);
    EXPECT_EQ(count, powerHandleComponentCountMultiDevice);

    auto handle = pSysmanDeviceImp->pPowerHandleContext->handleList.front();
    delete handle;

    pSysmanDeviceImp->pPowerHandleContext->handleList.erase(pSysmanDeviceImp->pPowerHandleContext->handleList.begin());
    zes_pwr_handle_t phPower = {};
    EXPECT_EQ(zesDeviceGetCardPowerDomain(device->toHandle(), &phPower), ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenReadingToSysNodesFailsWhenCallingGetPowerLimitsExtThenPowerLimitCountIsZero) {
    for (const auto &handle : pSysmanDeviceImp->pPowerHandleContext->handleList) {
        delete handle;
    }
    pSysmanDeviceImp->pPowerHandleContext->handleList.clear();
    pSysfsAccess->mockReadValUnsignedLongResult.push_back(ZE_RESULT_ERROR_NOT_AVAILABLE);
    pSysfsAccess->mockReadValUnsignedLongResult.push_back(ZE_RESULT_ERROR_NOT_AVAILABLE);
    pSysmanDeviceImp->pPowerHandleContext->init(pLinuxSysmanImp->getSubDeviceCount());

    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        uint32_t count = 0;
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &count, nullptr));
        EXPECT_EQ(count, 0u);
    }
}

TEST_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerHandleWhenGettingPowerEnergyCounterThenValidPowerReadingsRetrieved) {
    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);

    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_power_properties_t properties = {};
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));
        zes_power_energy_counter_t energyCounter = {};
        ASSERT_EQ(ZE_RESULT_SUCCESS, zesPowerGetEnergyCounter(handle, &energyCounter));
        if (properties.subdeviceId == 0) {
            EXPECT_EQ(energyCounter.energy, expectedEnergyCounterTile0);
        } else if (properties.subdeviceId == 1) {
            EXPECT_EQ(energyCounter.energy, expectedEnergyCounterTile1);
        }
    }
}

HWTEST2_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenSetPowerLimitsWhenGettingPowerLimitsThenLimitsSetEarlierAreRetrieved, IsXeHpOrXeHpcOrXeHpgCore) {
    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        zes_power_properties_t properties = {};
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));

        zes_power_sustained_limit_t sustainedSet = {};
        zes_power_sustained_limit_t sustainedGet = {};
        sustainedSet.power = 300000;
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerSetLimits(handle, &sustainedSet, nullptr, nullptr));
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimits(handle, &sustainedGet, nullptr, nullptr));
            EXPECT_EQ(sustainedGet.power, sustainedSet.power);
        } else {
            EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, zesPowerSetLimits(handle, &sustainedSet, nullptr, nullptr));
            EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, zesPowerGetLimits(handle, &sustainedGet, nullptr, nullptr));
        }

        zes_power_burst_limit_t burstGet = {};
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimits(handle, nullptr, &burstGet, nullptr));
            EXPECT_EQ(burstGet.enabled, false);
            EXPECT_EQ(burstGet.power, -1);
        } else {
            EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, zesPowerGetLimits(handle, nullptr, &burstGet, nullptr));
        }

        zes_power_peak_limit_t peakSet = {};
        zes_power_peak_limit_t peakGet = {};
        peakSet.powerAC = 300000;
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerSetLimits(handle, nullptr, nullptr, &peakSet));
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimits(handle, nullptr, nullptr, &peakGet));
            EXPECT_EQ(peakGet.powerAC, peakSet.powerAC);
            EXPECT_EQ(peakGet.powerDC, -1);
        } else {
            EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, zesPowerGetLimits(handle, nullptr, nullptr, &peakGet));
        }
    }
}

HWTEST2_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerHandlesWhenCallingSetAndGetPowerLimitExtThenLimitsSetEarlierAreRetrieved, IsPVC) {
    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);

        uint32_t limitCount = 0;
        const int32_t testLimit = 300000;
        const int32_t testInterval = 10;

        zes_power_properties_t properties = {};
        zes_power_limit_ext_desc_t limits = {};
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));

        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, nullptr));
            EXPECT_EQ(limitCount, mockLimitCount);
        } else {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, &limits));
            EXPECT_EQ(limitCount, 0u);
        }

        limitCount++;
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, nullptr));
            EXPECT_EQ(limitCount, mockLimitCount);
        } else {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, nullptr));
            EXPECT_EQ(limitCount, 0u);
        }

        std::vector<zes_power_limit_ext_desc_t> allLimits(limitCount);
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, allLimits.data()));
        } else {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, allLimits.data()));
            EXPECT_EQ(limitCount, 0u);
        }
        for (uint32_t i = 0; i < limitCount; i++) {
            if (allLimits[i].level == ZES_POWER_LEVEL_SUSTAINED) {
                EXPECT_FALSE(allLimits[i].limitValueLocked);
                EXPECT_TRUE(allLimits[i].enabledStateLocked);
                EXPECT_FALSE(allLimits[i].intervalValueLocked);
                EXPECT_EQ(ZES_POWER_SOURCE_ANY, allLimits[i].source);
                EXPECT_EQ(ZES_LIMIT_UNIT_POWER, allLimits[i].limitUnit);
                allLimits[i].limit = testLimit;
                allLimits[i].interval = testInterval;
            } else if (allLimits[i].level == ZES_POWER_LEVEL_PEAK) {
                EXPECT_FALSE(allLimits[i].limitValueLocked);
                EXPECT_TRUE(allLimits[i].enabledStateLocked);
                EXPECT_TRUE(allLimits[i].intervalValueLocked);
                EXPECT_EQ(ZES_POWER_SOURCE_ANY, allLimits[i].source);
                EXPECT_EQ(ZES_LIMIT_UNIT_CURRENT, allLimits[i].limitUnit);
                allLimits[i].limit = testLimit;
            }
        }
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerSetLimitsExt(handle, &limitCount, allLimits.data()));
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, allLimits.data()));
            for (uint32_t i = 0; i < limitCount; i++) {
                if (allLimits[i].level == ZES_POWER_LEVEL_SUSTAINED) {
                    EXPECT_EQ(testInterval, allLimits[i].interval);
                } else if (allLimits[i].level == ZES_POWER_LEVEL_PEAK) {
                    EXPECT_EQ(0, allLimits[i].interval);
                }
                EXPECT_EQ(testLimit, allLimits[i].limit);
            }
        } else {
            EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, zesPowerSetLimitsExt(handle, &limitCount, allLimits.data()));
        }
    }
}

HWTEST2_F(SysmanDevicePowerMultiDeviceFixtureHelper, GivenValidPowerHandlesWhenCallingSetAndGetPowerLimitExtThenLimitsSetEarlierAreRetrieved, IsDG1) {
    auto handles = getPowerHandles(powerHandleComponentCountMultiDevice);
    for (auto handle : handles) {
        ASSERT_NE(nullptr, handle);
        uint32_t limitCount = 0;
        const int32_t testLimit = 300000;
        const int32_t testInterval = 10;

        zes_power_properties_t properties = {};
        zes_power_limit_ext_desc_t limits = {};
        EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetProperties(handle, &properties));

        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, nullptr));
            EXPECT_EQ(limitCount, mockLimitCount);
        } else {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, &limits));
            EXPECT_EQ(limitCount, 0u);
        }

        limitCount++;
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, nullptr));
            EXPECT_EQ(limitCount, mockLimitCount);
        } else {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, nullptr));
            EXPECT_EQ(limitCount, 0u);
        }

        std::vector<zes_power_limit_ext_desc_t> allLimits(limitCount);
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, allLimits.data()));
        } else {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, allLimits.data()));
            EXPECT_EQ(limitCount, 0u);
        }
        for (uint32_t i = 0; i < limitCount; i++) {
            if (allLimits[i].level == ZES_POWER_LEVEL_SUSTAINED) {
                EXPECT_FALSE(allLimits[i].limitValueLocked);
                EXPECT_TRUE(allLimits[i].enabledStateLocked);
                EXPECT_FALSE(allLimits[i].intervalValueLocked);
                EXPECT_EQ(ZES_POWER_SOURCE_ANY, allLimits[i].source);
                EXPECT_EQ(ZES_LIMIT_UNIT_POWER, allLimits[i].limitUnit);
                allLimits[i].limit = testLimit;
                allLimits[i].interval = testInterval;
            } else if (allLimits[i].level == ZES_POWER_LEVEL_PEAK) {
                EXPECT_FALSE(allLimits[i].limitValueLocked);
                EXPECT_TRUE(allLimits[i].enabledStateLocked);
                EXPECT_TRUE(allLimits[i].intervalValueLocked);
                EXPECT_EQ(ZES_POWER_SOURCE_ANY, allLimits[i].source);
                EXPECT_EQ(ZES_LIMIT_UNIT_POWER, allLimits[i].limitUnit);
                allLimits[i].limit = testLimit;
            }
        }
        if (!properties.onSubdevice) {
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerSetLimitsExt(handle, &limitCount, allLimits.data()));
            EXPECT_EQ(ZE_RESULT_SUCCESS, zesPowerGetLimitsExt(handle, &limitCount, allLimits.data()));
            for (uint32_t i = 0; i < limitCount; i++) {
                if (allLimits[i].level == ZES_POWER_LEVEL_SUSTAINED) {
                    EXPECT_EQ(testInterval, allLimits[i].interval);
                } else if (allLimits[i].level == ZES_POWER_LEVEL_PEAK) {
                    EXPECT_EQ(0, allLimits[i].interval);
                }
                EXPECT_EQ(testLimit, allLimits[i].limit);
            }
        } else {
            EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, zesPowerSetLimitsExt(handle, &limitCount, allLimits.data()));
        }
    }
}

} // namespace ult
} // namespace Sysman
} // namespace L0
