/*
 * SPDX-License-Identifier: Apache-2.0
 * ZephCore USB CDC Companion Transport
 *
 * V3-framed USB CDC for companion mode. Extracted from main_companion.cpp.
 * Only compiled when CONFIG_LOG is enabled (debug builds).
 *
 * USBD lifecycle + 1200-baud DFU detection + DTR state tracking live in
 * the shared ZephyrUSBCDC module; this file just runs the V3 frame parser
 * on top of the CDC ACM UART and reacts to DTR-drop events from there.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zephcore_usb, CONFIG_ZEPHCORE_USB_LOG_LEVEL);

#include <ZephyrBLE.h>
#include <app/CompanionMesh.h>

#include "ZephyrCompanionUSB.h"
#include "ZephyrUSBCDC.h"

/* MAX_FRAME_SIZE defined in CompanionMesh.h */

#define USB_RING_BUF_SIZE     512     /* USB RX ring buffer size */
#define USB_FRAME_TIMEOUT_MS  2000    /* Partial frame timeout - reset parser after 2s of no completion */

/* USB CDC state */
static const struct device *usb_dev;
static uint8_t usb_ring_buf_data[USB_RING_BUF_SIZE];
static struct ring_buf usb_ring_buf;
static uint8_t usb_rx_buf[MAX_FRAME_SIZE + 2];  /* +2 for length prefix */
static uint16_t usb_rx_idx;
static uint16_t usb_frame_len;  /* Expected frame length (0 = waiting for header) */
static uint32_t usb_frame_start_time;  /* Timestamp of first byte in current frame */

/* Work item for deferred V3 frame processing (set by init) */
static struct k_work *s_rx_work;

/* Work items */
static void usb_rx_work_fn(struct k_work *work);

K_WORK_DEFINE(usb_rx_work, usb_rx_work_fn);

/* USB CDC UART interrupt callback - puts bytes in ring buffer */
static void usb_uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[64];
			int recv_len = uart_fifo_read(dev, buf, sizeof(buf));
			if (recv_len > 0) {
				ring_buf_put(&usb_ring_buf, buf, recv_len);
				k_work_submit(&usb_rx_work);
			}
		}
	}
}

/* USB RX work - parses V3 frames from ring buffer */
static void usb_rx_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	uint8_t byte;

	/* Timeout partial frames — if we've been accumulating bytes for too long
	 * without completing a frame, reset the parser state */
	if (usb_rx_idx > 0 && (k_uptime_get_32() - usb_frame_start_time) > USB_FRAME_TIMEOUT_MS) {
		LOG_WRN("usb_rx: partial frame timeout (idx=%u, expected=%u), resync",
			usb_rx_idx, usb_frame_len);
		usb_frame_len = 0;
		usb_rx_idx = 0;
	}

	while (ring_buf_get(&usb_ring_buf, &byte, 1) == 1) {
		/* V3 framing: [len_lo][len_hi][payload...] */
		if (usb_rx_idx == 0) {
			/* First byte of length */
			usb_rx_buf[0] = byte;
			usb_rx_idx = 1;
			usb_frame_start_time = k_uptime_get_32();
		} else if (usb_rx_idx == 1) {
			/* Second byte of length */
			usb_rx_buf[1] = byte;
			usb_frame_len = usb_rx_buf[0] | (usb_rx_buf[1] << 8);
			usb_rx_idx = 2;

			if (usb_frame_len == 0 || usb_frame_len > MAX_FRAME_SIZE) {
				LOG_WRN("usb_rx: invalid frame len %u, resync", usb_frame_len);
				usb_frame_len = 0;
				usb_rx_idx = 0;
			}
		} else {
			/* Payload bytes */
			usb_rx_buf[usb_rx_idx++] = byte;

			if (usb_rx_idx >= usb_frame_len + 2) {
				/* Frame complete - queue it */
				uint8_t *payload = &usb_rx_buf[2];
				uint16_t payload_len = usb_frame_len;

				LOG_DBG("usb_rx: frame complete len=%u hdr=0x%02x", payload_len, payload[0]);

				/* Check for CMD_APP_START to switch interface.
				 * CMD_APP_START is 0x01 — see CompanionMesh.cpp:26.
				 * (Previously hardcoded 0x03 with the same comment, which
				 * is actually CMD_SEND_CHANNEL_TXT_MSG and meant the USB
				 * handshake silently dropped the app's first frame.) */
				if (payload_len >= 1 && payload[0] == 0x01 /* CMD_APP_START */) {
					if (zephcore_ble_get_active_iface() == ZEPHCORE_IFACE_BLE &&
					    zephcore_ble_is_connected()) {
						LOG_INF("usb_rx: CMD_APP_START, disconnecting BLE");
						zephcore_ble_disconnect();
					}
					zephcore_ble_set_active_iface(ZEPHCORE_IFACE_USB);
					LOG_INF("usb_rx: active_iface = IFACE_USB");
				}

				/* Only process if USB is active interface */
				if (zephcore_ble_get_active_iface() == ZEPHCORE_IFACE_USB) {
					struct {
						uint16_t len;
						uint8_t buf[MAX_FRAME_SIZE];
					} f;
					f.len = payload_len;
					memcpy(f.buf, payload, payload_len);
					/* sysworkq handles V3-protocol parsing; main only wakes
					 * if downstream LoRa work gets enqueued (via TX_DRAIN). */
					if (k_msgq_put(zephcore_ble_get_recv_queue(), &f, K_NO_WAIT) == 0) {
						k_work_submit(s_rx_work);
					}
				}

				/* Reset for next frame */
				usb_frame_len = 0;
				usb_rx_idx = 0;
			}
		}
	}
}

