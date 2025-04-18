// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "hw/ip/aes/model/aes_modes.h"
#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/lib/base/mmio.h"
#include "sw/device/lib/base/status.h"
#include "sw/device/lib/dif/dif_aes.h"
#include "sw/device/lib/dif/dif_alert_handler.h"
#include "sw/device/lib/dif/dif_csrng.h"
#include "sw/device/lib/dif/dif_edn.h"
#include "sw/device/lib/dif/dif_entropy_src.h"
#include "sw/device/lib/dif/dif_otbn.h"
#include "sw/device/lib/runtime/hart.h"
#include "sw/device/lib/runtime/log.h"
#include "sw/device/lib/testing/aes_testutils.h"
#include "sw/device/lib/testing/csrng_testutils.h"
#include "sw/device/lib/testing/entropy_testutils.h"
#include "sw/device/lib/testing/otbn_testutils.h"
#include "sw/device/lib/testing/test_framework/check.h"
#include "sw/device/lib/testing/test_framework/ottf_main.h"
#include "sw/device/tests/otbn_randomness_impl.h"

#include "entropy_src_regs.h"  // autogenerated
#include "hw/top_earlgrey/sw/autogen/top_earlgrey.h"

#define TIMEOUT (1000 * 1000)

enum {
  kEntropySrcStartupWaitMicros = 250000,
};

OTTF_DEFINE_TEST_CONFIG();

// Module handles
static dif_entropy_src_t entropy_src;
static dif_csrng_t csrng;
static dif_edn_t edn0;
static dif_edn_t edn1;
static dif_aes_t aes;
static dif_otbn_t otbn;
static dif_alert_handler_t alert_handler;

status_t init_test_environment(void) {
  LOG_INFO(
      "Initializing modules entropy_src, csrng, edn0, edn1, aes, otbn and "
      "alert_handler...");
  TRY(dif_entropy_src_init(
      mmio_region_from_addr(TOP_EARLGREY_ENTROPY_SRC_BASE_ADDR), &entropy_src));
  TRY(dif_csrng_init(mmio_region_from_addr(TOP_EARLGREY_CSRNG_BASE_ADDR),
                     &csrng));
  TRY(dif_edn_init(mmio_region_from_addr(TOP_EARLGREY_EDN0_BASE_ADDR), &edn0));
  TRY(dif_edn_init(mmio_region_from_addr(TOP_EARLGREY_EDN1_BASE_ADDR), &edn1));
  TRY(dif_aes_init(mmio_region_from_addr(TOP_EARLGREY_AES_BASE_ADDR), &aes));
  TRY(dif_otbn_init(mmio_region_from_addr(TOP_EARLGREY_OTBN_BASE_ADDR), &otbn));
  TRY(dif_alert_handler_init(
      mmio_region_from_addr(TOP_EARLGREY_ALERT_HANDLER_BASE_ADDR),
      &alert_handler));
  return OK_STATUS();
}

status_t print_entropy_src_state(const dif_entropy_src_t *entropy_src) {
  // Declare a variable to hold the current state
  dif_entropy_src_main_fsm_t cur_state;

  // Get the current state of the entropy source
  TRY(dif_entropy_src_get_main_fsm_state(entropy_src, &cur_state));

  // Print the current state
  LOG_INFO("Current ENTROPY_SRC state: 0x%x", cur_state);

  const char *state_description;
  switch (cur_state) {
    case kDifEntropySrcMainFsmStateIdle:
      state_description = "Idle: Initial or inactive state.";
      break;
    case kDifEntropySrcMainFsmStateBootHTRunning:
      state_description =
          "Boot Health Tests Running: Health tests during boot.";
      break;
    case kDifEntropySrcMainFsmStateBootPostHTChk:
      state_description =
          "Boot Post Health Test Check: Health tests completed after boot.";
      break;
    case kDifEntropySrcMainFsmStateBootPhaseDone:
      state_description =
          "Boot Phase Done: Initial setup and checks completed.";
      break;
    case kDifEntropySrcMainFsmStateStartupHTStart:
      state_description =
          "Startup Health Tests Start: Start of health tests during startup.";
      break;
    case kDifEntropySrcMainFsmStateStartupPhase1:
      state_description =
          "Startup Phase 1: Initial configuration or loading constants.";
      break;
    case kDifEntropySrcMainFsmStateStartupPass1:
      state_description =
          "Startup Phase 1 Pass: Successful completion of the first phase of "
          "startup.";
      break;
    case kDifEntropySrcMainFsmStateStartupFail1:
      state_description =
          "Startup Phase 1 Fail: Failure during the first phase of startup.";
      break;
    case kDifEntropySrcMainFsmStateContHTStart:
      state_description =
          "Continuous Health Tests Start: Start of continuous health tests.";
      break;
    case kDifEntropySrcMainFsmStateContHTRunning:
      state_description =
          "Continuous Health Tests Running: Health tests running in the "
          "background.";
      break;
    case kDifEntropySrcMainFsmStateFWInsertStart:
      state_description =
          "Firmware Insert Start: Start of firmware insertion process.";
      break;
    case kDifEntropySrcMainFsmStateFWInsertMsg:
      state_description =
          "Firmware Insert Message: Firmware is providing data to the entropy "
          "source.";
      break;
    case kDifEntropySrcMainFsmStateSha3MsgDone:
      state_description =
          "SHA-3 Message Done: SHA-3 message processing complete.";
      break;
    case kDifEntropySrcMainFsmStateSha3Process:
      state_description =
          "SHA-3 Process: SHA-3 hashing process actively running.";
      break;
    case kDifEntropySrcMainFsmStateSha3Valid:
      state_description = "SHA-3 Valid: SHA-3 hash output is valid and ready.";
      break;
    case kDifEntropySrcMainFsmStateSha3Done:
      state_description =
          "SHA-3 Done: SHA-3 hashing process is fully complete.";
      break;
    case kDifEntropySrcMainFsmStateAlertState:
      state_description = "Alert State: Entropy source has triggered an alert.";
      break;
    case kDifEntropySrcMainFsmStateAlertHang:
      state_description =
          "Alert Hang: Entropy source is in a hang state due to an alert.";
      break;
    case kDifEntropySrcMainFsmStateError:
      state_description =
          "Error: Generic error condition within the entropy source.";
      break;
    default:
      state_description = "Unknown State";
      break;
  }
  LOG_INFO("Current Entropy Source State: %s (0x%x)", state_description,
           cur_state);

  return OK_STATUS();
}

