// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/manuf/lib/individualize_sw_cfg.h"

#include "sw/device/lib/base/macros.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/lib/crypto/include/datatypes.h"
#include "sw/device/lib/dif/dif_flash_ctrl.h"
#include "sw/device/lib/dif/dif_otp_ctrl.h"
#include "sw/device/lib/testing/flash_ctrl_testutils.h"
#include "sw/device/lib/testing/otp_ctrl_testutils.h"
#include "sw/device/silicon_creator/manuf/lib/flash_info_fields.h"
#include "sw/device/silicon_creator/manuf/lib/otp_img_types.h"
#include "sw/device/silicon_creator/manuf/lib/util.h"

#include "flash_ctrl_regs.h"  // Generated.
#include "hw/top_earlgrey/sw/autogen/top_earlgrey.h"
#include "otp_ctrl_regs.h"  // Generated.

enum {
  kValidAstCfgOtpAddrLow = OTP_CTRL_PARAM_CREATOR_SW_CFG_AST_CFG_OFFSET,
  kInvalidAstCfgOtpAddrHigh =
      kValidAstCfgOtpAddrLow + OTP_CTRL_PARAM_CREATOR_SW_CFG_AST_CFG_SIZE,
  kSha256DigestWords = 256 / 32,
};

static uint32_t
    flash_info_page_buf[FLASH_CTRL_PARAM_BYTES_PER_PAGE / sizeof(uint32_t)];

/**
 * Writes OTP values to target OTP `partition`.
 *
 * The `kv` array is preferrably generated using the build infrastructure. See
 * individualize_preop.c and its build target for an example.
 *
 * @param otp OTP Controller instance.
 * @param partition Target OTP partition.
 * @param kv OTP Array of OTP key values. See `otp_kv_t` documentation for more
 * details.
 * @param len Length of the `kv` array.
 * @return OK_STATUS if the OTP values were written into the target partition.
 */
OT_WARN_UNUSED_RESULT
static status_t otp_img_write(const dif_otp_ctrl_t *otp,
                              dif_otp_ctrl_partition_t partition,
                              const otp_kv_t *kv, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    // We purposely skip the provisioning of the flash data region default
    // configuration as it must be enabled only after the OTP SECRET1
    // partition has been provisioned. Since OTP SECRET1 provisioning requires
    // the HW_CFG0 partition to be provisioned to use the CSRNG SW interface,
    // there is a delicate order of operations in which this field is
    // provisioned. Therefore we require explicit provisioning of this field
    // immediately before the transport image is loaded, after all other
    // provisioning is complete.
    //
    // We skip the provisioning of the creator manufacturing status as it must
    // be provisioned only at the end of the personalization flow. The
    // personalization firmware is bound with the INITIAL (empty) manufacturing
    // state, so once the manufacturing state is provisioned, the
    // personalization firmware can't be re-entrant.
    //
    // We skip the provisioning of the immutable ROM_EXT enablement
    // configuration as it must be provisioned only at the end of the
    // personalization flow. The personalization firmware doesn't include an
    // immutable ROM_EXT section. Enabling this feature with personalization
    // firmware would result in ROM self-shutdown due to an invalid immutable
    // ROM extension hash.
    //
    // We also skip the provisioning of the ROM bootstrap disablement
    // configuration. This should only be disabled after all bootstrap
    // operations in the personalization flow have been completed.
    //
    // Additionally, we skip the provisioning of the AST configuration data, as
    // this should already be written to a flash info page. We will pull the
    // data directly from there.
    if (kv[i].offset ==
            OTP_CTRL_PARAM_CREATOR_SW_CFG_FLASH_DATA_DEFAULT_CFG_OFFSET ||
        kv[i].offset == OTP_CTRL_PARAM_CREATOR_SW_CFG_MANUF_STATE_OFFSET ||
        kv[i].offset ==
            OTP_CTRL_PARAM_CREATOR_SW_CFG_IMMUTABLE_ROM_EXT_EN_OFFSET ||
        kv[i].offset == OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_BOOTSTRAP_DIS_OFFSET ||
        (kv[i].offset >= kValidAstCfgOtpAddrLow &&
         kv[i].offset < kInvalidAstCfgOtpAddrHigh)) {
      continue;
    }
    uint32_t offset;
    TRY(dif_otp_ctrl_relative_address(partition, kv[i].offset, &offset));
    switch (kv[i].type) {
      case kOptValTypeUint32Buff:
        TRY(otp_ctrl_testutils_dai_write32(otp, partition, offset,
                                           kv[i].value32, kv[i].num_values));
        break;
      case kOptValTypeUint64Buff:
        TRY(otp_ctrl_testutils_dai_write64(otp, partition, offset,
                                           kv[i].value64, kv[i].num_values));
        break;
      case kOptValTypeUint64Rnd:
        return UNIMPLEMENTED();
      default:
        return INTERNAL();
    }
  }
  return OK_STATUS();
}

