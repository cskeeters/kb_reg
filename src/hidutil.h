#pragma once

#include <string>
#include <hidapi.h>

std::wstring get_vendor(hid_device *dev);
std::wstring get_product(hid_device *dev);