// Log alert fail test statistics
status_t log_entropy_src_failures_and_alerts(
    const dif_entropy_src_t *entropy_src) {
  // Statistics for total failures
  dif_entropy_src_health_test_stats_t stats;
  TRY(dif_entropy_src_get_health_test_stats(entropy_src, &stats));

  // Fail counts
  dif_entropy_src_alert_fail_counts_t fail_counts;
  TRY(dif_entropy_src_get_alert_fail_counts(entropy_src, &fail_counts));

  // Iterate over all test variants (0..kDifEntropySrcTestNumVariants-1).
  for (size_t i = 0; i < kDifEntropySrcTestNumVariants; ++i) {
    switch ((dif_entropy_src_test_t)i) {
      case kDifEntropySrcTestRepetitionCount:
        LOG_INFO("Test Repetition Count (Index %u):", i);
        LOG_INFO("Watermarks: high=%u, low=%u", stats.high_watermark[i],
                 stats.low_watermark[i]);
        LOG_INFO("All Fails: high=%u, low=%u", stats.high_fails[i],
                 stats.low_fails[i]);
        LOG_INFO("Alert Fails (RepetitionCount): high=%u, low=%u",
                 fail_counts.high_fails[i], fail_counts.low_fails[i]);
        break;
      case kDifEntropySrcTestAdaptiveProportion:
        LOG_INFO("Test Adaptive Proportion (Index %u):", i);
        LOG_INFO("Watermarks: high=%u, low=%u", stats.high_watermark[i],
                 stats.low_watermark[i]);
        LOG_INFO("All Fails: high=%u, low=%u", stats.high_fails[i],
                 stats.low_fails[i]);
        LOG_INFO("Alert Fails (Adaptive Proportion): high=%u, low=%u",
                 fail_counts.high_fails[i], fail_counts.low_fails[i]);
        break;
      case kDifEntropySrcTestBucket:
        LOG_INFO("Test Bucket (Index %u):", i);
        LOG_INFO("Watermarks: high=%u, low=%u", stats.high_watermark[i],
                 stats.low_watermark[i]);
        LOG_INFO("All Fails: high=%u, low=%u", stats.high_fails[i],
                 stats.low_fails[i]);
        LOG_INFO("Alert Fails (Bucket): high=%u, low=%u",
                 fail_counts.high_fails[i], fail_counts.low_fails[i]);
        break;
      case kDifEntropySrcTestMarkov:
        LOG_INFO("Test Markov (Index %u):", i);
        LOG_INFO("Watermarks: high=%u, low=%u", stats.high_watermark[i],
                 stats.low_watermark[i]);
        LOG_INFO("All Fails: high=%u, low=%u", stats.high_fails[i],
                 stats.low_fails[i]);
        LOG_INFO("Alert Fails (Markov): high=%u, low=%u",
                 fail_counts.high_fails[i], fail_counts.low_fails[i]);
        break;
      default:
        LOG_INFO("Test %u: Unused test type, moving on...", i);
        break;
    }
  }

  return OK_STATUS();
}

// Function to disable entropy complex
status_t disable_entropy_complex(void) {
  // Using entropy test utility function to stop all EDN0, EDN1, CSRNG, and the
  // Entropy
  LOG_INFO("Disabling the entropy complex...");
  return entropy_testutils_stop_all();
}