/**
 * Overwrites the target field with its expected final value in a buffer
 * representing the provided partition.
 *
 * @param partition Target OTP partition.
 * @param field_offset An offest of the target OTP field.
 * @param buffer A buffer containing the entire target OTP partition.
 * @return OK_STATUS if the expected OTP values are successfully written to the
 * `buffer`.
 */
static status_t otp_img_expected_value_read(dif_otp_ctrl_partition_t partition,
                                            uint32_t field_offset,
                                            uint8_t *buffer) {
  uint32_t relative_addr;
  TRY(dif_otp_ctrl_relative_address(partition, field_offset, &relative_addr));
  switch (field_offset) {
    case OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_BOOTSTRAP_DIS_OFFSET:
      memcpy(buffer + relative_addr, &kOwnerSwCfgRomBootstrapDisValue,
             sizeof(uint32_t));
      break;
    case OTP_CTRL_PARAM_CREATOR_SW_CFG_MANUF_STATE_OFFSET:
      memcpy(buffer + relative_addr, &kCreatorSwCfgManufStateValue,
             sizeof(uint32_t));
      break;
    case OTP_CTRL_PARAM_CREATOR_SW_CFG_IMMUTABLE_ROM_EXT_EN_OFFSET:
      memcpy(buffer + relative_addr, &kCreatorSwCfgImmutableRomExtEnValue,
             sizeof(uint32_t));
      break;
    default:
      return INTERNAL();
  }
  return OK_STATUS();
}

/**
 * Computes a SHA256 digest of an OTP partition and uses the least significant
 * 64-bits of the digest to additionally lock the partition.
 *
 * Note: only {Creator,Owner}SwCfg partitions and the VendorTest partition may
 * be locked in this manner. All other partitions are locked via hardware.
 *
 * @param otp OTP Controller instance.
 * @param partition Target OTP partition.
 * @return OK_STATUS if the target partition was locked.
 */
OT_WARN_UNUSED_RESULT
static status_t lock_otp_partition(const dif_otp_ctrl_t *otp_ctrl,
                                   dif_otp_ctrl_partition_t partition) {
  // Compute SHA256 of the OTP partition.
  uint32_t digest[kSha256DigestWords];
  otcrypto_word32_buf_t otp_partition_digest = {
      .len = ARRAYSIZE(digest),
      .data = digest,
  };
  TRY(manuf_util_hash_otp_partition(otp_ctrl, partition, otp_partition_digest));

  // Get the least significant 64 bits of the digest. We will use this as the
  // digest to lock the OTP partition. The complete digest will be used in the
  // attestation key / certificate generation. Note: cryptolib generates the
  // digest in big-endian format so we must rearrange the bytes.
  uint64_t partition_digest_lowest_64bits = __builtin_bswap32(digest[6]);
  partition_digest_lowest_64bits =
      (partition_digest_lowest_64bits << 32) | __builtin_bswap32(digest[7]);

  TRY(otp_ctrl_testutils_lock_partition(
      otp_ctrl, partition, /*digest=*/partition_digest_lowest_64bits));

  return OK_STATUS();
}

