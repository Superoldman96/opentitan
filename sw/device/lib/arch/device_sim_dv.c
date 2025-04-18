// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include <stdbool.h>

#include "dt/dt_rv_core_ibex.h"
#include "sw/device/lib/arch/device.h"

#include "rv_core_ibex_regs.h"
#include "uart_regs.h"

// Use the first dt_rv_core_ibex_t enum, i.e. the first Ibex core instance.
static const dt_rv_core_ibex_t kRvCoreIbexDt = (dt_rv_core_ibex_t)0;
static_assert(kDtRvCoreIbexCount == 1, "Only single core tops are supported");

static inline uintptr_t rv_core_ibex_base(void) {
  return (uintptr_t)dt_rv_core_ibex_primary_reg_block(kRvCoreIbexDt);
}

/**
 * Device-specific symbol definitions for the DV simulation device.
 */

const device_type_t kDeviceType = kDeviceSimDV;

// TODO: DV testbench completely randomizes these. Need to add code to
// retrieve these from a preloaded memory location set by the testbench.

const uint64_t kClockFreqCpuMhz = 100;

const uint64_t kClockFreqCpuHz = kClockFreqCpuMhz * 1000 * 1000;

uint64_t to_cpu_cycles(uint64_t usec) { return usec * kClockFreqCpuMhz; }

const uint64_t kClockFreqHiSpeedPeripheralHz = 96 * 1000 * 1000;  // 96MHz

const uint64_t kClockFreqPeripheralHz = 24 * 1000 * 1000;  // 24MHz

const uint64_t kClockFreqUsbHz = 48 * 1000 * 1000;  // 48MHz

const uint64_t kClockFreqAonHz = 200 * 1000;  // 200kHz

const uint64_t kUartBaudrate = 1 * 1000 * 1000;  // 1Mbps

const uint32_t kUartNCOValue =
    CALCULATE_UART_NCO(kUartBaudrate, kClockFreqPeripheralHz);

const uint32_t kUartBaud115K =
    CALCULATE_UART_NCO(115200, kClockFreqPeripheralHz);
const uint32_t kUartBaud230K =
    CALCULATE_UART_NCO(115200 * 2, kClockFreqPeripheralHz);
const uint32_t kUartBaud460K =
    CALCULATE_UART_NCO(115200 * 4, kClockFreqPeripheralHz);
const uint32_t kUartBaud921K =
    CALCULATE_UART_NCO(115200 * 8, kClockFreqPeripheralHz);
const uint32_t kUartBaud1M33 =
    CALCULATE_UART_NCO(1333333, kClockFreqPeripheralHz);
const uint32_t kUartBaud1M50 =
    CALCULATE_UART_NCO(1500000, kClockFreqPeripheralHz);

const uint32_t kUartTxFifoCpuCycles = CALCULATE_UART_TX_FIFO_CPU_CYCLES(
    kUartBaudrate, kClockFreqCpuHz, UART_PARAM_TX_FIFO_DEPTH);

const uint32_t kAstCheckPollCpuCycles =
    CALCULATE_AST_CHECK_POLL_CPU_CYCLES(kClockFreqCpuHz);

uintptr_t device_test_status_address(void) {
  return rv_core_ibex_base() + RV_CORE_IBEX_DV_SIM_WINDOW_REG_OFFSET;
}

uintptr_t device_log_bypass_uart_address(void) {
  return rv_core_ibex_base() + RV_CORE_IBEX_DV_SIM_WINDOW_REG_OFFSET + 0x04;
}

void device_fpga_version_print(void) {}