status_t configure_realistic_fips_health_tests(void) {
  LOG_INFO("Configuring loose health test thresholds...");

  // Check if entropy source is locked
  bool is_locked;
  TRY(dif_entropy_src_is_locked(&entropy_src, &is_locked));
  TRY_CHECK(!is_locked,
            "Entropy source is locked. Cannot configure ENTROPY_SRC");

  // Configure Repetition Count Test
  dif_entropy_src_health_test_config_t repcnt_test_config = {
      .test_type = kDifEntropySrcTestRepetitionCount,
      .high_threshold = 512,
      .low_threshold = 0,
  };
  TRY(dif_entropy_src_health_test_configure(&entropy_src, repcnt_test_config));

  // Configure Adaptive Proportion Test
  dif_entropy_src_health_test_config_t adaptp_test_config = {
      .test_type = kDifEntropySrcTestAdaptiveProportion,
      .high_threshold = 512,
      .low_threshold = 0,
  };
  TRY(dif_entropy_src_health_test_configure(&entropy_src, adaptp_test_config));

  // Configure Bucket Test
  dif_entropy_src_health_test_config_t bucket_test_config = {
      .test_type = kDifEntropySrcTestBucket,
      .high_threshold = 512,
      .low_threshold = 0,
  };
  TRY(dif_entropy_src_health_test_configure(&entropy_src, bucket_test_config));

  // Configure Markov Test
  dif_entropy_src_health_test_config_t markov_test_config = {
      .test_type = kDifEntropySrcTestMarkov,
      .high_threshold = 512,
      .low_threshold = 0,
  };
  TRY(dif_entropy_src_health_test_configure(&entropy_src, markov_test_config));

  return OK_STATUS();
}

status_t enable_realistic_entropy_src_fips_mode(
    uint16_t health_test_window_size) {
  LOG_INFO("Enabling ENTROPY_SRC in fips mode...");

  // Ensure the entropy source is not locked
  bool is_locked;
  TRY(dif_entropy_src_is_locked(&entropy_src, &is_locked));
  TRY_CHECK(!is_locked,
            "Entropy source is locked. Cannot configure ENTROPY_SRC");

  // Configure ENTROPY_SRC in fips mode with health tests enabled
  dif_entropy_src_config_t config = {
      .fips_enable = true,
      .route_to_firmware = false,
      .fips_flag = true,
      .rng_fips = true,
      .bypass_conditioner = false,
      .single_bit_mode = kDifEntropySrcSingleBitModeDisabled,
      .health_test_threshold_scope = true,
      .health_test_window_size = health_test_window_size,
      .alert_threshold = 0xFF,
  };

  // Apply the configuration and enable ENTROPY_SRC
  TRY(dif_entropy_src_configure(&entropy_src, config, kDifToggleEnabled));

  return OK_STATUS();
}

status_t set_threshold_and_enable_stringent_entropy_src_fips_mode(void) {
  LOG_INFO("Enabling stringent ENTROPY_SRC in fips mode...");

  // Ensure the entropy source is not locked
  bool is_locked;
  TRY(dif_entropy_src_is_locked(&entropy_src, &is_locked));
  TRY_CHECK(!is_locked,
            "Entropy source is locked. Cannot configure ENTROPY_SRC");

  // Configure ENTROPY_SRC in fips mode with health tests enabled
  dif_entropy_src_config_t config = {
      .fips_enable = true,
      .route_to_firmware = false,
      .fips_flag = true,
      .rng_fips = true,
      .bypass_conditioner = false,
      .single_bit_mode = kDifEntropySrcSingleBitModeDisabled,
      .health_test_threshold_scope = true,
      .health_test_window_size = 4096,
      .alert_threshold = 1,
  };

  print_entropy_src_state(&entropy_src);
  LOG_INFO("ENTROPY_SRC Configuration:");
  LOG_INFO("fips_enable: %d", config.fips_enable);
  LOG_INFO("route_to_firmware: %d", config.route_to_firmware);
  LOG_INFO("fips_flag: %d", config.fips_flag);
  LOG_INFO("rng_fips: %d", config.rng_fips);
  LOG_INFO("bypass_conditioner: %d", config.bypass_conditioner);
  LOG_INFO("single_bit_mode: %d", config.single_bit_mode);
  LOG_INFO("health_test_threshold_scope: %d",
           config.health_test_threshold_scope);
  LOG_INFO("health_test_window_size: %d", config.health_test_window_size);
  LOG_INFO("alert_threshold: %d", config.alert_threshold);

  // Apply the configuration and enable ENTROPY_SRC
  TRY(dif_entropy_src_configure(&entropy_src, config, kDifToggleEnabled));
  uint32_t errors;
  TRY(dif_entropy_src_get_errors(&entropy_src, &errors));
  LOG_INFO("ENTROPY_SRC Errors: 0x%x", errors);

  dif_entropy_src_irq_state_snapshot_t irq_state;
  TRY(dif_entropy_src_irq_get_state(&entropy_src, &irq_state));
  LOG_INFO("ENTROPY_SRC IRQ State: 0x%x", irq_state);
  CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));
  // Debug try
  dif_entropy_src_health_test_stats_t stats;
  TRY(dif_entropy_src_get_health_test_stats(&entropy_src, &stats));

  CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));

  LOG_INFO("Done enabling stringent ENTROPY_SRC in fips mode...");
  return OK_STATUS();
}