static status_t manuf_individualize_device_ast_cfg(
    const dif_otp_ctrl_t *otp_ctrl, dif_flash_ctrl_state_t *flash_state) {
  // Clear flash info page buffer.
  memset(flash_info_page_buf, UINT8_MAX, FLASH_CTRL_PARAM_BYTES_PER_PAGE);

  // Copy all of flash info page 0 into RAM. This contains the AST configuration
  // data, which we will extract and then delete.
  uint32_t page_byte_address = 0;
  TRY(flash_ctrl_testutils_info_region_setup_properties(
      flash_state, kFlashInfoFieldAstCalibrationData.page,
      kFlashInfoFieldAstCalibrationData.bank,
      kFlashInfoFieldAstCalibrationData.partition,
      (dif_flash_ctrl_region_properties_t){
          .ecc_en = kMultiBitBool4True,
          .high_endurance_en = kMultiBitBool4False,
          .erase_en = kMultiBitBool4True,
          .prog_en = kMultiBitBool4True,
          .rd_en = kMultiBitBool4True,
          .scramble_en = kMultiBitBool4False},
      &page_byte_address));
  TRY(flash_ctrl_testutils_read(
      flash_state, page_byte_address,
      kFlashInfoFieldAstCalibrationData.partition, flash_info_page_buf,
      kDifFlashCtrlPartitionTypeInfo,
      FLASH_CTRL_PARAM_BYTES_PER_PAGE / sizeof(uint32_t),
      /*delay=*/0));

  // Write AST configuration data to OTP.
  size_t ast_cfg_offset =
      kFlashInfoFieldAstCalibrationData.byte_offset / sizeof(uint32_t);
  for (size_t i = 0; i < kFlashInfoAstCalibrationDataSizeIn32BitWords; ++i) {
    uint32_t addr =
        OTP_CTRL_PARAM_CREATOR_SW_CFG_AST_CFG_OFFSET + i * sizeof(uint32_t);
    uint32_t data = flash_info_page_buf[ast_cfg_offset + i];
    uint32_t relative_addr;
    // Check the range is valid.
    if (addr < kValidAstCfgOtpAddrLow || addr >= kInvalidAstCfgOtpAddrHigh) {
      return OUT_OF_RANGE();
    }
    TRY(dif_otp_ctrl_relative_address(kDifOtpCtrlPartitionCreatorSwCfg, addr,
                                      &relative_addr));
    TRY(otp_ctrl_testutils_dai_write32(otp_ctrl,
                                       kDifOtpCtrlPartitionCreatorSwCfg,
                                       relative_addr, &data, /*len=*/1));
    flash_info_page_buf[ast_cfg_offset + i] =
        UINT32_MAX;  // Erase AST config data after use.
  }

  // Erase AST data from flash by erasing the entire page and rewriting the
  // modified buffered contents back to the page.
  TRY(flash_ctrl_testutils_erase_page(
      flash_state, page_byte_address,
      kFlashInfoFieldAstCalibrationData.partition,
      kDifFlashCtrlPartitionTypeInfo));
  TRY(flash_ctrl_testutils_write(
      flash_state, page_byte_address,
      kFlashInfoFieldAstCalibrationData.partition, flash_info_page_buf,
      kDifFlashCtrlPartitionTypeInfo,
      FLASH_CTRL_PARAM_BYTES_PER_PAGE / sizeof(uint32_t)));

  return OK_STATUS();
}

status_t manuf_individualize_device_creator_sw_cfg(
    const dif_otp_ctrl_t *otp_ctrl, dif_flash_ctrl_state_t *flash_state) {
  TRY(otp_img_write(otp_ctrl, kDifOtpCtrlPartitionCreatorSwCfg,
                    kOtpKvCreatorSwCfg, kOtpKvCreatorSwCfgSize));
  TRY(manuf_individualize_device_ast_cfg(otp_ctrl, flash_state));
  return OK_STATUS();
}

status_t manuf_individualize_device_field_cfg(const dif_otp_ctrl_t *otp_ctrl,
                                              uint32_t field_offset) {
  uint32_t relative_addr;
  const uint32_t *field_value_addr;
  dif_otp_ctrl_partition_t partition;
  switch (field_offset) {
    case OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_BOOTSTRAP_DIS_OFFSET:
      field_value_addr = &kOwnerSwCfgRomBootstrapDisValue;
      partition = kDifOtpCtrlPartitionOwnerSwCfg;
      break;
    case OTP_CTRL_PARAM_CREATOR_SW_CFG_FLASH_DATA_DEFAULT_CFG_OFFSET:
      field_value_addr = &kCreatorSwCfgFlashDataDefaultCfgValue;
      partition = kDifOtpCtrlPartitionCreatorSwCfg;
      break;
    case OTP_CTRL_PARAM_CREATOR_SW_CFG_IMMUTABLE_ROM_EXT_EN_OFFSET:
      field_value_addr = &kCreatorSwCfgImmutableRomExtEnValue;
      partition = kDifOtpCtrlPartitionCreatorSwCfg;
      break;
    case OTP_CTRL_PARAM_CREATOR_SW_CFG_MANUF_STATE_OFFSET:
      field_value_addr = &kCreatorSwCfgManufStateValue;
      partition = kDifOtpCtrlPartitionCreatorSwCfg;
      break;
    default:
      return INTERNAL();
  }

  TRY(dif_otp_ctrl_relative_address(partition, field_offset, &relative_addr));
  TRY(otp_ctrl_testutils_dai_write32(otp_ctrl, partition, relative_addr,
                                     field_value_addr, /*len=*/1));

  return OK_STATUS();
}

status_t manuf_individualize_device_flash_data_default_cfg_check(
    const dif_otp_ctrl_t *otp_ctrl) {
  uint32_t offset;
  TRY(dif_otp_ctrl_relative_address(
      kDifOtpCtrlPartitionCreatorSwCfg,
      OTP_CTRL_PARAM_CREATOR_SW_CFG_FLASH_DATA_DEFAULT_CFG_OFFSET, &offset));
  uint32_t val = 0;
  TRY(otp_ctrl_testutils_dai_read32(otp_ctrl, kDifOtpCtrlPartitionCreatorSwCfg,
                                    offset, &val));
  bool is_provisioned = (val == kCreatorSwCfgFlashDataDefaultCfgValue);
  return is_provisioned ? OK_STATUS() : INTERNAL();
}

