/**
 * @file    ble.c
 * @author  Matthijs Bakker
 * @date    2026-02-03
 * @brief   BLE Keyboard HID implementation
 * 
 * Adapted from 'sdk-nrf/peripheral_hids_keyboard', added support for media keys.
 */

#include "ble.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/hids.h>

#define BASE_USB_HID_SPEC_VERSION   		0x0101
#define INPUT_REPORT_KEYS_MAX_LEN 			(1 + 1 + 6) // modifiers + reserved + keys[6]
#define INPUT_REPORT_CONSUMER_MAX_LEN		1
#define OUTPUT_REPORT_MAX_LEN            	1

// Report IDs
#define INPUT_REP_KEYS_REF_ID            	1
#define INPUT_REP_CONSUMER_REF_ID           2
#define OUTPUT_REP_KEYS_REF_ID           	0

// Internal report table indexes
#define OUTPUT_REP_KEYS_IDX					0
#define INPUT_REP_KEYS_IDX					0
#define INPUT_REP_CONSUMER_IDX				1

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
		      (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, (sizeof(CONFIG_BT_DEVICE_NAME) - 1)),
};

static struct conn_mode {
	struct bt_conn *conn;
	bool in_boot_mode;
} conn_mode[CONFIG_BT_HIDS_MAX_CLIENT_COUNT];

static struct k_work pairing_work;

struct pairing_data_mitm {
	struct bt_conn *conn;
	unsigned int passkey;
};

K_MSGQ_DEFINE(mitm_queue, sizeof(struct pairing_data_mitm), CONFIG_BT_HIDS_MAX_CLIENT_COUNT, 4);
K_MSGQ_DEFINE(input_queue, sizeof(ble_key_input_t), 10, 1);

BT_HIDS_DEF(hids_obj, OUTPUT_REPORT_MAX_LEN, INPUT_REPORT_KEYS_MAX_LEN, INPUT_REPORT_CONSUMER_MAX_LEN);

LOG_MODULE_REGISTER(ble);

static void advertising_start(void)
{
	const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
						BT_LE_ADV_OPT_CONN,
						BT_GAP_ADV_FAST_INT_MIN_1,
						BT_GAP_ADV_FAST_INT_MAX_1,
						NULL);
	int err;

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		if (err == -EALREADY) {
			LOG_INF("Advertising continued\n");
		} else {
			LOG_ERR("Advertising failed to start (err %d)\n", err);
		}

		return;
	}

	LOG_INF("Advertising successfully started\n");
}

static void pairing_process(struct k_work *work)
{
	int err;
	struct pairing_data_mitm pairing_data;

	char addr[BT_ADDR_LE_STR_LEN];

	err = k_msgq_peek(&mitm_queue, &pairing_data);

	if (err) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(pairing_data.conn),
			  addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u", addr, pairing_data.passkey);
	LOG_INF("Hold VOLUME UP + VOLUME DOWN simultaneously for 3 seconds to pair.");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Failed to connect to %s 0x%02x %s\n", addr, err, bt_hci_err_to_str(err));
		return;
	}

	LOG_INF("Connected %s\n", addr);

	err = bt_hids_connected(&hids_obj, conn);

	if (err) {
		LOG_ERR("Failed to notify HID service about connection\n");
		return;
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			conn_mode[i].conn = conn;
			conn_mode[i].in_boot_mode = false;
			break;
		}
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (!conn_mode[i].conn) {
			advertising_start();
			return;
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int err;
	bool is_any_dev_connected = false;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected from %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

	err = bt_hids_disconnected(&hids_obj, conn);

	if (err) {
		LOG_ERR("Failed to notify HID service about disconnection\n");
	}

	for (size_t i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn == conn) {
			conn_mode[i].conn = NULL;
		} else {
			if (conn_mode[i].conn) {
				is_any_dev_connected = true;
			}
		}
	}

	advertising_start();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Security failed: %s level %u err %d %s\n",
			    addr, level, err, bt_security_err_to_str(err));
	}

	LOG_INF("Security changed: %s level %u\n", addr, level);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void hids_outp_rep_handler(struct bt_hids_rep *rep,
				  struct bt_conn *conn,
				  bool write)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!write) {
		LOG_INF("Output report read\n");
		return;
	};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Output report has been received %s\n", addr);
}

static void hids_boot_kb_outp_rep_handler(struct bt_hids_rep *rep,
					  struct bt_conn *conn,
					  bool write)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!write) {
		LOG_INF("Output report read\n");
		return;
	};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Boot Keyboard Output report has been received %s\n", addr);
}

static void hids_pm_evt_handler(enum bt_hids_pm_evt evt,
				struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];
	size_t i;

	for (i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn == conn) {
			break;
		}
	}

	if (i >= CONFIG_BT_HIDS_MAX_CLIENT_COUNT) {
		LOG_ERR("Cannot find connection handle when processing PM");
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	switch (evt) {
		case BT_HIDS_PM_EVT_BOOT_MODE_ENTERED:
			LOG_INF("Boot mode entered %s\n", addr);
			conn_mode[i].in_boot_mode = true;
			break;

		case BT_HIDS_PM_EVT_REPORT_MODE_ENTERED:
			LOG_INF("Report mode entered %s\n", addr);
			conn_mode[i].in_boot_mode = false;
			break;
		default:
			break;
	}
}