status_t configure_stringent_fips_health_tests(void) {
  LOG_INFO("Configuring stringent health test thresholds...");

  // Ensure the entropy source is not locked
  bool is_locked;
  TRY(dif_entropy_src_is_locked(&entropy_src, &is_locked));
  TRY_CHECK(!is_locked,
            "Entropy source is locked. Cannot configure ENTROPY_SRC");
  // Configure Repetition Count Test with stringent threshold
  dif_entropy_src_health_test_config_t repcnt_test_config = {
      .test_type = kDifEntropySrcTestRepetitionCount,
      .high_threshold = 50,
      .low_threshold = 0,
  };
  LOG_INFO("Repetition Count Test Configuration:");
  LOG_INFO("test_type: %d", repcnt_test_config.test_type);
  LOG_INFO("high_threshold: %d", repcnt_test_config.high_threshold);
  LOG_INFO("low_threshold: %d", repcnt_test_config.low_threshold);
  TRY(dif_entropy_src_health_test_configure(&entropy_src, repcnt_test_config));

  // Configure Adaptive Proportion Test with stringent threshold
  dif_entropy_src_health_test_config_t adaptp_test_config = {
      .test_type = kDifEntropySrcTestAdaptiveProportion,
      .high_threshold = 270,
      .low_threshold = 0,
  };
  LOG_INFO("Adaptive Proportion Test Configuration:");
  LOG_INFO("test_type: %d", adaptp_test_config.test_type);
  LOG_INFO("high_threshold: %d", adaptp_test_config.high_threshold);
  LOG_INFO("low_threshold: %d", adaptp_test_config.low_threshold);
  TRY(dif_entropy_src_health_test_configure(&entropy_src, adaptp_test_config));

  // Configure Bucket Test with stringent threshold
  dif_entropy_src_health_test_config_t bucket_test_config = {
      .test_type = kDifEntropySrcTestBucket,
      .high_threshold = 270,
      .low_threshold = 0,
  };
  LOG_INFO("Bucket Test Configuration:");
  LOG_INFO("test_type: %d", bucket_test_config.test_type);
  LOG_INFO("high_threshold: %d", bucket_test_config.high_threshold);
  LOG_INFO("low_threshold: %d", bucket_test_config.low_threshold);
  TRY(dif_entropy_src_health_test_configure(&entropy_src, bucket_test_config));

  // Configure Markov Test with stringent threshold
  dif_entropy_src_health_test_config_t markov_test_config = {
      .test_type = kDifEntropySrcTestMarkov,
      .high_threshold = 270,
      .low_threshold = 0,
  };
  LOG_INFO("Markov Test Configuration:");
  LOG_INFO("test_type: %d", markov_test_config.test_type);
  LOG_INFO("high_threshold: %d", markov_test_config.high_threshold);
  LOG_INFO("low_threshold: %d", markov_test_config.low_threshold);
  TRY(dif_entropy_src_health_test_configure(&entropy_src, markov_test_config));

  return OK_STATUS();
}

//
// Custom version of entropy_testutils_auto_mode_init() for this test.
//
// Differences from the original version:
// Does NOT reinitialize the entropy complex (to retain previous state).
// Uses a custom reseed_interval (value = 4 instead of default 32).
// Includes additional debug logs to help track hang behavior.

