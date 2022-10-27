/*
 * Copyright (C) 2021-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/default_hw_info.h"
#include "shared/test/common/helpers/gtest_helpers.h"
#include "shared/test/common/test_macros/hw_test.h"

#include "level_zero/core/source/hw_helpers/l0_hw_helper.h"

namespace L0 {
namespace ult {

using L0HwHelperTest = ::testing::Test;

using PlatformsWithWa = IsWithinGfxCore<IGFX_GEN12LP_CORE, IGFX_XE_HP_CORE>;
using NonMultiTilePlatforms = IsWithinGfxCore<IGFX_GEN9_CORE, IGFX_GEN12LP_CORE>;

HWTEST2_F(L0HwHelperTest, givenResumeWANotNeededThenFalseIsReturned, IsAtMostGen11) {
    auto &l0HwHelper = L0::L0HwHelper::get(NEO::defaultHwInfo->platform.eRenderCoreFamily);

    EXPECT_FALSE(l0HwHelper.isResumeWARequired());
}

HWTEST2_F(L0HwHelperTest, givenResumeWANeededThenTrueIsReturned, PlatformsWithWa) {
    auto &l0HwHelper = L0::L0HwHelper::get(NEO::defaultHwInfo->platform.eRenderCoreFamily);

    EXPECT_TRUE(l0HwHelper.isResumeWARequired());
}

static void printAttentionBitmask(uint8_t *expected, uint8_t *actual, uint32_t maxSlices, uint32_t maxSubSlicesPerSlice, uint32_t maxEuPerSubslice, uint32_t threadsPerEu, bool printBitmask = false) {
    auto bytesPerThread = threadsPerEu > 8 ? 2u : 1u;

    auto maxEUsInAtt = maxEuPerSubslice > 8 ? 8 : maxEuPerSubslice;
    auto bytesPerSlice = maxSubSlicesPerSlice * maxEUsInAtt * bytesPerThread;
    auto bytesPerSubSlice = maxEUsInAtt * bytesPerThread;

    for (uint32_t slice = 0; slice < maxSlices; slice++) {
        for (uint32_t subslice = 0; subslice < maxSubSlicesPerSlice; subslice++) {
            for (uint32_t eu = 0; eu < maxEUsInAtt; eu++) {
                for (uint32_t byte = 0; byte < bytesPerThread; byte++) {
                    if (printBitmask) {
                        std::bitset<8> bits(actual[slice * bytesPerSlice + subslice * bytesPerSubSlice + eu * bytesPerThread + byte]);
                        std::cout << " slice = " << slice << " subslice = " << subslice << " eu = " << eu << " threads bitmask = " << bits << "\n";
                    }

                    if (expected[slice * bytesPerSlice + subslice * bytesPerSubSlice + eu * bytesPerThread + byte] !=
                        actual[slice * bytesPerSlice + subslice * bytesPerSubSlice + eu * bytesPerThread + byte]) {
                        std::bitset<8> bits(actual[slice * bytesPerSlice + subslice * bytesPerSubSlice + eu * bytesPerThread + byte]);
                        std::bitset<8> bitsExpected(expected[slice * bytesPerSlice + subslice * bytesPerSubSlice + eu * bytesPerThread + byte]);
                        ASSERT_FALSE(true) << " got: slice = " << slice << " subslice = " << subslice << " eu = " << eu << " threads bitmask = " << bits << "\n"
                                           << " expected: slice = " << slice << " subslice = " << subslice << " eu = " << eu << " threads bitmask = " << bitsExpected << "\n";
                        ;
                    }
                }
            }
        }
    }

    if (printBitmask) {
        std::cout << "\n\n";
    }
}

HWTEST_F(L0HwHelperTest, givenL0HwHelperWhenAskingForImageCompressionSupportThenReturnFalse) {
    DebugManagerStateRestore restore;

    auto &l0HwHelper = L0::L0HwHelper::get(NEO::defaultHwInfo->platform.eRenderCoreFamily);

    EXPECT_FALSE(l0HwHelper.imageCompressionSupported(*NEO::defaultHwInfo));

    NEO::DebugManager.flags.RenderCompressedImagesEnabled.set(1);
    EXPECT_TRUE(l0HwHelper.imageCompressionSupported(*NEO::defaultHwInfo));

    NEO::DebugManager.flags.RenderCompressedImagesEnabled.set(0);
    EXPECT_FALSE(l0HwHelper.imageCompressionSupported(*NEO::defaultHwInfo));
}

HWTEST_F(L0HwHelperTest, givenL0HwHelperWhenAskingForUsmCompressionSupportThenReturnFalse) {
    DebugManagerStateRestore restore;

    auto &l0HwHelper = L0::L0HwHelper::get(NEO::defaultHwInfo->platform.eRenderCoreFamily);

    EXPECT_FALSE(l0HwHelper.forceDefaultUsmCompressionSupport());

    HardwareInfo hwInfo = *NEO::defaultHwInfo;

    hwInfo.capabilityTable.ftrRenderCompressedBuffers = true;
    EXPECT_FALSE(l0HwHelper.usmCompressionSupported(hwInfo));

    hwInfo.capabilityTable.ftrRenderCompressedBuffers = false;
    EXPECT_FALSE(l0HwHelper.usmCompressionSupported(hwInfo));

    NEO::DebugManager.flags.RenderCompressedBuffersEnabled.set(1);
    EXPECT_TRUE(l0HwHelper.usmCompressionSupported(hwInfo));

    hwInfo.capabilityTable.ftrRenderCompressedBuffers = true;
    NEO::DebugManager.flags.RenderCompressedBuffersEnabled.set(0);
    EXPECT_FALSE(l0HwHelper.usmCompressionSupported(hwInfo));
}

HWTEST_F(L0HwHelperTest, givenSliceSubsliceEuAndThreadIdsWhenGettingBitmaskThenCorrectBitmaskIsReturned) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t subslice = subslicesPerSlice > 1 ? subslicesPerSlice - 1 : 0;

    const auto threadsPerEu = (hwInfo.gtSystemInfo.ThreadCount / hwInfo.gtSystemInfo.EUCount);
    const auto bytesPerEu = 1;

    const auto maxEUsInAtt = hwInfo.gtSystemInfo.MaxEuPerSubSlice > 8 ? 8 : hwInfo.gtSystemInfo.MaxEuPerSubSlice;
    const auto threadsSizePerSubSlice = maxEUsInAtt * bytesPerEu;
    const auto threadsSizePerSlice = threadsSizePerSubSlice * subslicesPerSlice;

    std::vector<EuThread::ThreadId> threads;
    threads.push_back({0, 0, 0, 0, 6});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threads, hwInfo, bitmask, size);

    auto expectedBitmask = std::make_unique<uint8_t[]>(size);
    uint8_t *data = nullptr;
    memset(expectedBitmask.get(), 0, size);

    auto returnedBitmask = bitmask.get();
    EXPECT_EQ(uint8_t(1u << 6), returnedBitmask[0]);

    threads.clear();
    threads.push_back({0, 0, 0, 1, 3});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threads, hwInfo, bitmask, size);
    returnedBitmask = bitmask.get();
    returnedBitmask += bytesPerEu;
    EXPECT_EQ(uint8_t(1u << 3), returnedBitmask[0]);

    threads.clear();
    threads.push_back({0, 0, subslice, 3, 6});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threads, hwInfo, bitmask, size);

    data = expectedBitmask.get();
    memset(expectedBitmask.get(), 0, size);

    data = ptrOffset(data, subslice * threadsSizePerSubSlice);
    data = ptrOffset(data, 3 * bytesPerEu);
    data[0] = 1 << 6;

    printAttentionBitmask(expectedBitmask.get(), bitmask.get(), hwInfo.gtSystemInfo.MaxSlicesSupported, subslicesPerSlice, hwInfo.gtSystemInfo.MaxEuPerSubSlice, threadsPerEu);
    EXPECT_EQ(0, memcmp(bitmask.get(), expectedBitmask.get(), size));

    threads.clear();
    threads.push_back({0, hwInfo.gtSystemInfo.MaxSlicesSupported - 1, subslice, 3, 6});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threads, hwInfo, bitmask, size);
    data = expectedBitmask.get();
    memset(expectedBitmask.get(), 0, size);

    data = ptrOffset(data, (hwInfo.gtSystemInfo.MaxSlicesSupported - 1) * threadsSizePerSlice);
    data = ptrOffset(data, subslice * threadsSizePerSubSlice);
    data = ptrOffset(data, 3 * bytesPerEu);
    data[0] = 1 << 6;

    printAttentionBitmask(expectedBitmask.get(), bitmask.get(), hwInfo.gtSystemInfo.MaxSlicesSupported, subslicesPerSlice, hwInfo.gtSystemInfo.MaxEuPerSubSlice, threadsPerEu);
    EXPECT_EQ(0, memcmp(bitmask.get(), expectedBitmask.get(), size));

    threads.clear();
    threads.push_back({0, hwInfo.gtSystemInfo.MaxSlicesSupported - 1, subslice, maxEUsInAtt - 1, 0});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threads, hwInfo, bitmask, size);
    data = expectedBitmask.get();
    memset(expectedBitmask.get(), 0, size);

    data = ptrOffset(data, (hwInfo.gtSystemInfo.MaxSlicesSupported - 1) * threadsSizePerSlice);
    data = ptrOffset(data, subslice * threadsSizePerSubSlice);

    if (l0HwHelper.isResumeWARequired()) {
        data = ptrOffset(data, (maxEUsInAtt - 1) % 4 * bytesPerEu);
    } else {
        data = ptrOffset(data, maxEUsInAtt - 1 * bytesPerEu);
    }
    data[0] = 1;

    printAttentionBitmask(expectedBitmask.get(), bitmask.get(), hwInfo.gtSystemInfo.MaxSlicesSupported, subslicesPerSlice, hwInfo.gtSystemInfo.MaxEuPerSubSlice, threadsPerEu);
    EXPECT_EQ(0, memcmp(bitmask.get(), expectedBitmask.get(), size));
}

HWTEST_F(L0HwHelperTest, givenSingleThreadsWhenGettingBitmaskThenCorrectBitsAreSet) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    std::vector<EuThread::ThreadId> threads;
    threads.push_back({0, 0, 0, 0, 3});
    threads.push_back({0, 0, 0, 1, 0});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threads, hwInfo, bitmask, size);

    auto data = bitmask.get();
    EXPECT_EQ(1u << 3, data[0]);
    EXPECT_EQ(1u, data[1]);

    EXPECT_TRUE(memoryZeroed(&data[2], size - 2));
}

HWTEST_F(L0HwHelperTest, givenBitmaskWithAttentionBitsForSingleThreadWhenGettingThreadsThenSingleCorrectThreadReturned) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t subsliceID = subslicesPerSlice > 2 ? subslicesPerSlice - 2 : 0;

    uint32_t threadID = 3;
    std::vector<EuThread::ThreadId> threadsWithAtt;
    threadsWithAtt.push_back({0, 0, subsliceID, 0, threadID});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

    ASSERT_EQ(1u, threads.size());
    EXPECT_EQ(0u, threads[0].slice);
    EXPECT_EQ(subsliceID, threads[0].subslice);
    EXPECT_EQ(0u, threads[0].eu);
    EXPECT_EQ(threadID, threads[0].thread);

    EXPECT_EQ(0u, threads[0].tileIndex);

    threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 1, bitmask.get(), size);

    ASSERT_EQ(1u, threads.size());
    EXPECT_EQ(0u, threads[0].slice);
    EXPECT_EQ(subsliceID, threads[0].subslice);
    EXPECT_EQ(0u, threads[0].eu);
    EXPECT_EQ(threadID, threads[0].thread);

    EXPECT_EQ(1u, threads[0].tileIndex);
}

HWTEST_F(L0HwHelperTest, givenBitmaskWithAttentionBitsForAllSubslicesWhenGettingThreadsThenCorrectThreadsAreReturned) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t threadID = 0;

    std::vector<EuThread::ThreadId> threadsWithAtt;
    for (uint32_t subsliceID = 0; subsliceID < subslicesPerSlice; subsliceID++) {
        threadsWithAtt.push_back({0, 0, subsliceID, 0, threadID});
    }

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

    ASSERT_EQ(subslicesPerSlice, threads.size());

    for (uint32_t i = 0; i < subslicesPerSlice; i++) {
        EXPECT_EQ(0u, threads[i].slice);
        EXPECT_EQ(i, threads[i].subslice);
        EXPECT_EQ(0u, threads[i].eu);
        EXPECT_EQ(threadID, threads[i].thread);

        EXPECT_EQ(0u, threads[i].tileIndex);
    }
}

HWTEST_F(L0HwHelperTest, givenBitmaskWithAttentionBitsForAllEUsWhenGettingThreadsThenCorrectThreadsAreReturned) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    const auto numEUsPerSS = hwInfo.gtSystemInfo.MaxEuPerSubSlice;
    uint32_t threadID = 3;

    std::vector<EuThread::ThreadId> threadsWithAtt;
    for (uint32_t euId = 0; euId < numEUsPerSS; euId++) {
        threadsWithAtt.push_back({0, 0, 0, euId, threadID});
    }

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);
    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

    ASSERT_EQ(numEUsPerSS, threads.size());

    for (uint32_t i = 0; i < numEUsPerSS; i++) {
        EXPECT_EQ(0u, threads[i].slice);
        EXPECT_EQ(0u, threads[i].subslice);
        EXPECT_EQ(i, threads[i].eu);
        EXPECT_EQ(threadID, threads[i].thread);

        EXPECT_EQ(0u, threads[i].tileIndex);
    }
}

HWTEST_F(L0HwHelperTest, givenEu0To1Threads0To3BitmaskWhenGettingThreadsThenCorrectThreadsAreReturned) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint8_t data[2] = {0x0f, 0x0f};
    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, data, sizeof(data));

    ASSERT_EQ(8u, threads.size());

    ze_device_thread_t expectedThreads[] = {
        {0, 0, 0, 0},
        {0, 0, 0, 1},
        {0, 0, 0, 2},
        {0, 0, 0, 3},
        {0, 0, 1, 0},
        {0, 0, 1, 1},
        {0, 0, 1, 2},
        {0, 0, 1, 3}};

    for (uint32_t i = 0; i < 8u; i++) {
        EXPECT_EQ(expectedThreads[i].slice, threads[i].slice);
        EXPECT_EQ(expectedThreads[i].subslice, threads[i].subslice);
        EXPECT_EQ(expectedThreads[i].eu, threads[i].eu);
        EXPECT_EQ(expectedThreads[i].thread, threads[i].thread);

        EXPECT_EQ(0u, threads[i].tileIndex);
    }
}

HWTEST_F(L0HwHelperTest, givenBitmaskWithAttentionBitsForHalfOfThreadsWhenGettingThreadsThenCorrectThreadsAreReturned) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t threadID = 3;
    auto numOfActiveSubslices = ((subslicesPerSlice + 1) / 2);

    std::vector<EuThread::ThreadId> threadsWithAtt;
    for (uint32_t subsliceID = 0; subsliceID < numOfActiveSubslices; subsliceID++) {
        threadsWithAtt.push_back({0, 0, subsliceID, 0, threadID});
    }

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

    auto bitmaskSizePerSingleSubslice = size / hwInfo.gtSystemInfo.MaxSlicesSupported / subslicesPerSlice;

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), bitmaskSizePerSingleSubslice * numOfActiveSubslices);

    ASSERT_EQ(numOfActiveSubslices, threads.size());

    for (uint32_t i = 0; i < numOfActiveSubslices; i++) {
        EXPECT_EQ(0u, threads[i].slice);
        EXPECT_EQ(i, threads[i].subslice);
        EXPECT_EQ(0u, threads[i].eu);
        EXPECT_EQ(threadID, threads[i].thread);

        EXPECT_EQ(0u, threads[i].tileIndex);
    }
}

using PlatformsWithFusedEus = IsWithinGfxCore<IGFX_GEN12LP_CORE, IGFX_XE_HPG_CORE>;
using L0HwHelperFusedEuTest = ::testing::Test;

HWTEST2_F(L0HwHelperTest, givenBitmaskWithAttentionBitsWith8EUSSWhenGettingThreadsThenSingleCorrectThreadReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);
    hwInfo.gtSystemInfo.MaxEuPerSubSlice = 8;
    hwInfo.gtSystemInfo.MaxSubSlicesSupported = 64;
    hwInfo.gtSystemInfo.MaxDualSubSlicesSupported = 32;

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    for (uint32_t subsliceID = 0; subsliceID < 2; subsliceID++) {

        std::vector<EuThread::ThreadId> threadsWithAtt;
        threadsWithAtt.push_back({0, 0, subsliceID, 0, 0});

        l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

        auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

        ASSERT_EQ(2u, threads.size());

        EXPECT_EQ(0u, threads[0].slice);
        EXPECT_EQ(subsliceID, threads[0].subslice);
        EXPECT_EQ(0u, threads[0].eu);
        EXPECT_EQ(0u, threads[0].thread);
        EXPECT_EQ(0u, threads[0].tileIndex);

        EXPECT_EQ(0u, threads[1].slice);
        EXPECT_EQ(subsliceID, threads[1].subslice);
        EXPECT_EQ(4u, threads[1].eu);
        EXPECT_EQ(0u, threads[1].thread);
        EXPECT_EQ(0u, threads[1].tileIndex);
    }
}

HWTEST2_F(L0HwHelperFusedEuTest, givenDynamicallyPopulatesSliceInfoGreaterThanMaxSlicesSupportedThenBitmasksAreCorrect, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);
    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    hwInfo.gtSystemInfo.IsDynamicallyPopulated = true;
    hwInfo.gtSystemInfo.MaxSlicesSupported = 2;
    for (int i = 0; i < GT_MAX_SLICE; i++) {
        hwInfo.gtSystemInfo.SliceInfo[i].Enabled = false;
    }
    hwInfo.gtSystemInfo.SliceInfo[2].Enabled = true;
    hwInfo.gtSystemInfo.SliceInfo[3].Enabled = true;

    std::vector<EuThread::ThreadId> threadsWithAtt;
    threadsWithAtt.push_back({0, 2, 0, 0, 0});
    threadsWithAtt.push_back({0, 3, 0, 0, 0});
    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);
    const uint32_t numSubslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    const uint32_t numEuPerSubslice = std::min(hwInfo.gtSystemInfo.MaxEuPerSubSlice, 8u);
    const uint32_t numThreadsPerEu = (hwInfo.gtSystemInfo.ThreadCount / hwInfo.gtSystemInfo.EUCount);
    const uint32_t bytesPerEu = alignUp(numThreadsPerEu, 8) / 8;
    auto expectedSize = 4 * numSubslicesPerSlice * numEuPerSubslice * bytesPerEu;
    EXPECT_EQ(size, expectedSize);

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);
    ASSERT_EQ(threads.size(), 4u);
    EXPECT_EQ(threads[0], threadsWithAtt[0]);
    EXPECT_EQ(threads[2], threadsWithAtt[1]);
}

HWTEST2_F(L0HwHelperFusedEuTest, givenBitmaskWithAttentionBitsForSingleThreadWhenGettingThreadsThenThreadForTwoEUsReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t subsliceID = subslicesPerSlice > 2 ? subslicesPerSlice - 2 : 0;

    uint32_t threadID = 3;
    std::vector<EuThread::ThreadId> threadsWithAtt;
    threadsWithAtt.push_back({0, 0, subsliceID, 0, threadID});

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

    ASSERT_EQ(2u, threads.size());

    EXPECT_EQ(0u, threads[0].slice);
    EXPECT_EQ(subsliceID, threads[0].subslice);
    EXPECT_EQ(0u, threads[0].eu);
    EXPECT_EQ(threadID, threads[0].thread);
    EXPECT_EQ(0u, threads[0].tileIndex);

    EXPECT_EQ(0u, threads[1].slice);
    EXPECT_EQ(subsliceID, threads[1].subslice);
    EXPECT_EQ(4u, threads[1].eu);
    EXPECT_EQ(threadID, threads[1].thread);
    EXPECT_EQ(0u, threads[1].tileIndex);
}

HWTEST2_F(L0HwHelperFusedEuTest, givenBitmaskWithAttentionBitsForAllSubslicesWhenGettingThreadsThenCorrectThreadsForTwoEUsAreReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t threadID = 0;

    std::vector<EuThread::ThreadId> threadsWithAtt;
    for (uint32_t subsliceID = 0; subsliceID < subslicesPerSlice; subsliceID++) {
        threadsWithAtt.push_back({0, 0, subsliceID, 0, threadID});
    }

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

    ASSERT_EQ(2 * subslicesPerSlice, threads.size());

    auto threadIndex = 0;
    for (uint32_t i = 0; i < subslicesPerSlice; i++) {
        EXPECT_EQ(0u, threads[threadIndex].slice);
        EXPECT_EQ(i, threads[threadIndex].subslice);
        EXPECT_EQ(threadID, threads[threadIndex].thread);
        EXPECT_EQ(0u, threads[threadIndex].eu);

        threadIndex++;
        EXPECT_EQ(threadID, threads[threadIndex].thread);
        EXPECT_EQ(4u, threads[threadIndex].eu);
        threadIndex++;
    }
}

HWTEST2_F(L0HwHelperFusedEuTest, givenBitmaskWithAttentionBitsForAllEUsWhenGettingThreadsThenCorrectThreadsAreReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    const auto maxEUsInAtt = 8u;
    uint32_t threadID = 3;

    std::vector<EuThread::ThreadId> threadsWithAtt;
    for (uint32_t euId = 0; euId < maxEUsInAtt; euId++) {
        threadsWithAtt.push_back({0, 0, 0, euId, threadID});
    }

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);
    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), size);

    ASSERT_EQ(maxEUsInAtt, threads.size());

    uint32_t expectedEUs[] = {0, 4, 1, 5, 2, 6, 3, 7};
    for (uint32_t i = 0; i < threads.size(); i++) {

        EXPECT_EQ(0u, threads[i].slice);
        EXPECT_EQ(0u, threads[i].subslice);
        EXPECT_EQ(expectedEUs[i], threads[i].eu);
        EXPECT_EQ(threadID, threads[i].thread);
    }
}

HWTEST2_F(L0HwHelperFusedEuTest, givenEu0To1Threads0To3BitmaskWhenGettingThreadsThenCorrectThreadsAreReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint8_t data[2] = {0x0f, 0x0f};
    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, data, sizeof(data));

    ASSERT_EQ(16u, threads.size());

    ze_device_thread_t expectedThreads[] = {
        {0, 0, 0, 0}, {0, 0, 4, 0}, {0, 0, 0, 1}, {0, 0, 4, 1}, {0, 0, 0, 2}, {0, 0, 4, 2}, {0, 0, 0, 3}, {0, 0, 4, 3}, {0, 0, 1, 0}, {0, 0, 5, 0}, {0, 0, 1, 1}, {0, 0, 5, 1}, {0, 0, 1, 2}, {0, 0, 5, 2}, {0, 0, 1, 3}, {0, 0, 5, 3}};

    for (uint32_t i = 0; i < 16u; i++) {
        EXPECT_EQ(expectedThreads[i].slice, threads[i].slice);
        EXPECT_EQ(expectedThreads[i].subslice, threads[i].subslice);
        EXPECT_EQ(expectedThreads[i].eu, threads[i].eu);
        EXPECT_EQ(expectedThreads[i].thread, threads[i].thread);
    }
}

HWTEST2_F(L0HwHelperFusedEuTest, givenEu8To9Threads0To3BitmaskWhenGettingThreadsThenCorrectThreadsAreReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x0f, 0x0f};
    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, data, sizeof(data));

    ASSERT_EQ(16u, threads.size());

    ze_device_thread_t expectedThreads[] = {
        {0, 0, 8, 0}, {0, 0, 12, 0}, {0, 0, 8, 1}, {0, 0, 12, 1}, {0, 0, 8, 2}, {0, 0, 12, 2}, {0, 0, 8, 3}, {0, 0, 12, 3}, {0, 0, 9, 0}, {0, 0, 13, 0}, {0, 0, 9, 1}, {0, 0, 13, 1}, {0, 0, 9, 2}, {0, 0, 13, 2}, {0, 0, 9, 3}, {0, 0, 13, 3}};

    for (uint32_t i = 0; i < 16u; i++) {
        EXPECT_EQ(expectedThreads[i].slice, threads[i].slice);
        EXPECT_EQ(expectedThreads[i].subslice, threads[i].subslice);
        EXPECT_EQ(expectedThreads[i].eu, threads[i].eu);
        EXPECT_EQ(expectedThreads[i].thread, threads[i].thread);
    }
}

HWTEST2_F(L0HwHelperFusedEuTest, givenBitmaskWithAttentionBitsForHalfOfThreadsWhenGettingThreadsThenCorrectThreadsAreReturned, PlatformsWithFusedEus) {
    auto hwInfo = *NEO::defaultHwInfo.get();

    if (hwInfo.gtSystemInfo.MaxEuPerSubSlice <= 8) {
        GTEST_SKIP();
    }

    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    std::unique_ptr<uint8_t[]> bitmask;
    size_t size = 0;

    uint32_t subslicesPerSlice = hwInfo.gtSystemInfo.MaxSubSlicesSupported / hwInfo.gtSystemInfo.MaxSlicesSupported;
    uint32_t threadID = 3;
    auto numOfActiveSubslices = ((subslicesPerSlice + 1) / 2);

    std::vector<EuThread::ThreadId> threadsWithAtt;
    for (uint32_t subsliceID = 0; subsliceID < numOfActiveSubslices; subsliceID++) {
        threadsWithAtt.push_back({0, 0, subsliceID, 0, threadID});
    }

    l0HwHelper.getAttentionBitmaskForSingleThreads(threadsWithAtt, hwInfo, bitmask, size);

    auto bitmaskSizePerSingleSubslice = size / hwInfo.gtSystemInfo.MaxSlicesSupported / subslicesPerSlice;

    auto threads = l0HwHelper.getThreadsFromAttentionBitmask(hwInfo, 0, bitmask.get(), bitmaskSizePerSingleSubslice * numOfActiveSubslices);

    ASSERT_EQ(2 * numOfActiveSubslices, threads.size());

    uint32_t subsliceIndex = 0;
    for (uint32_t i = 0; i < threads.size(); i++) {

        if (i % 2 == 0) {
            EXPECT_EQ(0u, threads[i].slice);
            EXPECT_EQ(subsliceIndex, threads[i].subslice);
            EXPECT_EQ(0u, threads[i].eu);
            EXPECT_EQ(threadID, threads[i].thread);

        } else {
            EXPECT_EQ(0u, threads[i].slice);
            EXPECT_EQ(subsliceIndex, threads[i].subslice);
            EXPECT_EQ(4u, threads[i].eu);
            EXPECT_EQ(threadID, threads[i].thread);

            subsliceIndex++;
        }
    }
}

HWTEST2_F(L0HwHelperTest, GivenNonMultiTilePlatformsWhenCheckingL0HelperForMultiTileCapablePlatformThenReturnFalse, NonMultiTilePlatforms) {
    EXPECT_FALSE(L0::L0HwHelperHw<FamilyType>::get().multiTileCapablePlatform());
}

HWTEST2_F(L0HwHelperTest, whenAlwaysAllocateEventInLocalMemCalledThenReturnFalse, IsNotXeHpcCore) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    auto &l0HwHelper = L0::L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);
    EXPECT_FALSE(l0HwHelper.alwaysAllocateEventInLocalMem());
}

TEST_F(L0HwHelperTest, givenL0HelperWhenGettingDefaultValueForUsePipeControlMultiKernelEventSyncThenReturnFalse) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    bool defaultValue = L0::L0HwHelper::usePipeControlMultiKernelEventSync(hwInfo);
    EXPECT_FALSE(defaultValue);
}

TEST_F(L0HwHelperTest, givenL0HelperWhenGettingDefaultValueForCompactL3FlushEventPacketThenReturnFalse) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    bool defaultValue = L0::L0HwHelper::useCompactL3FlushEventPacket(hwInfo);
    EXPECT_FALSE(defaultValue);
}

TEST_F(L0HwHelperTest, givenL0HelperWhenGettingDefaultValueForDynamicEventPacketCountThenReturnFalse) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    bool defaultValue = L0::L0HwHelper::useDynamicEventPacketsCount(hwInfo);
    EXPECT_FALSE(defaultValue);
}

HWTEST2_F(L0HwHelperTest, givenL0HelperWhenGettingMaxKernelAndMaxPacketThenExpectBothReturnOne, NonMultiTilePlatforms) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    EXPECT_EQ(1u, L0::L0HwHelperHw<FamilyType>::get().getEventMaxKernelCount(hwInfo));
    EXPECT_EQ(1u, L0::L0HwHelperHw<FamilyType>::get().getEventBaseMaxPacketCount(hwInfo));
}

template <int32_t usePipeControlMultiPacketEventSync, int32_t compactL3FlushEventPacket>
struct L0HwHelperMultiPacketEventFixture {
    void setUp() {
        DebugManager.flags.UsePipeControlMultiKernelEventSync.set(usePipeControlMultiPacketEventSync);
        DebugManager.flags.CompactL3FlushEventPacket.set(compactL3FlushEventPacket);
    }

    void tearDown() {
    }

    DebugManagerStateRestore restorer;
};

using L0HwHelperEventMultiKernelEnabledL3FlushCompactDisabledTest = Test<L0HwHelperMultiPacketEventFixture<0, 0>>;
HWTEST2_F(L0HwHelperEventMultiKernelEnabledL3FlushCompactDisabledTest,
          givenL0HelperWhenGettingMaxKernelAndMaxPacketThenExpectKernelThreeAndPacketThreeWithL3PacketWhenApplicable,
          IsAtLeastXeHpCore) {
    auto hwInfo = *NEO::defaultHwInfo.get();

    uint32_t expectedPacket = 3;
    if (NEO::MemorySynchronizationCommands<FamilyType>::getDcFlushEnable(true, hwInfo)) {
        expectedPacket++;
    }

    EXPECT_EQ(3u, L0::L0HwHelperHw<FamilyType>::get().getEventMaxKernelCount(hwInfo));
    EXPECT_EQ(expectedPacket, L0::L0HwHelperHw<FamilyType>::get().getEventBaseMaxPacketCount(hwInfo));
}

using L0HwHelperEventMultiKernelEnabledL3FlushCompactEnabledTest = Test<L0HwHelperMultiPacketEventFixture<0, 1>>;
HWTEST2_F(L0HwHelperEventMultiKernelEnabledL3FlushCompactEnabledTest,
          givenL0HelperWhenGettingMaxKernelAndMaxPacketThenExpectKernelThreeAndPacketThree,
          IsAtLeastXeHpCore) {
    auto hwInfo = *NEO::defaultHwInfo.get();

    uint32_t expectedPacket = 3;

    EXPECT_EQ(3u, L0::L0HwHelperHw<FamilyType>::get().getEventMaxKernelCount(hwInfo));
    EXPECT_EQ(expectedPacket, L0::L0HwHelperHw<FamilyType>::get().getEventBaseMaxPacketCount(hwInfo));
}

using L0HwHelperEventMultiKernelDisabledL3FlushCompactDisabledTest = Test<L0HwHelperMultiPacketEventFixture<1, 0>>;
HWTEST2_F(L0HwHelperEventMultiKernelDisabledL3FlushCompactDisabledTest,
          givenL0HelperWhenGettingMaxKernelAndMaxPacketThenExpectKernelOneAndPacketOneWithL3PacketWhenApplicable,
          IsAtLeastXeHpCore) {
    auto hwInfo = *NEO::defaultHwInfo.get();

    uint32_t expectedPacket = 1;
    if (NEO::MemorySynchronizationCommands<FamilyType>::getDcFlushEnable(true, hwInfo)) {
        expectedPacket++;
    }

    EXPECT_EQ(1u, L0::L0HwHelperHw<FamilyType>::get().getEventMaxKernelCount(hwInfo));
    EXPECT_EQ(expectedPacket, L0::L0HwHelperHw<FamilyType>::get().getEventBaseMaxPacketCount(hwInfo));
}

using L0HwHelperEventMultiKernelDisabledL3FlushCompactEnabledTest = Test<L0HwHelperMultiPacketEventFixture<1, 1>>;
HWTEST2_F(L0HwHelperEventMultiKernelDisabledL3FlushCompactEnabledTest,
          givenL0HelperWhenGettingMaxKernelAndMaxPacketThenExpectKernelOneAndPacketOne,
          IsAtLeastXeHpCore) {
    auto hwInfo = *NEO::defaultHwInfo.get();

    uint32_t expectedPacket = 1;

    EXPECT_EQ(1u, L0::L0HwHelperHw<FamilyType>::get().getEventMaxKernelCount(hwInfo));
    EXPECT_EQ(expectedPacket, L0::L0HwHelperHw<FamilyType>::get().getEventBaseMaxPacketCount(hwInfo));
}

TEST_F(L0HwHelperTest, givenL0HelperWhenGettingDefaultValueForSignalAllEventPacketThenReturnFalse) {
    auto hwInfo = *NEO::defaultHwInfo.get();
    bool defaultValue = L0::L0HwHelper::useSignalAllEventPackets(hwInfo);
    EXPECT_FALSE(defaultValue);
}

} // namespace ult
} // namespace L0