static void hid_init(void)
{
	struct bt_hids_init_param    hids_init_obj = { 0 };
	struct bt_hids_inp_rep       *hids_inp_rep;
	struct bt_hids_outp_feat_rep *hids_outp_rep;
	int err;

	static const uint8_t report_map[] = {
		0x05, 0x01,       /* Usage Page (Generic Desktop) */
		0x09, 0x06,       /* Usage (Keyboard) */
		0xA1, 0x01,       /* Collection (Application) */

		0x85, 0x01,       /* Report ID 1: Keyboard */
		0x05, 0x07,       /* Usage Page (Key Codes) */
		0x19, 0xe0,       /* Usage Minimum (224) */
		0x29, 0xe7,       /* Usage Maximum (231) */
		0x15, 0x00,       /* Logical Minimum (0) */
		0x25, 0x01,       /* Logical Maximum (1) */
		0x75, 0x01,       /* Report Size (1) */
		0x95, 0x08,       /* Report Count (8) */
		0x81, 0x02,       /* Input (Data, Variable, Absolute) */

		0x95, 0x01,       /* Report Count (1) */
		0x75, 0x08,       /* Report Size (8) */
		0x81, 0x01,       /* Input (Constant) reserved byte(1) */

		0x95, 0x06,       /* Report Count (6) */
		0x75, 0x08,       /* Report Size (8) */
		0x15, 0x00,       /* Logical Minimum (0) */
		0x25, 0x65,       /* Logical Maximum (101) */
		
		0x05, 0x07,       /* Usage Page (Key codes) */
		0x19, 0x00,       /* Usage Minimum (0) */
		0x29, 0x65,       /* Usage Maximum (101) */
		0x81, 0x00,       /* Input (Data, Array) Key array(6 bytes) */

		0x95, 0x05,       /* Report Count (5) */
		0x75, 0x01,       /* Report Size (1) */
		0x05, 0x08,       /* Usage Page (Page# for LEDs) */
		0x19, 0x01,       /* Usage Minimum (1) */
		0x29, 0x05,       /* Usage Maximum (5) */
		0x91, 0x02,       /* Output (Data, Variable, Absolute), */
		0x95, 0x01,       /* Report Count (1) */
		0x75, 0x03,       /* Report Size (3) */
		0x91, 0x01,       /* Output (Data, Variable, Absolute), */

		0xC0,             /* End Collection (Application) */

		0x05, 0x0C,        // Usage Page (Consumer)
		0x09, 0x01,        // Usage (Consumer Control)
		0xA1, 0x01,        // Collection (Application)

		0x85, 0x02,        /* Report ID 2: Consumer */
		0x15, 0x00,        //   Logical Minimum (0)
		0x25, 0x01,        //   Logical Maximum (1)

		0x09, 0xE2,        //   Usage (Mute)
		0x09, 0xCD,        //   Usage (Play/Pause)
		0x09, 0xE9,        //   Usage (Volume Increment)
		0x09, 0xEA,        //   Usage (Volume Decrement)

		0x75, 0x01,        //   Report Size (1)
		0x95, 0x04,        //   Report Count (4)
		0x81, 0x02,        //   Input (Data,Var,Abs)

		0x75, 0x01,        //   Report Size (1)
		0x95, 0x04,        //   Report Count (4)
		0x81, 0x01,        //   Input (Const,Arr,Abs) ; padding

		0xC0               // End Collection
	};

	hids_init_obj.rep_map.data = report_map;
	hids_init_obj.rep_map.size = sizeof(report_map);

	hids_init_obj.info.bcd_hid = BASE_USB_HID_SPEC_VERSION;
	hids_init_obj.info.b_country_code = 0x00;
	hids_init_obj.info.flags = (BT_HIDS_REMOTE_WAKE | BT_HIDS_NORMALLY_CONNECTABLE);

	hids_inp_rep = &hids_init_obj.inp_rep_group_init.reports[INPUT_REP_KEYS_IDX];
	hids_inp_rep->size = INPUT_REPORT_KEYS_MAX_LEN;
	hids_inp_rep->id = INPUT_REP_KEYS_REF_ID;
	hids_init_obj.inp_rep_group_init.cnt++;

	hids_inp_rep = &hids_init_obj.inp_rep_group_init.reports[INPUT_REP_CONSUMER_IDX];
	hids_inp_rep->size = INPUT_REPORT_CONSUMER_MAX_LEN;
	hids_inp_rep->id = INPUT_REP_CONSUMER_REF_ID;
	hids_init_obj.inp_rep_group_init.cnt++;

	hids_outp_rep = &hids_init_obj.outp_rep_group_init.reports[OUTPUT_REP_KEYS_IDX];
	hids_outp_rep->size = OUTPUT_REPORT_MAX_LEN;
	hids_outp_rep->id = OUTPUT_REP_KEYS_REF_ID;
	hids_outp_rep->handler = hids_outp_rep_handler;
	hids_init_obj.outp_rep_group_init.cnt++;

	hids_init_obj.is_kb = true;
	hids_init_obj.boot_kb_outp_rep_handler = hids_boot_kb_outp_rep_handler;
	hids_init_obj.pm_evt_handler = hids_pm_evt_handler;

	err = bt_hids_init(&hids_obj, &hids_init_obj);
	__ASSERT(err == 0, "HIDS initialization failed\n");
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	int err;

	struct pairing_data_mitm pairing_data;

	pairing_data.conn    = bt_conn_ref(conn);
	pairing_data.passkey = passkey;

	err = k_msgq_put(&mitm_queue, &pairing_data, K_NO_WAIT);

	if (err) {
		LOG_ERR("Pairing queue is full. Purge previous data.\n");
	}

	/* In the case of multiple pairing requests, trigger
	 * pairing confirmation which needed user interaction only
	 * once to avoid display information about all devices at
	 * the same time. Passkey confirmation for next devices will
	 * be proccess from queue after handling the earlier ones.
	 */
	if (k_msgq_num_used_get(&mitm_queue) == 1) {
		k_work_submit(&pairing_work);
	}
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct pairing_data_mitm pairing_data;

	if (k_msgq_peek(&mitm_queue, &pairing_data) != 0) {
		return;
	}

	if (pairing_data.conn == conn) {
		bt_conn_unref(pairing_data.conn);
		k_msgq_get(&mitm_queue, &pairing_data, K_NO_WAIT);
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_ERR("Pairing failed conn: %s, reason %d %s\n",
		    addr, reason, bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

static int media_report_send(ble_hid_key_t pressed_keys)
{
	uint8_t data[INPUT_REPORT_CONSUMER_MAX_LEN];
	int i, err;

	data[0] = pressed_keys;
	
	LOG_HEXDUMP_INF(data, INPUT_REPORT_CONSUMER_MAX_LEN, "Media data");

	for (i = 0; i < CONFIG_BT_HIDS_MAX_CLIENT_COUNT; i++) {
		if (conn_mode[i].conn) {
			if (conn_mode[i].in_boot_mode) {
				LOG_WRN("Connection %d in boot mode, skipping media report", i);
				continue;
			}

			err = bt_hids_inp_rep_send(&hids_obj, conn_mode[i].conn, INPUT_REP_CONSUMER_IDX, data, sizeof(data), NULL);

			if (err) {
				LOG_ERR("Key report send error: %d\n", err);
				return err;
			}
		}
	}

	return 0;
}

static void num_comp_reply(bool accept)
{
	struct pairing_data_mitm pairing_data;
	struct bt_conn *conn;

	if (k_msgq_get(&mitm_queue, &pairing_data, K_NO_WAIT) != 0) {
		return;
	}

	conn = pairing_data.conn;

	if (accept) {
		bt_conn_auth_passkey_confirm(conn);
		LOG_INF("Numeric Match, conn %p\n", conn);
	} else {
		bt_conn_auth_cancel(conn);
		LOG_INF("Numeric Reject, conn %p\n", conn);
	}

	bt_conn_unref(pairing_data.conn);

	if (k_msgq_num_used_get(&mitm_queue)) {
		k_work_submit(&pairing_work);
	}
}

static void button_thread(void)
{
    ble_key_input_t input;
	k_timeout_t timeout = K_FOREVER;
	int err;

    while (true) {
        err = k_msgq_get(&input_queue, &input, timeout);

		timeout = K_FOREVER;

		if (err == -EAGAIN) {
			LOG_WRN("Accept pairing");

			if (k_msgq_num_used_get(&mitm_queue)) {
				num_comp_reply(true);
				continue;
			}
		}

		if (input.pressed_mask == (BLE_HID_KEY_VOLUME_UP | BLE_HID_KEY_VOLUME_DOWN)) {
			timeout = K_SECONDS(3);
			continue;
		}

		media_report_send(input.pressed_mask);
    }
}

int ble_init(void)
{
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);

	if (err) {
		LOG_ERR("Failed to register authorization callbacks.\n");
		return 0;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);

	if (err) {
		LOG_ERR("Failed to register authorization info callbacks.\n");
		return 0;
	}

	hid_init();

	err = bt_enable(NULL);

	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	LOG_INF("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	advertising_start();

	k_work_init(&pairing_work, pairing_process);

	return 0;
}

void ble_send_key_input(const ble_key_input_t *input)
{
    k_msgq_put(&input_queue, input, K_NO_WAIT);
}

K_THREAD_DEFINE(button_thread_id, 2048, button_thread, NULL, NULL, NULL, 14, 0, 0);