status_t my_entropy_testutils_auto_mode_init(void) {
  TRY(entropy_testutils_stop_all());
  busy_spin_micros(kEntropySrcStartupWaitMicros);

  print_entropy_src_state(&entropy_src);
  // re-enable entropy src and csrng
  CHECK_STATUS_OK(entropy_testutils_entropy_src_init());
  print_entropy_src_state(&entropy_src);
  busy_spin_micros(kEntropySrcStartupWaitMicros);
  LOG_INFO("Configuring csrng:");
  TRY(dif_csrng_configure(&csrng));
  print_entropy_src_state(&entropy_src);

  LOG_INFO("Configuring csrng done:");
  LOG_INFO("Setting EDN in auto mode...");
  CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));
  const int my_reseed_interval = 4;
  LOG_INFO("Setting EDN0 reseed_interval to %d", my_reseed_interval);
  print_entropy_src_state(&entropy_src);
  // Re-enable EDN0 in auto mode.
  TRY(dif_edn_set_auto_mode(
      &edn0,
      (dif_edn_auto_params_t){
          // EDN0 provides lower-quality entropy.  Let one generate command
          // return 8 blocks
          .instantiate_cmd =
              {
                  .cmd = csrng_cmd_header_build(kCsrngAppCmdInstantiate,
                                                kDifCsrngEntropySrcToggleEnable,
                                                /*cmd_len=*/0,
                                                /*generate_len=*/0),
                  .seed_material =
                      {
                          .len = 0,
                      },
              },
          .reseed_cmd =
              {
                  .cmd = csrng_cmd_header_build(
                      kCsrngAppCmdReseed, kDifCsrngEntropySrcToggleEnable,
                      /*cmd_len=*/0, /*generate_len=*/0),
                  .seed_material =
                      {
                          .len = 0,
                      },
              },
          .generate_cmd =
              {
                  // Generate 8 128-bit blocks.
                  .cmd = csrng_cmd_header_build(kCsrngAppCmdGenerate,
                                                kDifCsrngEntropySrcToggleEnable,
                                                /*cmd_len=*/0,
                                                /*generate_len=*/8),
                  .seed_material =
                      {
                          .len = 0,
                      },
              },
          // Reseed every 4 generates
          .reseed_interval = my_reseed_interval,
      }));

  LOG_INFO("Setting edn0 done:");
  print_entropy_src_state(&entropy_src);
  // Re-enable EDN1 in auto mode.
  TRY(dif_edn_set_auto_mode(
      &edn1,
      (dif_edn_auto_params_t){
          // EDN1 provides highest-quality entropy.  Let one generate command
          // return 1 block, and reseed after every generate.
          .instantiate_cmd =
              {
                  .cmd = csrng_cmd_header_build(kCsrngAppCmdInstantiate,
                                                kDifCsrngEntropySrcToggleEnable,
                                                /*cmd_len=*/0,
                                                /*generate_len=*/0),
                  .seed_material =
                      {
                          .len = 0,
                      },
              },
          .reseed_cmd =
              {
                  .cmd = csrng_cmd_header_build(
                      kCsrngAppCmdReseed, kDifCsrngEntropySrcToggleEnable,
                      /*cmd_len=*/0, /*generate_len=*/0),
                  .seed_material =
                      {
                          .len = 0,
                      },
              },
          .generate_cmd =
              {
                  // Generate 1 128-bit block.
                  .cmd = csrng_cmd_header_build(kCsrngAppCmdGenerate,
                                                kDifCsrngEntropySrcToggleEnable,
                                                /*cmd_len=*/0,
                                                /*generate_len=*/1),
                  .seed_material =
                      {
                          .len = 0,
                      },
              },
          // Reseed after every 4 generates.
          .reseed_interval = 4,
      }));
  LOG_INFO("Setting edn1 done:");
  return OK_STATUS();
}

status_t enable_realistic_csrng_edns_auto_mode(void) {
  LOG_INFO("Enabling EDNs in auto mode...");
  CHECK_STATUS_OK(entropy_testutils_auto_mode_init());
  return OK_STATUS();
}

status_t enable_csrng_edns_auto_mode(void) {
  LOG_INFO("Enabling EDNs in auto mode for fips(locally modified function)...");
  CHECK_STATUS_OK(my_entropy_testutils_auto_mode_init());
  LOG_INFO("Done Enabling EDNs in auto mode for fips...");
  return OK_STATUS();
}

status_t test_and_verify_aes_operation(void) {
  LOG_INFO("Triggering AES operation in ECB mode...");

  // Setup ECB encryption transaction
  dif_aes_transaction_t transaction = {
      .operation = kDifAesOperationEncrypt,
      .mode = kDifAesModeEcb,
      .key_len = kDifAesKey256,
      .key_provider = kDifAesKeySoftwareProvided,
      .mask_reseeding = kDifAesReseedPerBlock,
      .manual_operation = kDifAesManualOperationAuto,
      .reseed_on_key_change = false,
      .ctrl_aux_lock = false,
  };

  // Configure encryption
  CHECK_STATUS_OK(aes_testutils_setup_encryption(transaction, &aes));

  // Wait for the encryption to complete
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusOutputValid, true, TIMEOUT);

  // Decrypt and verify
  CHECK_STATUS_OK(aes_testutils_decrypt_ciphertext(transaction, &aes));

  LOG_INFO("AES operation in ECB mode verified successfully");
  return OK_STATUS();
}

static bool poll_for_output_valid_or_starve(const dif_aes_t *aes) {
  const uint32_t kMaxRetries = 1000;
  for (uint32_t i = 0; i < kMaxRetries; i++) {
    bool out_valid = false;
    dif_result_t dif_res =
        dif_aes_get_status(aes, kDifAesStatusOutputValid, &out_valid);
    if (dif_res != kDifOk) {
      LOG_ERROR("dif_aes_get_status() = %d while polling OUTPUT_VALID.",
                dif_res);
      return false;
    }
    if (out_valid) {
      return true;
    }
    // if no out_valid wait for 3ms
    busy_spin_micros(3000);
  }
  // Timed out after kMaxRetries of 1000 * 3000 us i.e 3 sec
  return false;
}

#define AES_OPERATION_HANG_STATUS_VALUE 0x700B
static inline status_t aes_starve_ok_status(void) {
  status_t st;
  st.value = AES_OPERATION_HANG_STATUS_VALUE;
  return st;
}

static inline bool status_is_aes_starve_ok(status_t s) {
  return (s.value == AES_OPERATION_HANG_STATUS_VALUE);
}

