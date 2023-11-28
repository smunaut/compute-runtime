/*
 * Copyright (C) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/device_factory.h"

#include "shared/source/aub/aub_center.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/device/root_device.h"
#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/compiler_product_helper.h"
#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/helpers/product_config_helper.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/aub_memory_operations_handler.h"
#include "shared/source/os_interface/os_interface.h"
#include "shared/source/os_interface/product_helper.h"

#include "hw_device_id.h"

namespace NEO {

bool DeviceFactory::prepareDeviceEnvironmentsForProductFamilyOverride(ExecutionEnvironment &executionEnvironment) {
    auto numRootDevices = 1u;
    if (DebugManager.flags.CreateMultipleRootDevices.get()) {
        numRootDevices = DebugManager.flags.CreateMultipleRootDevices.get();
    }
    executionEnvironment.prepareRootDeviceEnvironments(numRootDevices);

    auto productFamily = DebugManager.flags.ProductFamilyOverride.get();

    auto configStr = productFamily;
    auto productConfigHelper = std::make_unique<ProductConfigHelper>();
    ProductConfigHelper::adjustDeviceName(configStr);
    uint32_t productConfig = productConfigHelper->getProductConfigFromDeviceName(configStr);

    if (DebugManager.flags.OverrideHwIpVersion.get() != -1 && productConfigHelper->isSupportedProductConfig(DebugManager.flags.OverrideHwIpVersion.get())) {
        productConfig = DebugManager.flags.OverrideHwIpVersion.get();
    }

    const HardwareInfo *hwInfoConst = getDefaultHwInfo();
    DeviceAotInfo aotInfo{};
    auto productConfigFound = productConfigHelper->getDeviceAotInfoForProductConfig(productConfig, aotInfo);
    if (productConfigFound) {
        hwInfoConst = aotInfo.hwInfo;
    } else {
        getHwInfoForPlatformString(productFamily, hwInfoConst);
    }
    std::string hwInfoConfigStr;
    uint64_t hwInfoConfig = 0x0;
    DebugManager.getHardwareInfoOverride(hwInfoConfigStr);

    for (auto rootDeviceIndex = 0u; rootDeviceIndex < numRootDevices; rootDeviceIndex++) {
        auto &rootDeviceEnvironment = *executionEnvironment.rootDeviceEnvironments[rootDeviceIndex].get();
        rootDeviceEnvironment.setHwInfo(hwInfoConst);

        rootDeviceEnvironment.initProductHelper();
        rootDeviceEnvironment.initGfxCoreHelper();
        rootDeviceEnvironment.initApiGfxCoreHelper();
        rootDeviceEnvironment.initCompilerProductHelper();
        rootDeviceEnvironment.initAilConfigurationHelper();
        if (false == rootDeviceEnvironment.initAilConfiguration()) {
            return false;
        }

        auto hardwareInfo = rootDeviceEnvironment.getMutableHardwareInfo();

        if (DebugManager.flags.OverrideRevision.get() != -1) {
            hardwareInfo->platform.usRevId = static_cast<unsigned short>(DebugManager.flags.OverrideRevision.get());
        }

        if (DebugManager.flags.ForceDeviceId.get() != "unk") {
            hardwareInfo->platform.usDeviceID = static_cast<unsigned short>(std::stoi(DebugManager.flags.ForceDeviceId.get(), nullptr, 16));
        }

        const auto &compilerProductHelper = rootDeviceEnvironment.getHelper<CompilerProductHelper>();
        if (hwInfoConfigStr == "default") {
            hwInfoConfig = compilerProductHelper.getHwInfoConfig(*hwInfoConst);
        } else if (!parseHwInfoConfigString(hwInfoConfigStr, hwInfoConfig)) {
            return false;
        }

        if (productConfigFound) {
            compilerProductHelper.setProductConfigForHwInfo(*hardwareInfo, aotInfo.aotConfig);
            if (DebugManager.flags.ForceDeviceId.get() == "unk") {
                hardwareInfo->platform.usDeviceID = aotInfo.deviceIds->front();
            }
        }
        hardwareInfo->ipVersion.value = compilerProductHelper.getHwIpVersion(*hardwareInfo);
        rootDeviceEnvironment.initReleaseHelper();

        setHwInfoValuesFromConfig(hwInfoConfig, *hardwareInfo);
        hardwareInfoSetup[hwInfoConst->platform.eProductFamily](hardwareInfo, true, hwInfoConfig, rootDeviceEnvironment.getReleaseHelper());

        auto &productHelper = rootDeviceEnvironment.getProductHelper();
        productHelper.configureHardwareCustom(hardwareInfo, nullptr);

        rootDeviceEnvironment.setRcsExposure();

        if (DebugManager.flags.OverrideGpuAddressSpace.get() != -1) {
            hardwareInfo->capabilityTable.gpuAddressSpace = maxNBitValue(static_cast<uint64_t>(DebugManager.flags.OverrideGpuAddressSpace.get()));
        }

        [[maybe_unused]] bool result = rootDeviceEnvironment.initAilConfiguration();
        DEBUG_BREAK_IF(!result);

        auto csrType = DebugManager.flags.SetCommandStreamReceiver.get();
        if (csrType > 0) {
            auto &gfxCoreHelper = rootDeviceEnvironment.getHelper<GfxCoreHelper>();
            auto localMemoryEnabled = gfxCoreHelper.getEnableLocalMemory(*hardwareInfo);
            rootDeviceEnvironment.initGmm();
            rootDeviceEnvironment.initAubCenter(localMemoryEnabled, "", static_cast<CommandStreamReceiverType>(csrType));
            auto aubCenter = rootDeviceEnvironment.aubCenter.get();
            rootDeviceEnvironment.memoryOperationsInterface = std::make_unique<AubMemoryOperationsHandler>(aubCenter->getAubManager());
        }
    }

    executionEnvironment.setDeviceHierarchy(executionEnvironment.rootDeviceEnvironments[0]->getHelper<GfxCoreHelper>());
    executionEnvironment.parseAffinityMask();
    executionEnvironment.adjustCcsCount();
    executionEnvironment.calculateMaxOsContextCount();
    return true;
}

bool DeviceFactory::isHwModeSelected() {
    int32_t csr = DebugManager.flags.SetCommandStreamReceiver.get();
    switch (csr) {
    case CSR_AUB:
    case CSR_TBX:
    case CSR_TBX_WITH_AUB:
        return false;
    default:
        return true;
    }
}

static bool initHwDeviceIdResources(ExecutionEnvironment &executionEnvironment,
                                    std::unique_ptr<NEO::HwDeviceId> &&hwDeviceId, uint32_t rootDeviceIndex) {
    if (!executionEnvironment.rootDeviceEnvironments[rootDeviceIndex]->initOsInterface(std::move(hwDeviceId), rootDeviceIndex)) {
        return false;
    }

    if (DebugManager.flags.OverrideGpuAddressSpace.get() != -1) {
        executionEnvironment.rootDeviceEnvironments[rootDeviceIndex]->getMutableHardwareInfo()->capabilityTable.gpuAddressSpace =
            maxNBitValue(static_cast<uint64_t>(DebugManager.flags.OverrideGpuAddressSpace.get()));
    }

    if (DebugManager.flags.OverrideRevision.get() != -1) {
        executionEnvironment.rootDeviceEnvironments[rootDeviceIndex]->getMutableHardwareInfo()->platform.usRevId =
            static_cast<unsigned short>(DebugManager.flags.OverrideRevision.get());
    }

    executionEnvironment.rootDeviceEnvironments[rootDeviceIndex]->initGmm();

    return true;
}

bool DeviceFactory::prepareDeviceEnvironments(ExecutionEnvironment &executionEnvironment) {
    using HwDeviceIds = std::vector<std::unique_ptr<HwDeviceId>>;

    HwDeviceIds hwDeviceIds = OSInterface::discoverDevices(executionEnvironment);
    if (hwDeviceIds.empty()) {
        return false;
    }

    executionEnvironment.prepareRootDeviceEnvironments(static_cast<uint32_t>(hwDeviceIds.size()));

    uint32_t rootDeviceIndex = 0u;

    for (auto &hwDeviceId : hwDeviceIds) {
        if (initHwDeviceIdResources(executionEnvironment, std::move(hwDeviceId), rootDeviceIndex) == false) {
            continue;
        }

        rootDeviceIndex++;
    }

    executionEnvironment.rootDeviceEnvironments.resize(rootDeviceIndex);

    if (rootDeviceIndex == 0) {
        return false;
    }

    executionEnvironment.setDeviceHierarchy(executionEnvironment.rootDeviceEnvironments[0]->getHelper<GfxCoreHelper>());
    executionEnvironment.sortNeoDevices();
    executionEnvironment.parseAffinityMask();
    executionEnvironment.adjustRootDeviceEnvironments();
    executionEnvironment.adjustCcsCount();
    executionEnvironment.calculateMaxOsContextCount();

    return true;
}

bool DeviceFactory::prepareDeviceEnvironment(ExecutionEnvironment &executionEnvironment, std::string &osPciPath, const uint32_t rootDeviceIndex) {
    using HwDeviceIds = std::vector<std::unique_ptr<HwDeviceId>>;

    HwDeviceIds hwDeviceIds = OSInterface::discoverDevice(executionEnvironment, osPciPath);
    if (hwDeviceIds.empty()) {
        return false;
    }

    executionEnvironment.prepareRootDeviceEnvironment(rootDeviceIndex);

    // HwDeviceIds should contain only one entry corresponding to osPciPath
    UNRECOVERABLE_IF(hwDeviceIds.size() > 1);
    if (!initHwDeviceIdResources(executionEnvironment, std::move(hwDeviceIds[0]), rootDeviceIndex)) {
        return false;
    }

    executionEnvironment.adjustCcsCount(rootDeviceIndex);
    return true;
}

std::unique_ptr<Device> DeviceFactory::createDevice(ExecutionEnvironment &executionEnvironment, std::string &osPciPath, const uint32_t rootDeviceIndex) {
    std::unique_ptr<Device> device;
    if (!NEO::prepareDeviceEnvironment(executionEnvironment, osPciPath, rootDeviceIndex)) {
        return device;
    }

    executionEnvironment.memoryManager->createDeviceSpecificMemResources(rootDeviceIndex);
    executionEnvironment.memoryManager->reInitLatestContextId();
    device = createRootDeviceFunc(executionEnvironment, rootDeviceIndex);

    return device;
}

std::vector<std::unique_ptr<Device>> DeviceFactory::createDevices(ExecutionEnvironment &executionEnvironment) {
    std::vector<std::unique_ptr<Device>> devices;

    if (!NEO::prepareDeviceEnvironments(executionEnvironment)) {
        return devices;
    }

    if (!DeviceFactory::createMemoryManagerFunc(executionEnvironment)) {
        return devices;
    }

    for (uint32_t rootDeviceIndex = 0u; rootDeviceIndex < executionEnvironment.rootDeviceEnvironments.size(); rootDeviceIndex++) {
        auto device = createRootDeviceFunc(executionEnvironment, rootDeviceIndex);
        if (device) {
            devices.push_back(std::move(device));
        }
    }

    return devices;
}

std::unique_ptr<Device> (*DeviceFactory::createRootDeviceFunc)(ExecutionEnvironment &, uint32_t) = [](ExecutionEnvironment &executionEnvironment, uint32_t rootDeviceIndex) -> std::unique_ptr<Device> {
    return std::unique_ptr<Device>(Device::create<RootDevice>(&executionEnvironment, rootDeviceIndex));
};

bool (*DeviceFactory::createMemoryManagerFunc)(ExecutionEnvironment &) = [](ExecutionEnvironment &executionEnvironment) -> bool {
    return executionEnvironment.initializeMemoryManager();
};

bool DeviceFactory::isAllowedDeviceId(uint32_t deviceId, const std::string &deviceIdString) {
    if (deviceIdString != "unk") {
        char *endptr = nullptr;
        auto reqDeviceId = strtoul(deviceIdString.c_str(), &endptr, 16);
        return (static_cast<uint32_t>(reqDeviceId) == deviceId);
    }
    return true;
}
} // namespace NEO
