#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include "BT.h"
#include "PC.h"
#include "COMMON_SEM.h"

struct bt_conn *conn_connected;
volatile bool is_notify_enabled = false;

// advertising data ad & sd
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_HYDRO_VAL),
};

// Custom Service UUID
static const struct bt_uuid_128 hydro_service_uuid = BT_UUID_INIT_128(
    BT_UUID_HYDRO_VAL
);
static const struct bt_uuid_128 hydro_data_uuid = BT_UUID_INIT_128(
    BT_UUID_HYDRO_DATA_VAL
);

static void hydro_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    is_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printf("Notification %s", is_notify_enabled ? "enabled" : "disabled");
}

ssize_t receive_callback(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0) return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    if (len >= 10) {
        uint16_t cmd = read_cmd(buf, len);
        now_command = cmd;

        if (cmd != 0) {
            k_sem_give(&rec_semaphore);
        }
    }
    
    return len;
}

// Setting up GATT Characteristic and Service
BT_GATT_SERVICE_DEFINE(hydro_svc,
BT_GATT_PRIMARY_SERVICE(&hydro_service_uuid),
BT_GATT_CHARACTERISTIC(&hydro_data_uuid.uuid,
                        BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
                        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                        NULL,
                        receive_callback,
                        NULL),
BT_GATT_CCC(hydro_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static void connected(struct bt_conn *conn, uint8_t conn_err) {
    struct bt_conn_info conn_info;	
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err) {
		printf("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

    err = bt_conn_get_info(conn, &conn_info);
	if (err) {
		printk("Failed to get connection info (%d).\n", err);
		return;
	}

    printk("CONNECTED: %s role %u\n", addr, conn_info.role);

    conn_connected = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    struct bt_conn_info conn_info;	
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    err = bt_conn_get_info(conn, &conn_info);
	if (err) {
		printk("Failed to get connection info (%d).\n", err);
		return;
	}

    if (conn_connected) {
        printk("%s: %s role %u, reason %u %s\n", __func__, addr, conn_info.role,
            reason, bt_hci_err_to_str(reason));

        conn_connected = NULL;
        bt_conn_unref(conn);

        hydro_adv_start();
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
        .connected = connected,
        .disconnected = disconnected,
        // **can add le parameter update functions here**
    };

// bt_gatt_notify from gatt.h line 1327
// bt_gatt_notify_cb from gatt.c line 2879
// bt_gatt_indicate from gatt.c line 3104
int notify_data(struct bt_conn* conn, uint8_t input_data[], uint16_t data_len) {
    // static uint8_t data[BT_ATT_MAX_ATTRIBUTE_LEN] = {0, };
	// static uint16_t data_len;
	uint16_t data_len_max;
	int err;

    data_len_max = bt_gatt_get_mtu(conn) - 3;
    if (data_len_max > BT_ATT_MAX_ATTRIBUTE_LEN) {
		data_len_max = BT_ATT_MAX_ATTRIBUTE_LEN;
	}

    if (data_len > data_len_max) data_len = data_len_max;

    err = bt_gatt_notify(conn, &hydro_svc.attrs[2], input_data, data_len);
    if (err) {
        printf("Notify failed (err %d)\n", err);
    } else {
        // printk("Notify sent (%u bytes)\n", data_len); // 너무 빠르면 로그 끄기
    }

    return err;
}

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx) {
    printk("MTU updated: TX %u RX %u\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated
};

int hydro_adv_start() {
    return bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad),
	              sd, ARRAY_SIZE(sd));
}

int hydro_bt_enable() {
    return bt_enable(NULL);
}

void enable_gatt_callbacks() {
    bt_gatt_cb_register(&gatt_callbacks);
}

int hydro_notify_data(uint8_t ipnut_data[], uint16_t data_len) {
    if (conn_connected == NULL) {
        return -ENOTCONN;
    }
    else if (!is_notify_enabled) {
        return -EACCES;
    }
    else {
        return notify_data(conn_connected, ipnut_data, data_len);
    }
}