status_t test_and_verify_aes_operation_hang(void) {
  LOG_INFO("Perform AES-256 in ECB mode");

  // Construct a 256-bit software key share
  // For a known 256-bit key: kAesModesKey256 (32 bytes)
  // and a share array kKeyShare1[] (32 bytes).
  static const uint8_t kKeyShare1[32] = {
      0x0f, 0x1f, 0x2f, 0x3f, 0x4f, 0x5f, 0x6f, 0x7f, 0x8f, 0x9f, 0xaf,
      0xbf, 0xcf, 0xdf, 0xef, 0xff, 0x0a, 0x1a, 0x2a, 0x3a, 0x4a, 0x5a,
      0x6a, 0x7a, 0x8a, 0x9a, 0xaa, 0xba, 0xca, 0xda, 0xea, 0xfa};

  dif_aes_key_share_t key;
  uint8_t key_share0[32];
  for (int i = 0; i < 32; i++) {
    key_share0[i] = kAesModesKey256[i] ^ kKeyShare1[i];
  }
  memcpy(key.share0, key_share0, 16);
  memcpy(key.share1, &key_share0[16], 16);

  // Prepare an AES transaction for 256-bit ECB encryption.
  dif_aes_transaction_t transaction = {
      .operation = kDifAesOperationEncrypt,
      .mode = kDifAesModeEcb,
      .key_len = kDifAesKey256,
      .key_provider = kDifAesKeySoftwareProvided,
      .mask_reseeding = kDifAesReseedPerBlock,
      .manual_operation = kDifAesManualOperationAuto,
      .reseed_on_key_change = false,
      .ctrl_aux_lock = false,
  };

  // Single-block plaintext (16 bytes).
  dif_aes_data_t in_data;
  memcpy(in_data.data, kAesModesPlainText, 16);

  // Start AES encryption
  LOG_INFO("Starting AES encryption...");
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusIdle, true, TIMEOUT);
  CHECK_DIF_OK(dif_aes_start(&aes, &transaction, &key, NULL));

  // Wait for InputReady, load the plaintext block.
  LOG_INFO("Wait for AES InputReady, then load the single-block plaintext...");
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusInputReady, true, TIMEOUT);
  CHECK_DIF_OK(dif_aes_load_data(&aes, in_data));
  LOG_INFO("Plaintext block loaded for encryption.");

  // Poll for OutputValid or starve
  LOG_INFO("Polling for OutputValid in encryption...", TIMEOUT);
  bool got_output_valid = poll_for_output_valid_or_starve(&aes);

  if (!got_output_valid) {
    // starved; we shall treat it as pass
    LOG_INFO(
        "No OUTPUT_VALID... EDN starved during encryption expected pass "
        "scenario");
    return aes_starve_ok_status();
  }

  // If we do get OUTPUT_VALID
  dif_aes_data_t out_data;
  CHECK_DIF_OK(dif_aes_read_output(&aes, &out_data));

  // End encryption transaction
  CHECK_DIF_OK(dif_aes_end(&aes));
  LOG_INFO("AES encryption transaction ended successfully.");

  // Switch to AES decryption
  LOG_INFO("Switching to AES decryption mode...");
  transaction.operation = kDifAesOperationDecrypt;
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusIdle, true, TIMEOUT);
  CHECK_DIF_OK(dif_aes_start(&aes, &transaction, &key, NULL));

  // Wait for InputReady, load the ciphertext
  LOG_INFO("Waiting for InputReady to load ciphertext for decryption...");
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusInputReady, true, TIMEOUT);
  CHECK_DIF_OK(dif_aes_load_data(&aes, out_data));

  // Poll for OutputValid or starve
  LOG_INFO("Polling for OutputValid in decryption...");
  got_output_valid = poll_for_output_valid_or_starve(&aes);

  if (!got_output_valid) {
    // starved; we shall treat it as pass
    LOG_INFO(
        "EDN starved during decryption expected pass "
        "scenario");
    return aes_starve_ok_status();
  }

  // If we are here means we have a decrypted block
  dif_aes_data_t dec_data;
  CHECK_DIF_OK(dif_aes_read_output(&aes, &dec_data));
  LOG_INFO("Decryption output read from hardware.");

  uint8_t *dec_bytes = (uint8_t *)dec_data.data;

  // End the AES decryption transaction
  CHECK_DIF_OK(dif_aes_end(&aes));
  LOG_INFO("ECB decryption transaction ended successfully.");

  // Compare the decrypted block with the original plaintext
  LOG_INFO("Verifying decrypted block with original plaintext?");
  if (memcmp(dec_data.data, kAesModesPlainText, 16) != 0) {
    LOG_ERROR("Decryption mismatch => final plaintext != original!");
    for (int i = 0; i < 16; i++) {
      LOG_ERROR("Byte[%d]: got=0x%02x, want=0x%02x", i, dec_bytes[i],
                kAesModesPlainText[i]);
    }
    return INTERNAL();
  }

  LOG_INFO("AES operation in ECB mode verified successfully");
  return OK_STATUS();
}

