/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "level_zero/sysman/source/shared/linux/sysman_kmd_interface.h"

namespace L0 {
namespace Sysman {
namespace ult {
class MockSysmanKmdInterfaceXe : public L0::Sysman::SysmanKmdInterfaceXe {

  public:
    using L0::Sysman::SysmanKmdInterface::pProcfsAccess;
    using L0::Sysman::SysmanKmdInterface::pSysfsAccess;
    MockSysmanKmdInterfaceXe(const PRODUCT_FAMILY productFamily) : SysmanKmdInterfaceXe(productFamily) {}
    ~MockSysmanKmdInterfaceXe() override = default;
};

} // namespace ult
} // namespace Sysman
} // namespace L0
