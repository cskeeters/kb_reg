#pragma once

#include <hidapi.h>
#include <string>

void hid_version_check();

hid_device *open_raw(int vendor_id, int product_id);

std::wstring get_vendor(hid_device *hid_dev);
std::wstring get_product(hid_device *hid_dev);

// Switch current key in keyboard
void set_key(hid_device *dev, const std::string &key);

// sends value to they keyboard. Will be associated with current (or last set) key
void store_data(hid_device *dev, const std::string &value);