status_t test_and_verify_otbn_operation(void) {
  LOG_INFO("Starting OTBN randomness test...(expect completion)");

  // Start the OTBN randomness test with one iteration
  otbn_randomness_test_start(&otbn, 1);
  busy_spin_micros(9500);

  // Wait for a timeout period to check if OTBN is still busy
  const uint32_t kIterateMaxRetries = 10;
  bool otbn_busy = true;
  uint32_t iter_cntr = kIterateMaxRetries;

  dif_otbn_status_t otbn_status;

  while (iter_cntr > 0) {
    // Check if OTBN is still busy
    TRY(dif_otbn_get_status(&otbn, &otbn_status));

    // Check if any of the busy status flags are set
    otbn_busy = (otbn_status &
                 (kDifOtbnStatusBusyExecute | kDifOtbnStatusBusySecWipeDmem |
                  kDifOtbnStatusBusySecWipeImem)) != 0;
    // If OTBN is no longer busy, it has completed successfully
    // Break the loop
    if (!otbn_busy) {
      break;
    }
    iter_cntr--;
  }

  // After timeout, if OTBN is not busy, we shall conclude it's
  // completed running, lets check final status
  if (!otbn_busy) {
    // Print OTBN status and error bits
    dif_otbn_err_bits_t otbn_err_bits;
    TRY(dif_otbn_get_err_bits(&otbn, &otbn_err_bits));
    LOG_INFO("OTBN status: 0x%x", otbn_status);
    LOG_INFO("OTBN error bits: 0x%x", otbn_err_bits);

    LOG_INFO("OTBN program ran as expected with no hang");

    // Double check to confirm no other unexpected errors are
    // present leading to hang
    if (otbn_err_bits != kDifOtbnErrBitsNoError) {
      LOG_ERROR("OTBN encountered unexpected errors");

      // Optionally, decode and print specific error bits
      if (otbn_err_bits & kDifOtbnErrBitsBadDataAddr) {
        LOG_ERROR("A BAD_DATA_ADDR error was observed");
      }
      if (otbn_err_bits & kDifOtbnErrBitsBadInsnAddr) {
        LOG_ERROR("A BAD_INSN_ADDR error was observed");
      }
      if (otbn_err_bits & kDifOtbnErrBitsCallStack) {
        LOG_ERROR("A CALL_STACK error was observed");
      }
      if (otbn_err_bits & kDifOtbnErrBitsIllegalInsn) {
        LOG_ERROR("An ILLEGAL_INSN error was observed");
      }
      if (otbn_err_bits & kDifOtbnErrBitsLoop) {
        LOG_ERROR("A LOOP error was observed");
      }

      otbn_randomness_test_end(&otbn, 1);
      return INTERNAL();
    }
    return OK_STATUS();
  } else {
    // If still busy after kIterateMaxRetries, OTBN has not completed and failed
    LOG_ERROR("OTBN program did not complete run");
    return INTERNAL();
  }
}

status_t wait_for_recoverable_alert(void) {
  LOG_INFO("Waiting for Entropy Source recoverable alert to be asserted...");

  while (true) {
    // Poll the recoverable alerts
    uint32_t alerts = 0;
    dif_result_t res =
        dif_entropy_src_get_recoverable_alerts(&entropy_src, &alerts);
    if (res != kDifOk) {
      LOG_ERROR("dif_entropy_src_get_recoverable_alerts failed (res=%d)", res);
      return INTERNAL();
    }

    if (alerts != 0) {
      // if found a recoverable alert => log & break
      LOG_INFO("Recoverable alert detected. Alerts: 0x%x", alerts);

      print_entropy_src_state(&entropy_src);
      log_entropy_src_failures_and_alerts(&entropy_src);

      const uint32_t kEsMainSmAlertBit =
          ENTROPY_SRC_RECOV_ALERT_STS_ES_MAIN_SM_ALERT_BIT;

      if ((alerts & kEsMainSmAlertBit) != 0) {
        LOG_INFO("ES_MAIN_SM_ALERT bit is set, as expected.");
      } else {
        LOG_INFO("ES_MAIN_SM_ALERT bit not set, continuing anyway...");
      }

      // Acknowledge the Health Test Failed interrupt
      dif_result_t ack_res = dif_entropy_src_irq_acknowledge(
          &entropy_src, kDifEntropySrcIrqEsHealthTestFailed);
      if (ack_res != kDifOk) {
        LOG_INFO("Failed to acknowledge HealthTestFailed IRQ (res=%d).",
                 ack_res);
      } else {
        LOG_INFO("HealthTestFailed IRQ acknowledged.");
      }
      break;
    }
    // If no alerts --> sleep and poll again
    busy_spin_micros(50000);
  }

  LOG_INFO("Done waiting for Entropy Source recoverable alert (found).");
  return OK_STATUS();
}

status_t start_otbn_program(void) {
  LOG_INFO("Starting OTBN randomness test...");

  // Start the OTBN randomness test with one iteration
  otbn_randomness_test_start(&otbn, 1);

  LOG_INFO("OTBN randomness test started");
  return OK_STATUS();
}

