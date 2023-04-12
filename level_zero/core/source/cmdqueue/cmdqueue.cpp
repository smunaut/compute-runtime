/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/assert_handler/assert_handler.h"
#include "shared/source/command_container/cmdcontainer.h"
#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/command_stream/csr_definitions.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/command_stream/queue_throttle.h"
#include "shared/source/command_stream/submissions_aggregator.h"
#include "shared/source/command_stream/wait_status.h"
#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/debugger/debugger_l0.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/memory_manager/allocation_properties.h"
#include "shared/source/memory_manager/memory_manager.h"
#include "shared/source/os_interface/os_context.h"
#include "shared/source/os_interface/product_helper.h"

#include "level_zero/core/source/cmdqueue/cmdqueue_imp.h"
#include "level_zero/core/source/device/device.h"
#include "level_zero/core/source/device/device_imp.h"
#include "level_zero/core/source/driver/driver_handle_imp.h"
#include "level_zero/core/source/gfx_core_helpers/l0_gfx_core_helper.h"
#include "level_zero/core/source/kernel/kernel.h"

#include "igfxfmid.h"

namespace L0 {

CommandQueueAllocatorFn commandQueueFactory[IGFX_MAX_PRODUCT] = {};

bool CommandQueue::frontEndTrackingEnabled() const {
    return NEO::DebugManager.flags.AllowPatchingVfeStateInCommandLists.get() || this->frontEndStateTracking;
}

CommandQueueImp::CommandQueueImp(Device *device, NEO::CommandStreamReceiver *csr, const ze_command_queue_desc_t *desc)
    : desc(*desc), device(device), csr(csr) {
    int overrideCmdQueueSyncMode = NEO::DebugManager.flags.OverrideCmdQueueSynchronousMode.get();
    if (overrideCmdQueueSyncMode != -1) {
        this->desc.mode = static_cast<ze_command_queue_mode_t>(overrideCmdQueueSyncMode);
    }

    int overrideUseKmdWaitFunction = NEO::DebugManager.flags.OverrideUseKmdWaitFunction.get();
    if (overrideUseKmdWaitFunction != -1) {
        useKmdWaitFunction = !!(overrideUseKmdWaitFunction);
    }
    this->stateChanges.reserve(CommandQueueImp::defaultCommandListStateChangeListSize);
}

ze_result_t CommandQueueImp::destroy() {
    if (this->clientId != CommandQueue::clientNotRegistered) {
        this->csr->unregisterClient();
    }

    if (commandStream.getCpuBase() != nullptr) {
        commandStream.replaceGraphicsAllocation(nullptr);
        commandStream.replaceBuffer(nullptr, 0);
    }
    buffers.destroy(this->getDevice());
    if (NEO::Debugger::isDebugEnabled(internalUsage) && device->getL0Debugger()) {
        device->getL0Debugger()->notifyCommandQueueDestroyed(device->getNEODevice());
    }

    delete this;
    return ZE_RESULT_SUCCESS;
}

ze_result_t CommandQueueImp::initialize(bool copyOnly, bool isInternal, bool immediateCmdListQueue) {
    ze_result_t returnValue;
    internalUsage = isInternal;
    internalQueueForImmediateCommandList = immediateCmdListQueue;
    returnValue = buffers.initialize(device, totalCmdBufferSize);
    if (returnValue == ZE_RESULT_SUCCESS) {
        NEO::GraphicsAllocation *bufferAllocation = buffers.getCurrentBufferAllocation();
        UNRECOVERABLE_IF(bufferAllocation == nullptr);
        commandStream.replaceBuffer(bufferAllocation->getUnderlyingBuffer(),
                                    defaultQueueCmdBufferSize);
        commandStream.replaceGraphicsAllocation(bufferAllocation);
        isCopyOnlyCommandQueue = copyOnly;
        preemptionCmdSyncProgramming = getPreemptionCmdProgramming();
        activeSubDevices = static_cast<uint32_t>(csr->getOsContext().getDeviceBitfield().count());
        if (!isInternal) {
            partitionCount = csr->getActivePartitions();
        }
        if (NEO::Debugger::isDebugEnabled(internalUsage) && device->getL0Debugger()) {
            device->getL0Debugger()->notifyCommandQueueCreated(device->getNEODevice());
        }
        auto &hwInfo = device->getHwInfo();
        auto &rootDeviceEnvironment = device->getNEODevice()->getRootDeviceEnvironment();
        this->stateComputeModeTracking = L0GfxCoreHelper::enableStateComputeModeTracking(rootDeviceEnvironment);
        this->frontEndStateTracking = L0GfxCoreHelper::enableFrontEndStateTracking(rootDeviceEnvironment);
        this->pipelineSelectStateTracking = L0GfxCoreHelper::enablePipelineSelectStateTracking(rootDeviceEnvironment);
        this->stateBaseAddressTracking = L0GfxCoreHelper::enableStateBaseAddressTracking(rootDeviceEnvironment);
        auto &productHelper = rootDeviceEnvironment.getHelper<NEO::ProductHelper>();
        this->doubleSbaWa = productHelper.isAdditionalStateBaseAddressWARequired(hwInfo);
        this->cmdListHeapAddressModel = L0GfxCoreHelper::getHeapAddressModel(rootDeviceEnvironment);
        this->dispatchCmdListBatchBufferAsPrimary = L0GfxCoreHelper::dispatchCmdListBatchBufferAsPrimary(!immediateCmdListQueue);
    }
    return returnValue;
}

NEO::WaitStatus CommandQueueImp::reserveLinearStreamSize(size_t size) {
    auto waitStatus{NEO::WaitStatus::Ready};

    if (commandStream.getAvailableSpace() < size) {
        waitStatus = buffers.switchBuffers(csr);

        NEO::GraphicsAllocation *nextBufferAllocation = buffers.getCurrentBufferAllocation();
        commandStream.replaceBuffer(nextBufferAllocation->getUnderlyingBuffer(),
                                    defaultQueueCmdBufferSize);
        commandStream.replaceGraphicsAllocation(nextBufferAllocation);
    }

    return waitStatus;
}

NEO::SubmissionStatus CommandQueueImp::submitBatchBuffer(size_t offset, NEO::ResidencyContainer &residencyContainer, void *endingCmdPtr,
                                                         bool isCooperative) {
    UNRECOVERABLE_IF(csr == nullptr);

    NEO::BatchBuffer batchBuffer(this->startingCmdBuffer->getGraphicsAllocation(), offset, 0, 0, nullptr, false, false,
                                 NEO::QueueThrottle::HIGH, NEO::QueueSliceCount::defaultSliceCount,
                                 this->startingCmdBuffer->getUsed(), this->startingCmdBuffer, endingCmdPtr, csr->getNumClients(), false, false);

    if (this->startingCmdBuffer != &this->commandStream) {
        this->csr->makeResident(*this->commandStream.getGraphicsAllocation());
    }

    commandStream.getGraphicsAllocation()->updateTaskCount(csr->peekTaskCount() + 1, csr->getOsContext().getContextId());
    commandStream.getGraphicsAllocation()->updateResidencyTaskCount(csr->peekTaskCount() + 1, csr->getOsContext().getContextId());

    csr->setActivePartitions(partitionCount);
    auto ret = csr->submitBatchBuffer(batchBuffer, csr->getResidencyAllocations());
    if (ret != NEO::SubmissionStatus::SUCCESS) {
        commandStream.getGraphicsAllocation()->updateTaskCount(csr->peekTaskCount(), csr->getOsContext().getContextId());
        commandStream.getGraphicsAllocation()->updateResidencyTaskCount(csr->peekTaskCount(), csr->getOsContext().getContextId());
        return ret;
    }

    buffers.setCurrentFlushStamp(csr->peekTaskCount(), csr->obtainCurrentFlushStamp());

    return ret;
}

ze_result_t CommandQueueImp::synchronize(uint64_t timeout) {
    if ((timeout == std::numeric_limits<uint64_t>::max()) && useKmdWaitFunction) {
        auto &waitPair = buffers.getCurrentFlushStamp();
        const auto waitStatus = csr->waitForTaskCountWithKmdNotifyFallback(waitPair.first, waitPair.second, false, NEO::QueueThrottle::MEDIUM);
        if (waitStatus == NEO::WaitStatus::GpuHang) {
            postSyncOperations(true);
            return ZE_RESULT_ERROR_DEVICE_LOST;
        }
        postSyncOperations(false);

        return ZE_RESULT_SUCCESS;
    } else {
        return synchronizeByPollingForTaskCount(timeout);
    }
}

ze_result_t CommandQueueImp::synchronizeByPollingForTaskCount(uint64_t timeout) {
    UNRECOVERABLE_IF(csr == nullptr);

    auto taskCountToWait = getTaskCount();
    bool enableTimeout = true;
    int64_t timeoutMicroseconds = static_cast<int64_t>(timeout);
    if (timeout == std::numeric_limits<uint64_t>::max()) {
        enableTimeout = false;
        timeoutMicroseconds = NEO::TimeoutControls::maxTimeout;
    }

    const auto waitStatus = csr->waitForCompletionWithTimeout(NEO::WaitParams{false, enableTimeout, timeoutMicroseconds}, taskCountToWait);
    if (waitStatus == NEO::WaitStatus::NotReady) {
        return ZE_RESULT_NOT_READY;
    }
    if (waitStatus == NEO::WaitStatus::GpuHang) {
        postSyncOperations(true);
        return ZE_RESULT_ERROR_DEVICE_LOST;
    }

    postSyncOperations(false);
    return ZE_RESULT_SUCCESS;
}

void CommandQueueImp::printKernelsPrintfOutput(bool hangDetected) {
    size_t size = this->printfKernelContainer.size();
    for (size_t i = 0; i < size; i++) {
        this->printfKernelContainer[i]->printPrintfOutput(hangDetected);
    }
    this->printfKernelContainer.clear();
}

void CommandQueueImp::checkAssert() {
    bool valueExpected = true;
    bool hadAssert = cmdListWithAssertExecuted.compare_exchange_strong(valueExpected, false);

    if (hadAssert) {
        UNRECOVERABLE_IF(device->getNEODevice()->getRootDeviceEnvironment().assertHandler.get() == nullptr);
        device->getNEODevice()->getRootDeviceEnvironment().assertHandler->printAssertAndAbort();
    }
}

void CommandQueueImp::postSyncOperations(bool hangDetected) {
    printKernelsPrintfOutput(hangDetected);
    checkAssert();

    if (NEO::Debugger::isDebugEnabled(internalUsage) && device->getL0Debugger() && NEO::DebugManager.flags.DebuggerLogBitmask.get()) {
        device->getL0Debugger()->printTrackedAddresses(csr->getOsContext().getContextId());
    }
}

CommandQueue *CommandQueue::create(uint32_t productFamily, Device *device, NEO::CommandStreamReceiver *csr,
                                   const ze_command_queue_desc_t *desc, bool isCopyOnly, bool isInternal, bool immediateCmdListQueue, ze_result_t &returnValue) {
    CommandQueueAllocatorFn allocator = nullptr;
    if (productFamily < IGFX_MAX_PRODUCT) {
        allocator = commandQueueFactory[productFamily];
    }

    CommandQueueImp *commandQueue = nullptr;
    returnValue = ZE_RESULT_ERROR_UNINITIALIZED;
    if (!allocator) {
        return nullptr;
    }

    commandQueue = static_cast<CommandQueueImp *>((*allocator)(device, csr, desc));
    returnValue = commandQueue->initialize(isCopyOnly, isInternal, immediateCmdListQueue);
    if (returnValue != ZE_RESULT_SUCCESS) {
        commandQueue->destroy();
        commandQueue = nullptr;
        return nullptr;
    }

    auto &osContext = csr->getOsContext();
    DriverHandleImp *driverHandleImp = static_cast<DriverHandleImp *>(device->getDriverHandle());
    if (driverHandleImp->powerHint && driverHandleImp->powerHint != osContext.getUmdPowerHintValue()) {
        osContext.setUmdPowerHintValue(driverHandleImp->powerHint);
        osContext.reInitializeContext();
    }

    csr->initializeResources();
    csr->initDirectSubmission();
    if (commandQueue->cmdListHeapAddressModel == NEO::HeapAddressModel::GlobalStateless) {
        csr->createGlobalStatelessHeap();
    }

    return commandQueue;
}

ze_command_queue_mode_t CommandQueueImp::getSynchronousMode() const {
    return desc.mode;
}

ze_result_t CommandQueueImp::CommandBufferManager::initialize(Device *device, size_t sizeRequested) {
    size_t alignedSize = alignUp<size_t>(sizeRequested, MemoryConstants::pageSize64k);
    NEO::AllocationProperties properties{device->getRootDeviceIndex(), true, alignedSize,
                                         NEO::AllocationType::COMMAND_BUFFER,
                                         (device->getNEODevice()->getNumGenericSubDevices() > 1u) /* multiOsContextCapable */,
                                         false,
                                         device->getNEODevice()->getDeviceBitfield()};

    auto firstBuffer = device->obtainReusableAllocation(alignedSize, NEO::AllocationType::COMMAND_BUFFER);
    if (!firstBuffer) {
        firstBuffer = device->getNEODevice()->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);
    }

