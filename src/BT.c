#include "BT.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>

// advertising data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_HYDRO_VAL),
};

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
		printf("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

	printf("Connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    printf("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
        .connected = connected,
        .disconnected = disconnected,
    };

int hydro_adv_start() {
    return bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
	              sd, ARRAY_SIZE(sd));
}

int hydro_bt_enable() {
    return bt_enable(NULL);
}