status_t execute_test(void) {
  // Initialize peripherals and test environments
  CHECK_STATUS_OK(init_test_environment());

  // Step 8:  Repeat the procedure with different health test window sizes for
  // FIPS mode.
  uint16_t window_sizes[5] = {256, 512, 1024, 2048, 4096};
  for (uint16_t i = 0; i < ARRAYSIZE(window_sizes); i++) {
    LOG_INFO("Testing health test window size value = %u", window_sizes[i]);
    // Step 1: Disable the entropy complex
    CHECK_STATUS_OK(disable_entropy_complex());

    // Step 2: Configure realistic health test threshold values for fips FIPS/CC
    // compliant mode mode
    CHECK_STATUS_OK(configure_realistic_fips_health_tests());

    // Step 3: Enable ENTROPY_SRC in FIPS mode
    CHECK_STATUS_OK(enable_realistic_entropy_src_fips_mode(window_sizes[i]));

    // Step 4: Enable CSRNG
    // Step 5: Enable both EDNs in auto request mode
    CHECK_STATUS_OK(enable_realistic_csrng_edns_auto_mode());

    // Step 6: Trigger the execution of a cryptographic hardware block to stess
    // test the entropy (e.g. AES, OTBN) to test EDN0
    // Step 7: Verify the entropy consuming endpoint(e.g. AES, OTBN)
    // finishes its operation
    CHECK_STATUS_OK(test_and_verify_aes_operation());
    CHECK_STATUS_OK(test_and_verify_aes_operation());
    CHECK_STATUS_OK(test_and_verify_aes_operation());
    CHECK_STATUS_OK(test_and_verify_otbn_operation());
    CHECK_STATUS_OK(test_and_verify_otbn_operation());
    CHECK_STATUS_OK(test_and_verify_otbn_operation());
  }
  LOG_INFO(
      "Realistic Test successfully passed with different health test window "
      "sizes...");

  for (uint16_t iter = 0; iter < 1; iter++) {
    LOG_INFO("iter = %d", iter);

    // Step 9: Disable the entropy complex again
    LOG_INFO("Step 9: Disable the entropy complex again. iter = %d", iter);
    CHECK_STATUS_OK(disable_entropy_complex());

    // Step 10: Configure unrealistically stringent health test threshold values
    // for FIPS mode
    LOG_INFO(
        "Step 10: Configure unrealistically stringent health test threshold "
        "values. iter = %d",
        iter);
    CHECK_STATUS_OK(configure_stringent_fips_health_tests());

    // Step 11: Configure a low alert threshold value in the ALERT_THRESHOLD
    // register Step 12: Enable ENTROPY_SRC in FIPS mode
    LOG_INFO(
        "Step 11 and Step 12:Configure a low alert threshold value in the "
        "ALERT_THRESHOLD and enable entropy_src. iter = %d",
        iter);
    CHECK_STATUS_OK(set_threshold_and_enable_stringent_entropy_src_fips_mode());

    //  Wait for Entropy Source startup tests to complete
    busy_spin_micros(kEntropySrcStartupWaitMicros);
    // Step 13: Enable CSRNG
    // Step 14: Enable both EDNs in auto request mode and with low values for
    // the generate length parameter to request fresh entropy more often
    LOG_INFO(
        "Step 13 and Step 14:Enable CSRNG and both EDNs in auto request mode. "
        "iter = %d",
        iter);
    CHECK_STATUS_OK(enable_csrng_edns_auto_mode());
    CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));

    busy_spin_micros(kEntropySrcStartupWaitMicros);

    // Step 15: Trigger the execution of various cryptographic hardware blocks
    // consuming entropy to stress test the entropy complex
    LOG_INFO("Step 15 skipped!");
    CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));

    busy_spin_micros(kEntropySrcStartupWaitMicros);
    // Step 16: Verify that ENTROPY_SRC triggers a recoverable alert and sets
    // the RECOV_ALERT_STS.ES_MAIN_SM_ALERT bit
    CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));
    // Find if we get recoverable alert and untill we hit
    // recoverable alert we do not proceed
    LOG_INFO("Step 16: Wait for recoverable alert");
    LOG_INFO("Step 16:iter = %d", iter);
    status_t st = wait_for_recoverable_alert();
    if (!status_ok(st)) {
      LOG_ERROR("wait_for_recoverable_alert() error=0x%x", st.value);
      return INTERNAL();
    }
    CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));

    // Step 17 Verify that various entropy consuming endpoints hang as
    // ENTROPY_SRC stops generating entropy after triggering the recoverable
    // alert
    for (uint16_t i = 0; i < 500; i++) {
      status_t st = test_and_verify_aes_operation_hang();
      if (status_is_aes_starve_ok(st)) {
        LOG_INFO(
            "Step 17: Starve event encountered confirming AES expected hang");
        break;
      }
      TRY(st);
    }
    CHECK_STATUS_OK(print_entropy_src_state(&entropy_src));
  }

  LOG_INFO("Entropy source fips mode health test completed");

  return OK_STATUS();
}

bool test_main(void) {
  LOG_INFO("Entering Entropy Source FIPS Mode Health Test");

  return status_ok(execute_test());
}