/* DTR-transition callback from the shared ZephyrUSBCDC module.
 * On drop: host closed the port → reset parser, hand control back to BLE. */
static void on_dtr_change(bool dtr_active)
{
	if (dtr_active) {
		return;
	}
	LOG_INF("usb_dtr: DTR dropped, USB disconnected");
	if (zephcore_ble_get_active_iface() == ZEPHCORE_IFACE_USB) {
		zephcore_ble_set_active_iface(ZEPHCORE_IFACE_NONE);
		LOG_INF("usb_dtr: active_iface = IFACE_NONE");
	}
	ring_buf_reset(&usb_ring_buf);
	usb_frame_len = 0;
	usb_rx_idx = 0;
}

/* Send frame over USB with V3 length prefix */
size_t zephcore_usb_companion_write_frame(const uint8_t *src, size_t len)
{
	if (!usb_dev || len == 0 || len > MAX_FRAME_SIZE) {
		return 0;
	}

	/* Send length prefix (little-endian) */
	uint8_t len_lo = (uint8_t)(len & 0xFF);
	uint8_t len_hi = (uint8_t)((len >> 8) & 0xFF);

	uart_poll_out(usb_dev, len_lo);
	uart_poll_out(usb_dev, len_hi);

	/* Send payload */
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(usb_dev, src[i]);
	}

	LOG_DBG("usb_write_frame: sent len=%u hdr=0x%02x", (unsigned)len, src[0]);
	return len;
}

void zephcore_usb_companion_reset_rx(void)
{
	ring_buf_reset(&usb_ring_buf);
	usb_frame_len = 0;
	usb_rx_idx = 0;
}

void zephcore_usb_companion_init(struct k_event *mesh_events,
				 struct k_work *rx_work,
				 uint32_t mesh_event_ble_rx,
				 void *board)
{
	ARG_UNUSED(board);
	ARG_UNUSED(mesh_events);
	ARG_UNUSED(mesh_event_ble_rx);

	s_rx_work = rx_work;

#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_cdc_acm_uart)
	usb_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
	if (device_is_ready(usb_dev)) {
		LOG_INF("USB CDC device ready: %s", usb_dev->name);
		ring_buf_init(&usb_ring_buf, sizeof(usb_ring_buf_data), usb_ring_buf_data);

		/* Set up UART interrupt callback */
		uart_irq_callback_set(usb_dev, usb_uart_isr);
		uart_irq_rx_enable(usb_dev);

		/* DTR state changes (including disconnect) reach us via the
		 * shared usbd_msg_callback — no polling work needed. */
		zephcore_usbd_set_dtr_cb(on_dtr_change);
	} else {
		LOG_WRN("USB CDC device not ready");
		usb_dev = NULL;
	}
#endif
}
