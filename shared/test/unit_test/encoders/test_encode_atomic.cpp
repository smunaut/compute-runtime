/*
 * Copyright (C) 2020-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/test/common/cmd_parse/gen_cmd_parse.h"
#include "shared/test/common/helpers/unit_test_helper.h"
#include "shared/test/common/test_macros/hw_test.h"
#include "shared/test/unit_test/fixtures/command_container_fixture.h"

using namespace NEO;

using CommandEncodeAtomic = Test<CommandEncodeStatesFixture>;

HWTEST_F(CommandEncodeAtomic, WhenProgrammingMiAtomicThenExpectAllFieldsSetCorrectly) {
    using MI_ATOMIC = typename FamilyType::MI_ATOMIC;
    using ATOMIC_OPCODES = typename FamilyType::MI_ATOMIC::ATOMIC_OPCODES;
    using DATA_SIZE = typename FamilyType::MI_ATOMIC::DATA_SIZE;

    constexpr size_t bufferSize = 128u;
    uint8_t buffer[bufferSize];
    uint64_t address = static_cast<uint64_t>(0x123400);
    LinearStream cmdbuffer(buffer, bufferSize);

    EncodeAtomic<FamilyType>::programMiAtomic(cmdbuffer,
                                              address,
                                              ATOMIC_OPCODES::ATOMIC_4B_DECREMENT,
                                              DATA_SIZE::DATA_SIZE_DWORD,
                                              0x1u,
                                              0x1u,
                                              0x0u,
                                              0x0u);

    MI_ATOMIC *miAtomicCmd = reinterpret_cast<MI_ATOMIC *>(cmdbuffer.getCpuBase());

    EXPECT_EQ(ATOMIC_OPCODES::ATOMIC_4B_DECREMENT, miAtomicCmd->getAtomicOpcode());
    EXPECT_EQ(DATA_SIZE::DATA_SIZE_DWORD, miAtomicCmd->getDataSize());
    EXPECT_EQ(address, UnitTestHelper<FamilyType>::getAtomicMemoryAddress(*miAtomicCmd));
    EXPECT_EQ(0x1u, miAtomicCmd->getReturnDataControl());
    EXPECT_EQ(0x1u, miAtomicCmd->getCsStall());
}

HWTEST_F(CommandEncodeAtomic, WhenProgrammingMiAtomicMoveOperationThenExpectInlineDataSet) {
    using MI_ATOMIC = typename FamilyType::MI_ATOMIC;
    using ATOMIC_OPCODES = typename FamilyType::MI_ATOMIC::ATOMIC_OPCODES;
    using DATA_SIZE = typename FamilyType::MI_ATOMIC::DATA_SIZE;
    using DWORD_LENGTH = typename FamilyType::MI_ATOMIC::DWORD_LENGTH;

    constexpr size_t bufferSize = sizeof(MI_ATOMIC) * 4;
    uint8_t buffer[bufferSize];
    uint64_t address = (static_cast<uint64_t>(3) << 32) + 0x123400;
    LinearStream cmdbuffer(buffer, bufferSize);

    constexpr uint32_t operand1DataLow = 0x10u;
    constexpr uint32_t operand1DataHigh = 0x20u;
    constexpr uint64_t operand1Data = (static_cast<uint64_t>(operand1DataHigh) << 32) | operand1DataLow;

    constexpr uint32_t operand2DataLow = 0x1fu;
    constexpr uint32_t operand2DataHigh = 0x2fu;
    constexpr uint64_t operand2Data = (static_cast<uint64_t>(operand2DataHigh) << 32) | operand2DataLow;

    EncodeAtomic<FamilyType>::programMiAtomic(cmdbuffer,
                                              address,
                                              ATOMIC_OPCODES::ATOMIC_4B_MOVE,
                                              DATA_SIZE::DATA_SIZE_DWORD,
                                              0x0u,
                                              0x0u,
                                              operand1Data,
                                              operand2Data);

    EncodeAtomic<FamilyType>::programMiAtomic(cmdbuffer,
                                              address,
                                              ATOMIC_OPCODES::ATOMIC_8B_MOVE,
                                              DATA_SIZE::DATA_SIZE_QWORD,
                                              0x0u,
                                              0x0u,
                                              operand1Data,
                                              operand2Data);

    EncodeAtomic<FamilyType>::programMiAtomic(cmdbuffer,
                                              address,
                                              ATOMIC_OPCODES::ATOMIC_8B_CMP_WR,
                                              DATA_SIZE::DATA_SIZE_QWORD,
                                              0x0u,
                                              0x0u,
                                              operand1Data,
                                              operand2Data);

    EncodeAtomic<FamilyType>::programMiAtomic(cmdbuffer,
                                              address,
                                              ATOMIC_OPCODES::ATOMIC_8B_ADD,
                                              DATA_SIZE::DATA_SIZE_QWORD,
                                              0x0u,
                                              0x0u,
                                              operand1Data,
                                              operand2Data);

    MI_ATOMIC *miAtomicCmd = reinterpret_cast<MI_ATOMIC *>(cmdbuffer.getCpuBase());

    EXPECT_EQ(ATOMIC_OPCODES::ATOMIC_4B_MOVE, miAtomicCmd->getAtomicOpcode());
    EXPECT_EQ(DATA_SIZE::DATA_SIZE_DWORD, miAtomicCmd->getDataSize());
    EXPECT_EQ(address, UnitTestHelper<FamilyType>::getAtomicMemoryAddress(*miAtomicCmd));
    EXPECT_EQ(0x0u, miAtomicCmd->getReturnDataControl());
    EXPECT_EQ(DWORD_LENGTH::DWORD_LENGTH_INLINE_DATA_1, miAtomicCmd->getDwordLength());
    EXPECT_EQ(0x1u, miAtomicCmd->getInlineData());
    EXPECT_EQ(operand1DataLow, miAtomicCmd->getOperand1DataDword0());
    EXPECT_EQ(operand1DataHigh, miAtomicCmd->getOperand1DataDword1());
    EXPECT_EQ(operand2DataLow, miAtomicCmd->getOperand2DataDword0());
    EXPECT_EQ(operand2DataHigh, miAtomicCmd->getOperand2DataDword1());

    miAtomicCmd++;
    EXPECT_EQ(ATOMIC_OPCODES::ATOMIC_8B_MOVE, miAtomicCmd->getAtomicOpcode());
    EXPECT_EQ(DATA_SIZE::DATA_SIZE_QWORD, miAtomicCmd->getDataSize());
    EXPECT_EQ(address, UnitTestHelper<FamilyType>::getAtomicMemoryAddress(*miAtomicCmd));
    EXPECT_EQ(0x0u, miAtomicCmd->getReturnDataControl());
    EXPECT_EQ(DWORD_LENGTH::DWORD_LENGTH_INLINE_DATA_1, miAtomicCmd->getDwordLength());
    EXPECT_EQ(0x1u, miAtomicCmd->getInlineData());
    EXPECT_EQ(operand1DataLow, miAtomicCmd->getOperand1DataDword0());
    EXPECT_EQ(operand1DataHigh, miAtomicCmd->getOperand1DataDword1());
    EXPECT_EQ(operand2DataLow, miAtomicCmd->getOperand2DataDword0());
    EXPECT_EQ(operand2DataHigh, miAtomicCmd->getOperand2DataDword1());

    miAtomicCmd++;
    EXPECT_EQ(ATOMIC_OPCODES::ATOMIC_8B_CMP_WR, miAtomicCmd->getAtomicOpcode());
    EXPECT_EQ(DATA_SIZE::DATA_SIZE_QWORD, miAtomicCmd->getDataSize());
    EXPECT_EQ(address, UnitTestHelper<FamilyType>::getAtomicMemoryAddress(*miAtomicCmd));
    EXPECT_EQ(0x0u, miAtomicCmd->getReturnDataControl());
    EXPECT_EQ(DWORD_LENGTH::DWORD_LENGTH_INLINE_DATA_1, miAtomicCmd->getDwordLength());
    EXPECT_EQ(0x1u, miAtomicCmd->getInlineData());
    EXPECT_EQ(operand1DataLow, miAtomicCmd->getOperand1DataDword0());
    EXPECT_EQ(operand1DataHigh, miAtomicCmd->getOperand1DataDword1());
    EXPECT_EQ(operand2DataLow, miAtomicCmd->getOperand2DataDword0());
    EXPECT_EQ(operand2DataHigh, miAtomicCmd->getOperand2DataDword1());

    miAtomicCmd++;
    EXPECT_EQ(ATOMIC_OPCODES::ATOMIC_8B_ADD, miAtomicCmd->getAtomicOpcode());
    EXPECT_EQ(DATA_SIZE::DATA_SIZE_QWORD, miAtomicCmd->getDataSize());
    EXPECT_EQ(address, UnitTestHelper<FamilyType>::getAtomicMemoryAddress(*miAtomicCmd));
    EXPECT_EQ(0x0u, miAtomicCmd->getReturnDataControl());
    EXPECT_EQ(DWORD_LENGTH::DWORD_LENGTH_INLINE_DATA_1, miAtomicCmd->getDwordLength());
    EXPECT_EQ(0x1u, miAtomicCmd->getInlineData());
    EXPECT_EQ(operand1DataLow, miAtomicCmd->getOperand1DataDword0());
    EXPECT_EQ(operand1DataHigh, miAtomicCmd->getOperand1DataDword1());
    EXPECT_EQ(operand2DataLow, miAtomicCmd->getOperand2DataDword0());
    EXPECT_EQ(operand2DataHigh, miAtomicCmd->getOperand2DataDword1());
}