    auto secondBuffer = device->obtainReusableAllocation(alignedSize, NEO::AllocationType::COMMAND_BUFFER);
    if (!secondBuffer) {
        secondBuffer = device->getNEODevice()->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);
    }

    buffers[BUFFER_ALLOCATION::FIRST] = firstBuffer;
    buffers[BUFFER_ALLOCATION::SECOND] = secondBuffer;

    if (!buffers[BUFFER_ALLOCATION::FIRST] || !buffers[BUFFER_ALLOCATION::SECOND]) {
        return ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    flushId[BUFFER_ALLOCATION::FIRST] = std::make_pair(0u, 0u);
    flushId[BUFFER_ALLOCATION::SECOND] = std::make_pair(0u, 0u);
    return ZE_RESULT_SUCCESS;
}

void CommandQueueImp::CommandBufferManager::destroy(Device *device) {
    if (buffers[BUFFER_ALLOCATION::FIRST]) {
        device->storeReusableAllocation(*buffers[BUFFER_ALLOCATION::FIRST]);
        buffers[BUFFER_ALLOCATION::FIRST] = nullptr;
    }
    if (buffers[BUFFER_ALLOCATION::SECOND]) {
        device->storeReusableAllocation(*buffers[BUFFER_ALLOCATION::SECOND]);
        buffers[BUFFER_ALLOCATION::SECOND] = nullptr;
    }
}

