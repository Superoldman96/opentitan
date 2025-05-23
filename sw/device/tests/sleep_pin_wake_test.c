// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/base/mmio.h"
#include "sw/device/lib/dif/dif_pinmux.h"
#include "sw/device/lib/dif/dif_pwrmgr.h"
#include "sw/device/lib/dif/dif_rv_plic.h"
#include "sw/device/lib/runtime/irq.h"
#include "sw/device/lib/runtime/log.h"
#include "sw/device/lib/testing/flash_ctrl_testutils.h"
#include "sw/device/lib/testing/pwrmgr_testutils.h"
#include "sw/device/lib/testing/rand_testutils.h"
#include "sw/device/lib/testing/rv_plic_testutils.h"
#include "sw/device/lib/testing/test_framework/check.h"
#include "sw/device/lib/testing/test_framework/ottf_main.h"
#include "sw/device/lib/testing/test_framework/ottf_utils.h"

#include "hw/top_earlgrey/sw/autogen/top_earlgrey.h"
// Below includes are generated during compile time.
#include "flash_ctrl_regs.h"
#include "pinmux_regs.h"

/* We need control flow for the ujson messages exchanged
 * with the host in OTTF_WAIT_FOR on real devices. */
OTTF_DEFINE_TEST_CONFIG(.enable_uart_flow_control = true);

static const dt_pwrmgr_t kPwrmgrDt = 0;
static_assert(kDtPwrmgrCount == 1, "this library expects exactly one pwrmgr");
static const dt_pinmux_t kPinmuxDt = 0;
static_assert(kDtPinmuxCount == 1, "this library expects exactly one pinmux");
static const dt_rv_plic_t kRvPlicDt = 0;
static_assert(kDtRvPlicCount == 1, "this library expects exactly one rv_plic");
static const dt_flash_ctrl_t kFlashCtrlDt = 0;
static_assert(kDtFlashCtrlCount >= 1,
              "this library expects at least one flash_ctrl");

// PLIC structures
static dif_pwrmgr_t pwrmgr;
static dif_pinmux_t pinmux;
static dif_rv_plic_t plic;
static dif_flash_ctrl_state_t flash_ctrl_state;

enum {
  kPlicTarget = 0,
};

static const uint32_t kNumDio = 16;  // top_earlgrey has 16 DIOs

// kDirectDio is a list of Dio index that TB cannot control the PAD value.
// The list should be incremental order (see the code below)
#define NUM_DIRECT_DIO 5
static const uint32_t kDirectDio[NUM_DIRECT_DIO] = {6, 12, 13, 14, 15};

// Preserve wakeup_detector_selected over multiple resets
OT_SET_BSS_SECTION(".non_volatile_scratch", uint32_t wakeup_detector_idx;)

OTTF_BACKDOOR_VAR
int8_t sival_mio_pad = -1;  // -1 means not assigned yet.
OTTF_BACKDOOR_VAR
int8_t sival_wakeup_detector_idx = -1;  // -1 means not assigned yet.
OTTF_BACKDOOR_VAR
bool sival_ready_to_sleep = false;

/**
 * External interrupt handler.
 */
bool ottf_handle_irq(uint32_t *exc_info, dt_instance_id_t devid,
                     dif_rv_plic_irq_id_t irq_id) {
  if (devid == dt_pwrmgr_instance_id(kPwrmgrDt) &&
      irq_id == dt_pwrmgr_irq_to_plic_id(kPwrmgrDt, kDtPwrmgrIrqWakeup)) {
    CHECK_DIF_OK(dif_pwrmgr_irq_acknowledge(&pwrmgr, kDtPwrmgrIrqWakeup));
    return true;
  } else {
    return false;
  }
}

