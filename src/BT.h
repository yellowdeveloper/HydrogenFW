#ifndef BT_H
#define BT_H

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// custom uuid: 7f2fa2dc-2871-4e9e-9328-00372d10e04f (created via https://www.uuidgenerator.net/version4)
#define BT_UUID_HYDRO_VAL \
	BT_UUID_128_ENCODE(0x7f2fa2dc, 0x2871, 0x4e9e, 0x9328, 0x00372d10e04f)

// custom uuid: a594f566-4481-4297-b817-9b58025116ef (created via https://www.uuidgenerator.net/version4)
#define BT_UUID_HYDRO_DATA_VAL \
	BT_UUID_128_ENCODE(0xa594f566, 0x4481, 0x4297, 0xb817, 0x9b58025116ef)

#endif