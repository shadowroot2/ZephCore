/*
 * ZephCore LR1110 Firmware Updater
 * SPDX-License-Identifier: MIT
 *
 * Standalone tool that updates the LR1110 radio firmware to v0x0402
 * (Semtech H1_2026 security release: CVE-2025-14857/14858/14859).
 * Flash this UF2 first, let it update the radio, then flash ZephCore main firmware.
 *
 * Firmware image source: https://github.com/Lora-net/radio_firmware_images
 * Bootloader protocol source: https://github.com/Lora-net/lr1110_driver
 * Loader flow reference: https://github.com/Lora-net/SWTL001 (AN1200.57)
 * License: Clear BSD (Semtech Corporation 2021-2026)
 *
 * Firmware 0x0402 only boots on chip bootloader 0x1001. Chips made before
 * the H1_2026 release run legacy bootloader 0x6500, so the update is
 * TWO-STAGE (matching Semtech's SWTL001 reference):
 *
 *   Stage A — bootloader update (only when the chip reports BL 0x6500):
 *     1. Force bootloader mode (reset with BUSY held LOW)
 *     2. Erase flash, WriteFlashEncrypted the lr1110_loader_6500 image
 *     3. Reboot — the chip now RUNS the loader firmware
 *     4. Loader command 0x8100: rewrite the on-chip bootloader
 *     5. Loader command 0x8101: verification report (6 checks)
 *     6. Loader command 0x8102: reboot; chip lands back in bootloader
 *     7. Verify GetVersion now reports BL 0x1001
 *
 *   Stage B — transceiver firmware:
 *     8. Erase flash, WriteFlashEncrypted the 0x0402 image (61320 words)
 *     9. Reboot into firmware, verify version == 0x0402
 *    10. Reboot MCU into UF2 DFU mode (nRF52) / idle for reflash (ESP32)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>

#include "lr11xx_hal_updater.h"
#include "lr1110_bootloader.h"
#include "lr1110_bl_updater.h"
#include "lr1110_transceiver_0402.h"

#if defined(CONFIG_SOC_SERIES_NRF52)
#include <hal/nrf_power.h>
#endif

/* Adafruit UF2 bootloader magic — enter mass storage DFU mode */
#define BOOTLOADER_DFU_UF2_MAGIC 0x57

/* Target firmware version */
#define TARGET_FW_VERSION LR11XX_FIRMWARE_VERSION  /* 0x0402 */

/* LR1110 type field values */
#define LR1110_TYPE_TRANSCEIVER  0x01
#define LR1110_TYPE_BOOTLOADER   0xDF
/* Reported while the lr1110_loader_6500 image is RUNNING. Not in Semtech's
 * headers — SWTL001 never checks TYPE after booting the loader, it relies on
 * fw==0x6500, which the ROM bootloader ALSO reports when the loader image
 * failed to boot and the chip fell back. Field-observed (M9, 2026-07-19):
 * a chip verifiably running the loader answers TYPE=0xDE, FW=0x6500. TYPE
 * is therefore the only reliable loader-vs-fallback discriminator. */
#define LR1110_TYPE_BL_UPDATER   0xDE

/* ── LED feedback (optional) ─────────────────────────────────── */

#if DT_NODE_EXISTS(DT_ALIAS(led0))
#define HAS_LED 1
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static void led_init(void)  { gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE); }
static void led_on(void)    { gpio_pin_set_dt(&led, 1); }
static void led_off(void)   { gpio_pin_set_dt(&led, 0); }
static void led_toggle(void) { gpio_pin_toggle_dt(&led); }
#else
#define HAS_LED 0
static void led_init(void)  {}
static void led_on(void)    {}
static void led_off(void)   {}
static void led_toggle(void) {}
#endif

/* ── Helpers ──────────────────────────────────────────────────── */

static void updater_done(void)
{
#if defined(CONFIG_SOC_SERIES_NRF52)
	printk("\nRebooting into UF2 DFU mode...\n");
	printk("You can now drag-drop ZephCore firmware UF2.\n");
	k_msleep(500);

	nrf_power_gpregret_set(NRF_POWER, 0, BOOTLOADER_DFU_UF2_MAGIC);
	sys_reboot(SYS_REBOOT_COLD);
#else
	/* ESP32: no persistent DFU mode to reboot into, and a plain reboot
	 * would just re-run the updater. Idle here — west flash / esptool
	 * resets the chip itself when writing the main firmware. */
	printk("\nFlash ZephCore main firmware over USB now:\n");
	printk("  west flash --esp-device <COMx>\n");
	while (1) {
		led_toggle();
		k_msleep(1000);
	}
#endif
}