bool test_main(void) {
  dif_pinmux_wakeup_config_t wakeup_cfg;

  // Default Deep Power Down
  dif_pwrmgr_domain_config_t pwrmgr_domain_cfg = 0;

  // Enable global and external IRQ at Ibex.
  irq_global_ctrl(true);
  irq_external_ctrl(true);

  // Initialize power manager
  CHECK_DIF_OK(dif_pwrmgr_init_from_dt(kPwrmgrDt, &pwrmgr));
  CHECK_DIF_OK(dif_rv_plic_init_from_dt(kRvPlicDt, &plic));
  CHECK_DIF_OK(dif_pinmux_init_from_dt(kPinmuxDt, &pinmux));
  CHECK_DIF_OK(
      dif_flash_ctrl_init_state_from_dt(&flash_ctrl_state, kFlashCtrlDt));

  // Wakeup source for pinmux.
  dif_pwrmgr_request_sources_t wakeup_sources;
  CHECK_DIF_OK(dif_pwrmgr_find_request_source(
      &pwrmgr, kDifPwrmgrReqTypeWakeup, dt_pinmux_instance_id(kPinmuxDt),
      kDtPinmuxWakeupPinWkupReq, &wakeup_sources));

  if (kDeviceType == kDeviceSimDV) {
    // Enable access to flash for storing info across resets.
    CHECK_STATUS_OK(
        flash_ctrl_testutils_default_region_access(&flash_ctrl_state,
                                                   /*rd_en*/ true,
                                                   /*prog_en*/ true,
                                                   /*erase_en*/ true,
                                                   /*scramble_en*/ false,
                                                   /*ecc_en*/ false,
                                                   /*he_en*/ false));
  }

  // Randomly pick one of the wakeup detectors
  // only after the first boot.
  dif_pinmux_index_t wakeup_detector_selected = 0;

  if (UNWRAP(pwrmgr_testutils_is_wakeup_reason(&pwrmgr, 0)) == true) {
    LOG_INFO("Test in POR phase");

    // After randomly generated wake detector index,
    // store to non volatile area to preserve from the reset event.
    wakeup_detector_selected =
        rand_testutils_gen32_range(0, PINMUX_PARAM_N_WKUP_DETECT - 1);
    if (kDeviceType == kDeviceSimDV) {
      CHECK_STATUS_OK(flash_ctrl_testutils_write(
          &flash_ctrl_state,
          (uint32_t)(&wakeup_detector_idx) -
              TOP_EARLGREY_FLASH_CTRL_MEM_BASE_ADDR,
          0, &wakeup_detector_selected, kDifFlashCtrlPartitionTypeData, 1));
    }
    LOG_INFO("detector %d is selected", wakeup_detector_selected);
    // TODO(lowrisc/opentitan#15889): The weak pull on IOC3 needs to be
    // disabled for this test. Remove this later.
    dif_pinmux_pad_attr_t out_attr;
    dif_pinmux_pad_attr_t in_attr = {0};
    CHECK_DIF_OK(dif_pinmux_pad_write_attrs(&pinmux, kTopEarlgreyMuxedPadsIoc3,
                                            kDifPinmuxPadKindMio, in_attr,
                                            &out_attr));

    // This print is placed here on purpose.
    // sv sequence is waiting for this print log followed by
    // Pad Section. So do not put any other print between
    // this LOG_INFO and LOG_INFO("Pad Selection");
    LOG_INFO("pinmux_init end");
    // Random choose low power or deep powerdown
    uint32_t deep_powerdown_en = rand_testutils_gen32_range(0, 1);

    // Prepare which PAD SW want to select. In SiVal we only test the MIO pads,
    // as the DIO ones are generally not testable with our setup.
    uint32_t mio0_dio1 =
        (kDeviceType == kDeviceSimDV) ? rand_testutils_gen32_range(0, 1) : 0;
    uint32_t pad_sel = 0;

    // Enable AonWakeup Interrupt if normal sleep
    if (deep_powerdown_en == 0) {
      // Enable all the AON interrupts used in this test.
      dif_rv_plic_irq_id_t irq_id =
          dt_pwrmgr_irq_to_plic_id(kPwrmgrDt, kDtPwrmgrIrqWakeup);
      rv_plic_testutils_irq_range_enable(&plic, kPlicTarget, irq_id, irq_id);
      // Enable pwrmgr interrupt
      CHECK_DIF_OK(dif_pwrmgr_irq_set_enabled(&pwrmgr, 0, kDifToggleEnabled));
    }

    // SpiDev CLK(idx 12), CS#(idx 13), D0(idx 6) and SpiHost CLK (14), CS#
    // (15) are directly connected to the SPI IF. Cannot control them. Roll 3
    // less and compensated later.
    if (mio0_dio1) {
      // DIO
      pad_sel = rand_testutils_gen32_range(0, kNumDio - 1 - NUM_DIRECT_DIO);

      for (int i = 0; i < NUM_DIRECT_DIO; i++) {
        if (pad_sel >= kDirectDio[i]) {
          pad_sel++;
        }
      }
      LOG_INFO("Pad Selection: %d / %d", mio0_dio1, pad_sel);
    } else {
      // MIO: 0, 1 are tie-0, tie-1
      if (kDeviceType == kDeviceSimDV) {
        pad_sel = rand_testutils_gen32_range(2, kTopEarlgreyPinmuxInselLast);
      } else {
        OTTF_WAIT_FOR(sival_mio_pad != -1, 1000000);
        pad_sel = (uint32_t)sival_mio_pad + 2;  // skip 0, 1 (see above)
      }

      LOG_INFO("Pad Selection: %d / %d", mio0_dio1, pad_sel - 2);
    }

    if (mio0_dio1 == 0) {
      // TODO: Check if the PAD is locked (kDifPinmuxLockTargetOutsel), then
      // skip if locked.
      // Turn off Pinmux output selection
      CHECK_DIF_OK(dif_pinmux_output_select(
          &pinmux, pad_sel - 2, kTopEarlgreyPinmuxOutselConstantHighZ));
    }

    wakeup_cfg.mode = kDifPinmuxWakeupModePositiveEdge;
    wakeup_cfg.signal_filter = false;
    wakeup_cfg.pad_type = mio0_dio1;
    wakeup_cfg.pad_select = pad_sel;

    // TODO: A better test would be to program ALL wakeup detectors with
    // randomly chosen pin wakeup sources and prove the right wakeup source
    // woke up the chip from sleep.
    CHECK_DIF_OK(dif_pinmux_wakeup_detector_enable(
        &pinmux, wakeup_detector_selected, wakeup_cfg));

    if (deep_powerdown_en == 0) {
      CHECK_DIF_OK(dif_pwrmgr_get_domain_config(&pwrmgr, &pwrmgr_domain_cfg));
      pwrmgr_domain_cfg |= kDifPwrmgrDomainOptionMainPowerInLowPower;
    }

    if (kDeviceType != kDeviceSimDV) {
      OTTF_WAIT_FOR(sival_ready_to_sleep != 0, 1000000);
    }

    // Enter low power
    CHECK_STATUS_OK(pwrmgr_testutils_enable_low_power(&pwrmgr, wakeup_sources,
                                                      pwrmgr_domain_cfg));

    LOG_INFO("Entering low power mode.");
    wait_for_interrupt();
  }

  if (UNWRAP(pwrmgr_testutils_is_wakeup_reason(&pwrmgr, wakeup_sources))) {
    LOG_INFO("Test in post-sleep pin wakeup phase");
    uint32_t wakeup_cause;
    CHECK_DIF_OK(dif_pinmux_wakeup_cause_get(&pinmux, &wakeup_cause));
    // Get the wakeup dectector index from stored variable.
    if (kDeviceType == kDeviceSimDV) {
      wakeup_detector_selected = wakeup_detector_idx;
    } else {
      OTTF_WAIT_FOR(sival_wakeup_detector_idx != -1, 1000000);
      wakeup_detector_selected = (uint32_t)sival_wakeup_detector_idx;
    }

    LOG_INFO("wakeup_cause: %x %d %d", wakeup_cause,
             1 << wakeup_detector_selected, wakeup_detector_selected);
    CHECK(wakeup_cause == 1 << wakeup_detector_selected);
    CHECK_DIF_OK(
        dif_pinmux_wakeup_detector_disable(&pinmux, wakeup_detector_selected));
    return true;

  } else {
    // Other wakeup. This is a failure.
    dif_pwrmgr_wakeup_reason_t wakeup_reason;
    CHECK_DIF_OK(dif_pwrmgr_wakeup_reason_get(&pwrmgr, &wakeup_reason));
    LOG_ERROR("Unexpected wakeup detected: type = %d, request_source = %d",
              wakeup_reason.types, wakeup_reason.request_sources);
    return false;
  }

  return false;
}

#undef NUM_DIRECT_DIO
#undef NUM_LOCKED_MIO
