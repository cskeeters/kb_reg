#include <sstream>
#include <cstdio>
#include <wchar.h>
#include <string.h>
#include <string>
#include <cstring>

#include <cstdlib>
#include <chrono>

#include <unistd.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <hidapi.h>
#include <cxxopts.hpp>

#include <fmt/core.h>
#include <fmt/xchar.h>

#include "utf8util.h"

// Fallback/example
#ifndef HID_API_MAKE_VERSION
#define HID_API_MAKE_VERSION(mj, mn, p) (((mj) << 24) | ((mn) << 8) | (p))
#endif
#ifndef HID_API_VERSION
#define HID_API_VERSION HID_API_MAKE_VERSION(HID_API_VERSION_MAJOR, HID_API_VERSION_MINOR, HID_API_VERSION_PATCH)
#endif

#define RAW_USAGE_ID 0x61
#define RAW_USAGE_PAGE 0xFF60

using namespace std;
using namespace fmt;
using namespace std::chrono;
using namespace std::chrono_literals; // c++14 min
using namespace spdlog;

static const size_t buf_size{256};
static unsigned char buf[buf_size];

void hid_version_check()
{
    if (HID_API_VERSION != HID_API_MAKE_VERSION(hid_version()->major, hid_version()->minor, hid_version()->patch)) {
        error("Compile-time version is different than runtime version of hidapi.");
    }
}

hid_device_info* find_raw(hid_device_info *devs, int usage_id, int usage_page)
{
    for (hid_device_info *i = devs; i != nullptr; i=i->next) {
        if (i->usage == usage_id) {
            if (i->usage_page == usage_page) {
                //info("Found raw at {}",i->path);
                return i;
            }
        }
    }

    return nullptr;
}

int read(hid_device *dev, unsigned char *buf, size_t amt, duration<float, std::milli> timeout)
{
    steady_clock::time_point start = high_resolution_clock::now();

    size_t read{0};

    while (true) {
        int res = hid_read(dev, buf, amt - read);
        if (res < 0) {
            error("Unable to read(): {}", u8enc(hid_error(dev)));
            break;
        }

        read += res;
        if (read == amt) {
            return read;
        }

        steady_clock::time_point now = high_resolution_clock::now();

        if ((now - start) > timeout) {
            error("hid_read() timeout");
            break;
        }

        usleep(200);
    }
    return -2;
}


hid_device *open_raw(int vendor_id, int product_id)
{
    hid_device *raw_dev = nullptr;

    struct hid_device_info *devs = hid_enumerate(vendor_id, product_id);
    hid_device_info* raw_dev_info = find_raw(devs, RAW_USAGE_ID, RAW_USAGE_PAGE);

    if (raw_dev_info != nullptr) {
        // Open before we free devs
        raw_dev = hid_open_path(raw_dev_info->path);

        if (!raw_dev) {
            error("Unable to open raw device");
        }
    } else {
        error("Unable to find raw device");
    }

    hid_free_enumeration(devs);
    return raw_dev;
}

// checks for return string from keyboard and prints errors
void check_ok(hid_device *dev) {
    memset(buf,0,sizeof(buf));

    int res = read(dev, buf, 32, 5ms);
    if (res == -1) {
        printf("Error reading from usb device\n");
    }
    if (res == -2) {
        printf("Timeout reading from usb device\n");
    }
    if (res > 0) {
        if (strcmp((char*)buf, "OK") != 0) {
            printf("Error from keyboard: %s\n", buf);
        }
    }
}

void set_key(hid_device *dev, const string &key) {
    memset(buf,0,sizeof(buf));

    buf[0] = 0x0;
    buf[1] = 'K';
    buf[2] = key.at(0);

    debug("Sending K");
    int res = hid_write(dev, buf, 33);
    if (res < 0) {
        printf("Unable to write(): %ls\n", hid_error(dev));
    }

    check_ok(dev);
}

void store_data(hid_device *dev, const string &data) {
    memset(buf,0,sizeof(buf));

    std::istringstream iss(data);

    buf[0] = 0x0;
    buf[1] = 'S';

    debug("Sending S");
    iss.read(reinterpret_cast<char *>(&buf[2]), 31);
    int res = hid_write(dev, buf, 33);
    if (res < 0) {
        printf("Unable to write(): %ls\n", hid_error(dev));
    }
    check_ok(dev);

    while (!iss.eof()) {
        memset(buf,0,sizeof(buf));
        buf[0] = 0x0;
        buf[1] = 'A';

        iss.read(reinterpret_cast<char *>(&buf[2]), 31);
        int res = hid_write(dev, buf, 33);
        if (res < 0) {
            printf("Unable to write(): %ls\n", hid_error(dev));
        }
        check_ok(dev);
    }

    memset(buf,0,sizeof(buf));

    buf[0] = 0x0;
    buf[1] = 'F';

    debug("Sending F");
    res = hid_write(dev, buf, 33);
    if (res < 0) {
        printf("Unable to write(): %ls\n", hid_error(dev));
    }

    check_ok(dev);
}
