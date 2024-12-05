/*
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/os_interface/os_library.h"
#include "shared/source/os_interface/windows/d3dkmthk_wrapper.h"
#include "shared/source/os_interface/windows/gdi_profiling.h"
#include "shared/source/os_interface/windows/thk_wrapper.h"

#include <memory>

namespace NEO {

#define DEFINE_THK_WRAPPER(TYPE, VAR) ThkWrapper<TYPE> VAR = ThkWrapper<TYPE>(this->profiler, #TYPE, this->gdiId++);

class Gdi {
    uint32_t gdiId = 0;
    GdiProfiler profiler;

  public:
    Gdi();
    MOCKABLE_VIRTUAL ~Gdi();

    DEFINE_THK_WRAPPER(IN OUT CONST D3DKMT_OPENADAPTERFROMLUID *, openAdapterFromLuid);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATEALLOCATION *, createAllocation);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATEALLOCATION *, createAllocation2);
    NTSTATUS(APIENTRY *shareObjects)
    (UINT cObjects, const D3DKMT_HANDLE *hObjects, POBJECT_ATTRIBUTES pObjectAttributes, DWORD dwDesiredAccess, HANDLE *phSharedNtHandle) = {};
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_DESTROYALLOCATION *, destroyAllocation);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_DESTROYALLOCATION2 *, destroyAllocation2);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_QUERYADAPTERINFO *, queryAdapterInfo);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_CLOSEADAPTER *, closeAdapter);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATEDEVICE *, createDevice);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_DESTROYDEVICE *, destroyDevice);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_ESCAPE *, escape);
    DEFINE_THK_WRAPPER(IN D3DKMT_CREATECONTEXTVIRTUAL *, createContext);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_DESTROYCONTEXT *, destroyContext);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_OPENRESOURCE *, openResource);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_OPENRESOURCEFROMNTHANDLE *, openResourceFromNtHandle);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_QUERYRESOURCEINFO *, queryResourceInfo);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE *, queryResourceInfoFromNtHandle);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATESYNCHRONIZATIONOBJECT *, createSynchronizationObject);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *, createSynchronizationObject2);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *, destroySynchronizationObject);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *, signalSynchronizationObject);
    DEFINE_THK_WRAPPER(IN CONST_FROM_WDK_10_0_18328_0 D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *, waitForSynchronizationObject);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *, waitForSynchronizationObjectFromCpu);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *, signalSynchronizationObjectFromCpu);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *, waitForSynchronizationObjectFromGpu);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *, signalSynchronizationObjectFromGpu);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 *, openSyncObjectFromNtHandle2);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATEPAGINGQUEUE *, createPagingQueue);
    DEFINE_THK_WRAPPER(IN OUT D3DDDI_DESTROYPAGINGQUEUE *, destroyPagingQueue);
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_LOCK2 *, lock2);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_UNLOCK2 *, unlock2);
    DEFINE_THK_WRAPPER(IN OUT D3DDDI_MAPGPUVIRTUALADDRESS *, mapGpuVirtualAddress);
    DEFINE_THK_WRAPPER(IN OUT D3DDDI_RESERVEGPUVIRTUALADDRESS *, reserveGpuVirtualAddress);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_FREEGPUVIRTUALADDRESS *, freeGpuVirtualAddress);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_UPDATEGPUVIRTUALADDRESS *, updateGpuVirtualAddress);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SUBMITCOMMAND *, submitCommand);
    DEFINE_THK_WRAPPER(IN OUT D3DDDI_MAKERESIDENT *, makeResident);
    DEFINE_THK_WRAPPER(IN D3DKMT_EVICT *, evict);
    DEFINE_THK_WRAPPER(IN D3DKMT_REGISTERTRIMNOTIFICATION *, registerTrimNotification);
    DEFINE_THK_WRAPPER(IN D3DKMT_UNREGISTERTRIMNOTIFICATION *, unregisterTrimNotification);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SETALLOCATIONPRIORITY *, setAllocationPriority);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *, setSchedulingPriority);

    // HW queue
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_CREATEHWQUEUE *, createHwQueue);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_DESTROYHWQUEUE *, destroyHwQueue);
    DEFINE_THK_WRAPPER(IN CONST D3DKMT_SUBMITCOMMANDTOHWQUEUE *, submitCommandToHwQueue);

    // For debug purposes
    DEFINE_THK_WRAPPER(IN OUT D3DKMT_GETDEVICESTATE *, getDeviceState);

    bool isInitialized() {
        return initialized;
    }

    MOCKABLE_VIRTUAL bool setupHwQueueProcAddresses();

  protected:
    OsLibrary *createGdiDLL();
    MOCKABLE_VIRTUAL bool getAllProcAddresses();
    std::unique_ptr<NEO::OsLibrary> gdiDll = nullptr;
    bool initialized = false;
};
} // namespace NEO