status_t manuf_individualize_device_creator_sw_cfg_lock(
    const dif_otp_ctrl_t *otp_ctrl) {
  TRY(lock_otp_partition(otp_ctrl, kDifOtpCtrlPartitionCreatorSwCfg));
  return OK_STATUS();
}

status_t manuf_individualize_device_creator_sw_cfg_check(
    const dif_otp_ctrl_t *otp_ctrl) {
  bool is_locked;
  TRY(dif_otp_ctrl_is_digest_computed(
      otp_ctrl, kDifOtpCtrlPartitionCreatorSwCfg, &is_locked));
  return is_locked ? OK_STATUS() : INTERNAL();
}

status_t manuf_individualize_device_owner_sw_cfg(
    const dif_otp_ctrl_t *otp_ctrl) {
  TRY(otp_img_write(otp_ctrl, kDifOtpCtrlPartitionOwnerSwCfg, kOtpKvOwnerSwCfg,
                    kOtpKvOwnerSwCfgSize));
  return OK_STATUS();
}

status_t manuf_individualize_device_partition_expected_read(
    dif_otp_ctrl_partition_t partition, uint8_t *buffer) {
  switch (partition) {
    case kDifOtpCtrlPartitionOwnerSwCfg:
      TRY(otp_img_expected_value_read(
          partition, OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_BOOTSTRAP_DIS_OFFSET,
          buffer));
      break;
    case kDifOtpCtrlPartitionCreatorSwCfg:
      TRY(otp_img_expected_value_read(
          partition, OTP_CTRL_PARAM_CREATOR_SW_CFG_MANUF_STATE_OFFSET, buffer));
      TRY(otp_img_expected_value_read(
          partition, OTP_CTRL_PARAM_CREATOR_SW_CFG_IMMUTABLE_ROM_EXT_EN_OFFSET,
          buffer));
      break;
    default:
      return INTERNAL();
  }

  return OK_STATUS();
}

status_t manuf_individualize_device_owner_sw_cfg_lock(
    const dif_otp_ctrl_t *otp_ctrl) {
  TRY(lock_otp_partition(otp_ctrl, kDifOtpCtrlPartitionOwnerSwCfg));
  return OK_STATUS();
}

status_t manuf_individualize_device_owner_sw_cfg_check(
    const dif_otp_ctrl_t *otp_ctrl) {
  bool is_locked;
  TRY(dif_otp_ctrl_is_digest_computed(otp_ctrl, kDifOtpCtrlPartitionOwnerSwCfg,
                                      &is_locked));
  return is_locked ? OK_STATUS() : INTERNAL();
}

status_t manuf_individualize_device_rot_creator_auth_codesign(
    const dif_otp_ctrl_t *otp_ctrl) {
  TRY(otp_img_write(otp_ctrl, kDifOtpCtrlPartitionRotCreatorAuthCodesign,
                    kOtpKvRotCreatorAuthCodesign,
                    kOtpKvRotCreatorAuthCodesignSize));
  TRY(lock_otp_partition(otp_ctrl, kDifOtpCtrlPartitionRotCreatorAuthCodesign));
  return OK_STATUS();
}

status_t manuf_individualize_device_rot_creator_auth_state(
    const dif_otp_ctrl_t *otp_ctrl) {
  TRY(otp_img_write(otp_ctrl, kDifOtpCtrlPartitionRotCreatorAuthState,
                    kOtpKvRotCreatorAuthState, kOtpKvRotCreatorAuthStateSize));
  TRY(lock_otp_partition(otp_ctrl, kDifOtpCtrlPartitionRotCreatorAuthState));
  return OK_STATUS();
}

status_t manuf_individualize_device_rot_creator_auth_codesign_check(
    const dif_otp_ctrl_t *otp_ctrl) {
  bool is_locked;
  TRY(dif_otp_ctrl_is_digest_computed(
      otp_ctrl, kDifOtpCtrlPartitionRotCreatorAuthCodesign, &is_locked));
  return is_locked ? OK_STATUS() : INTERNAL();
}

status_t manuf_individualize_device_rot_creator_auth_state_check(
    const dif_otp_ctrl_t *otp_ctrl) {
  bool is_locked;
  TRY(dif_otp_ctrl_is_digest_computed(
      otp_ctrl, kDifOtpCtrlPartitionRotCreatorAuthState, &is_locked));
  return is_locked ? OK_STATUS() : INTERNAL();
}