static void fatal_error(const char *msg)
{
	printk("\n!!! FATAL: %s\n", msg);
	printk("Please power cycle the device and try again.\n");
	led_off();

	/* Blink LED rapidly to indicate error */
	while (1) {
		led_toggle();
		k_msleep(200);
	}
}

/* Erase the chip flash, then write `total` words in 64-word chunks with
 * progress output. Used for both the loader and the transceiver image. */
static void erase_and_flash_image(void *ctx, const char *what,
				  const uint32_t *image, uint32_t total)
{
	lr1110_status_t rc;

	printk("  Erasing LR1110 flash (~2.5 seconds)...\n");
	led_toggle();
	rc = lr1110_bootloader_erase_flash(ctx);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("Flash erase failed");
	}

	printk("  Writing %s (%u words = %u KB)...\n",
	       what, total, (total * 4) / 1024);

	const uint32_t chunk_size = 64; /* 64 uint32_t words per write */
	uint32_t num_chunks = (total + chunk_size - 1) / chunk_size;
	uint32_t progress_step = num_chunks / 10; /* Print every 10% */
	if (progress_step == 0) progress_step = 1;

	uint32_t remaining = total;
	uint32_t offset = 0;
	uint32_t chunk_idx = 0;

	while (remaining > 0) {
		uint8_t this_chunk = (remaining > chunk_size)
			? (uint8_t)chunk_size : (uint8_t)remaining;

		rc = lr1110_bootloader_write_flash_encrypted(
			ctx, offset,
			&image[chunk_idx * chunk_size],
			this_chunk);

		if (rc != LR1110_STATUS_OK) {
			printk("\n");
			printk("ERROR: Write failed at offset 0x%08X (chunk %u/%u)\n",
			       offset, chunk_idx + 1, num_chunks);
			fatal_error("Firmware write failed");
		}

		if ((chunk_idx % progress_step) == 0) {
			uint32_t pct = (chunk_idx * 100) / num_chunks;
			printk("  %3u%%  (%u / %u words)\n",
			       pct, total - remaining + this_chunk, total);
			led_toggle();
		}

		offset += this_chunk * sizeof(uint32_t);
		remaining -= this_chunk;
		chunk_idx++;
	}

	printk("  100%%  (%u / %u words)\n", total, total);
	printk("  %s write complete!\n", what);
}

/* The loader is RUNNING (TYPE 0xDE) — command the bootloader rewrite,
 * verify it, then reboot and confirm the chip reports BL 0x1001. Shared by
 * run_bootloader_update() and the resume path in main() (chip found already
 * running a loader at startup, e.g. a previous attempt that died between
 * flashing the loader and the rewrite). */
