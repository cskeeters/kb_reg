#pragma once

#include <string>
#include <libusb.h>

std::pair<int, std::string> get_utf8_string(libusb_device_handle *handle, uint8_t id);
std::string get_ascii_string(libusb_device_handle *handle, uint8_t id);