NEO::WaitStatus CommandQueueImp::CommandBufferManager::switchBuffers(NEO::CommandStreamReceiver *csr) {
    if (bufferUse == BUFFER_ALLOCATION::FIRST) {
        bufferUse = BUFFER_ALLOCATION::SECOND;
    } else {
        bufferUse = BUFFER_ALLOCATION::FIRST;
    }

    auto waitStatus{NEO::WaitStatus::Ready};
    auto completionId = flushId[bufferUse];
    if (completionId.second != 0u) {
        UNRECOVERABLE_IF(csr == nullptr);
        waitStatus = csr->waitForTaskCountWithKmdNotifyFallback(completionId.first, completionId.second, false, NEO::QueueThrottle::MEDIUM);
    }

    return waitStatus;
}

void CommandQueueImp::handleIndirectAllocationResidency(UnifiedMemoryControls unifiedMemoryControls, std::unique_lock<std::mutex> &lockForIndirect, bool performMigration) {
    NEO::Device *neoDevice = this->device->getNEODevice();
    auto svmAllocsManager = this->device->getDriverHandle()->getSvmAllocsManager();
    auto submitAsPack = this->device->getDriverHandle()->getMemoryManager()->allowIndirectAllocationsAsPack(neoDevice->getRootDeviceIndex());
    if (NEO::DebugManager.flags.MakeIndirectAllocationsResidentAsPack.get() != -1) {
        submitAsPack = !!NEO::DebugManager.flags.MakeIndirectAllocationsResidentAsPack.get();
    }

    if (submitAsPack) {
        svmAllocsManager->makeIndirectAllocationsResident(*(this->csr), this->csr->peekTaskCount() + 1u);
    } else {
        lockForIndirect = this->device->getDriverHandle()->getSvmAllocsManager()->obtainOwnership();
        NEO::ResidencyContainer residencyAllocations;
        svmAllocsManager->addInternalAllocationsToResidencyContainer(neoDevice->getRootDeviceIndex(),
                                                                     residencyAllocations,
                                                                     unifiedMemoryControls.generateMask());
        makeResidentAndMigrate(performMigration, residencyAllocations);
    }
}

void CommandQueueImp::makeResidentAndMigrate(bool performMigration, const NEO::ResidencyContainer &residencyContainer) {
    for (auto alloc : residencyContainer) {
        csr->makeResident(*alloc);
        if (performMigration &&
            (alloc->getAllocationType() == NEO::AllocationType::SVM_GPU ||
             alloc->getAllocationType() == NEO::AllocationType::SVM_CPU)) {
            auto pageFaultManager = device->getDriverHandle()->getMemoryManager()->getPageFaultManager();
            pageFaultManager->moveAllocationToGpuDomain(reinterpret_cast<void *>(alloc->getGpuAddress()));
        }
    }
}

} // namespace L0