static void loader_rewrite_and_verify(void *ctx)
{
	lr1110_bootloader_version_t version = { 0 };
	lr1110_bl_updater_report_t report = { 0 };
	lr1110_status_t rc;

	/* Ask the loader to rewrite the bootloader — BUSY is held for the
	 * duration; the HAL grants opcode 0x8100 the long timeout. */
	printk("  Rewriting chip bootloader (this takes a few seconds)...\n");
	rc = lr1110_bl_updater_update_bootloader(ctx);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("Bootloader rewrite command failed");
	}

	/* SWTL001 issues a GetStatus here (the loader answers the classic
	 * bootloader wire format) as its "wait for update termination" sync
	 * point. The read also returns the 0x8100 command status — the only
	 * way to tell a failed rewrite (FAIL) from a verify report that was
	 * corrupted on a marginal bus. Diagnostic only: like SWTL001, the
	 * verify below remains the gate. */
	{
		lr1110_bootloader_stat1_t stat1 = { 0 };
		lr1110_bootloader_stat2_t stat2 = { 0 };
		lr1110_bootloader_irq_mask_t irq = 0;

		rc = lr1110_bootloader_get_status(ctx, &stat1, &stat2, &irq);
		if (rc != LR1110_STATUS_OK) {
			fatal_error("Loader GetStatus after rewrite failed");
		}
		printk("  Loader status: cmd=%u (%s) mode=%u flash=%d reset=%u\n",
		       stat1.command_status,
		       stat1.command_status == LR1110_BOOTLOADER_CMD_STATUS_OK   ? "OK" :
		       stat1.command_status == LR1110_BOOTLOADER_CMD_STATUS_DATA ? "DATA" :
		       stat1.command_status == LR1110_BOOTLOADER_CMD_STATUS_PERR ? "PERR" : "FAIL",
		       stat2.chip_mode, stat2.is_running_from_flash, stat2.reset_status);
	}

	/* Verification report — all six checks must pass */
	rc = lr1110_bl_updater_verify_bootloader(ctx, &report);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("VerifyBootloader command failed");
	}
	printk("  Verify: sig=%d ver=%d usecase=%d major=%d minor=%d rollback=%d (v%u.%u usecase %u)\n",
	       report.signature_verified, report.version_verified,
	       report.use_case_verified, report.version_major_verified,
	       report.version_minor_verified, report.anti_rollback_verified,
	       report.version_major, report.version_minor, report.use_case);
	if (!lr1110_bl_updater_report_ok(&report)) {
		fatal_error("New bootloader failed verification");
	}

	/* Reboot out of the loader (loader opcode 0x8102). The new bootloader
	 * refuses to execute the loader image left in flash, so the chip lands
	 * in bootloader mode — force a clean re-entry and confirm 0x1001. */
	printk("  Rebooting with the new bootloader...\n");
	lr1110_bl_updater_reboot(ctx, false);
	k_msleep(500);

	if (lr1110_updater_reset_to_bootloader() != 0) {
		fatal_error("Failed to re-enter bootloader after update");
	}
	rc = lr1110_bootloader_get_version(ctx, &version);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("GetVersion after bootloader update failed");
	}
	printk("  Chip bootloader now: 0x%04X\n", version.fw);
	if (version.fw != LR1110_BL_VERSION_UPDATED) {
		fatal_error("Bootloader version mismatch after update");
	}

	printk("--- Stage A complete: bootloader 0x%04X ---\n\n",
	       LR1110_BL_VERSION_UPDATED);
}

/* Stage A — flash + run the loader so it rewrites the chip bootloader
 * 0x6500 -> 0x1001. Chip must currently be in bootloader mode. On return
 * the chip is back in bootloader mode running BL 0x1001. */
static void run_bootloader_update(void *ctx)
{
	lr1110_bootloader_version_t version = { 0 };
	lr1110_status_t rc;

	printk("\n--- Stage A: chip bootloader update 0x%04X -> 0x%04X ---\n",
	       LR1110_BL_VERSION_LEGACY, LR1110_BL_VERSION_UPDATED);
	printk("(required once per chip before firmware 0x0402 can run)\n\n");

	/* Flash the loader image */
	erase_and_flash_image(ctx, "bootloader-updater (loader)",
			      lr1110_bl_loader_image(),
			      lr1110_bl_loader_image_size());

	/* Boot the loader */
	printk("  Rebooting into the loader...\n");
	lr1110_bootloader_reboot(ctx, false);
	k_msleep(500);

	/* The loader answers the standard GetVersion frame with FW 0x6500 —
	 * but so does the ROM bootloader after a fallback, so TYPE (0xDE
	 * loader vs 0xDF ROM bootloader) is the real started-check. */
	rc = lr1110_bootloader_get_version(ctx, &version);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("GetVersion after loader boot failed");
	}
	printk("  Loader GetVersion: TYPE=0x%02X FW=0x%04X\n", version.type, version.fw);
	if (version.type == LR1110_TYPE_BOOTLOADER) {
		fatal_error("Loader did not start — chip fell back to the ROM "
			    "bootloader, so the loader flash write did not "
			    "stick. Check the radio power rail, then retry");
	}
	if (version.type != LR1110_TYPE_BL_UPDATER ||
	    version.fw != lr1110_bl_loader_version()) {
		fatal_error("Loader did not start (unexpected version)");
	}

	loader_rewrite_and_verify(ctx);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void)
{
	lr1110_bootloader_version_t version = { 0 };
	void *ctx;
	lr1110_status_t rc;

	/* Brief delay for USB CDC to enumerate */
	k_msleep(2000);

	printk("\n");
	printk("============================================\n");
	printk("  ZephCore LR1110 Firmware Updater\n");
	printk("  Target: transceiver FW 0x%04X\n", TARGET_FW_VERSION);
	printk("  Image:  %u words (%u KB)\n",
	       LR11XX_FIRMWARE_IMAGE_SIZE,
	       (LR11XX_FIRMWARE_IMAGE_SIZE * 4) / 1024);
	printk("============================================\n");
	printk("\n");

	led_init();
	led_on();

	/* ── Step 1: Initialize HAL ── */
	printk("[1/8] Initializing SPI and GPIOs...\n");
	if (lr1110_updater_hal_init() != 0) {
		fatal_error("HAL init failed");
	}
	ctx = lr1110_updater_get_context();

	/* ── Step 2: Hardware reset ── */
	printk("[2/8] Hardware reset LR1110...\n");
	if (lr1110_updater_hw_reset() != 0) {
		fatal_error("Hardware reset failed (BUSY stuck)");
	}

	/* ── Step 3: Read current version ── */
	printk("[3/8] Reading current firmware version...\n");
	rc = lr1110_bootloader_get_version(ctx, &version);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("GetVersion failed");
	}

	printk("  HW   = 0x%02X\n", version.hw);
	printk("  TYPE = 0x%02X", version.type);
	if (version.type == LR1110_TYPE_TRANSCEIVER) {
		printk(" (transceiver firmware)\n");
	} else if (version.type == LR1110_TYPE_BOOTLOADER) {
		printk(" (bootloader mode)\n");
	} else if (version.type == LR1110_TYPE_BL_UPDATER) {
		printk(" (bootloader-updater loader)\n");
	} else {
		printk(" (unknown)\n");
	}
	printk("  FW   = 0x%04X\n", version.fw);

	if (version.type == LR1110_TYPE_TRANSCEIVER && version.fw == TARGET_FW_VERSION) {
		printk("\nAlready running target firmware 0x%04X — no update needed!\n",
		       TARGET_FW_VERSION);
		led_off();
		updater_done();
		return 0;
	}

	/* ── Steps 4-6: bootloader path, or resume a running loader ── */
	if (version.type == LR1110_TYPE_BL_UPDATER) {
		/* A previous attempt (ours or an external tool) flashed the
		 * loader and the chip now RUNS it. Resume directly with the
		 * rewrite — the running loader is proven good, and a BUSY-held
		 * reset back to the ROM bootloader to re-flash it would only
		 * add another chance for the flash write to fail. Identity
		 * reads are bootloader-mode commands, so they are skipped. */
		if (version.fw != lr1110_bl_loader_version()) {
			fatal_error("Unknown loader version — refusing to proceed");
		}
		printk("[4/8] Loader (bootloader-updater) already running — resuming Stage A\n");
		printk("[5/8] Chip identity: not readable in loader mode — skipped\n");
		printk("[6/8] Chip bootloader: 0x%04X (legacy)\n", LR1110_BL_VERSION_LEGACY);
		printk("\n--- Stage A (resumed): chip bootloader update 0x%04X -> 0x%04X ---\n\n",
		       LR1110_BL_VERSION_LEGACY, LR1110_BL_VERSION_UPDATED);
		loader_rewrite_and_verify(ctx);
	} else {
		if (version.type != LR1110_TYPE_TRANSCEIVER &&
		    version.type != LR1110_TYPE_BOOTLOADER) {
			fatal_error("Unknown chip type — cannot proceed");
		}

		/* ALWAYS force bootloader entry via hardware reset with BUSY held LOW —
		 * even when the chip already reports bootloader mode. With an erased or
		 * invalid flash the chip only FELL BACK into the bootloader, and that
		 * fallback state is not equivalent to a deliberate BUSY-held entry
		 * (field-confirmed on LR1110: flashing from the fallback state produces
		 * images that never boot). Semtech's SWTL001 reference likewise resets
		 * to bootloader unconditionally before every update attempt. */
		printk("[4/8] Forcing LR1110 into bootloader mode (even if already there)...\n");
		if (lr1110_updater_reset_to_bootloader() != 0) {
			fatal_error("Failed to enter bootloader mode");
		}

		rc = lr1110_bootloader_get_version(ctx, &version);
		if (rc != LR1110_STATUS_OK) {
			fatal_error("GetVersion in bootloader failed");
		}
		printk("  Bootloader: HW=0x%02X TYPE=0x%02X BL=0x%04X\n",
		       version.hw, version.type, version.fw);
		if (version.type != LR1110_TYPE_BOOTLOADER) {
			fatal_error("Not in bootloader mode after reset");
		}

		/* ── Step 5: Read chip identity ── */
		{
			lr1110_bootloader_pin_t pin = { 0 };
			lr1110_bootloader_chip_eui_t chip_eui = { 0 };
			lr1110_bootloader_join_eui_t join_eui = { 0 };

			lr1110_bootloader_read_pin(ctx, pin);
			lr1110_bootloader_read_chip_eui(ctx, chip_eui);
			lr1110_bootloader_read_join_eui(ctx, join_eui);

			printk("[5/8] Chip identity:\n");
			printk("  PIN     = 0x%02X%02X%02X%02X\n",
			       pin[0], pin[1], pin[2], pin[3]);
			printk("  ChipEUI = 0x%02X%02X%02X%02X%02X%02X%02X%02X\n",
			       chip_eui[0], chip_eui[1], chip_eui[2], chip_eui[3],
			       chip_eui[4], chip_eui[5], chip_eui[6], chip_eui[7]);
			printk("  JoinEUI = 0x%02X%02X%02X%02X%02X%02X%02X%02X\n",
			       join_eui[0], join_eui[1], join_eui[2], join_eui[3],
			       join_eui[4], join_eui[5], join_eui[6], join_eui[7]);
		}

		/* ── Step 6: Chip bootloader — update if legacy ── */
		printk("[6/8] Checking chip bootloader version...\n");
		if (version.fw == LR1110_BL_VERSION_UPDATED) {
			printk("  Bootloader 0x%04X — already up to date\n", version.fw);
		} else if (version.fw == LR1110_BL_VERSION_LEGACY) {
			run_bootloader_update(ctx);
		} else {
			printk("  Unexpected bootloader version 0x%04X\n", version.fw);
			fatal_error("Unsupported chip bootloader — refusing to flash");
		}
	}

	/* ── Step 7: Flash transceiver firmware ── */
	printk("[7/8] Flashing transceiver firmware 0x%04X...\n", TARGET_FW_VERSION);
	erase_and_flash_image(ctx, "transceiver firmware",
			      lr11xx_firmware_image, LR11XX_FIRMWARE_IMAGE_SIZE);

	/* Reboot into firmware (273ms typical boot), then re-reset for a
	 * clean state before verification. */
	printk("  Rebooting LR1110 into new firmware...\n");
	lr1110_bootloader_reboot(ctx, false);
	k_msleep(500);
	if (lr1110_updater_hw_reset() != 0) {
		fatal_error("Reset after firmware flash failed");
	}

	/* ── Step 8: Verify ── */
	printk("[8/8] Verifying new firmware version...\n");
	rc = lr1110_bootloader_get_version(ctx, &version);
	if (rc != LR1110_STATUS_OK) {
		fatal_error("GetVersion after flash failed");
	}

	printk("  HW   = 0x%02X\n", version.hw);
	printk("  TYPE = 0x%02X", version.type);
	if (version.type == LR1110_TYPE_TRANSCEIVER) {
		printk(" (transceiver firmware)\n");
	} else if (version.type == LR1110_TYPE_BOOTLOADER) {
		printk(" (bootloader — firmware not running!)\n");
	} else {
		printk(" (unknown)\n");
	}
	printk("  FW   = 0x%04X\n", version.fw);

	if (version.type == LR1110_TYPE_TRANSCEIVER && version.fw == TARGET_FW_VERSION) {
		printk("\n============================================\n");
		printk("  UPDATE SUCCESSFUL!\n");
		printk("  LR1110 firmware: 0x%04X\n", version.fw);
		printk("============================================\n");
		led_on();
	} else {
		printk("\nWARNING: Expected FW 0x%04X but got TYPE=0x%02X FW=0x%04X\n",
		       TARGET_FW_VERSION, version.type, version.fw);
		printk("The update may have failed. Try again.\n");
		led_off();
	}

	/* ── Hand off to main-firmware flashing ── */
	printk("\nDone!\n");
	updater_done();

	return 0; /* never reached */
}
