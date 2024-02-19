#include <sstream>
#include <iostream>
#include <cstdio>
#include <wchar.h>
#include <string.h>
#include <string>
#include <cstring>
#include <vector>

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

#define MAX_STR 255

#define RAW_USAGE_ID 0x61
#define RAW_USAGE_PAGE 0xFF60

using namespace std;
using namespace fmt;
using namespace std::chrono;
using namespace std::chrono_literals; // c++14 min
using namespace spdlog;

const size_t buf_size{256};
unsigned char buf[buf_size];
wchar_t wstr[MAX_STR];

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

string read_all(FILE *fp) {
    std::stringstream ss;

    while (true) {
        size_t bytes = fread(buf, sizeof(char), buf_size, fp);

        ss.write(reinterpret_cast<char *>(buf), bytes);

        if (bytes < buf_size) {
            if (feof(stdin)) {
                break;
            }
        }
    }

    return ss.str();
}

wstring get_vendor(hid_device *dev) {
    // Read the Manufacturer String
    wstr[0] = 0x0000;
    int res = hid_get_manufacturer_string(dev, wstr, MAX_STR);
    if (res < 0) {
        printf("Unable to read manufacturer string\n");
        exit(1);
    }

    return wstring(wstr);
}

wstring get_product(hid_device *dev) {
    // Read the Product String
    wstr[0] = 0x0000;
    int res = hid_get_product_string(dev, wstr, MAX_STR);
    if (res < 0) {
        printf("Unable to read manufacturer string\n");
        exit(1);
    }

    return wstring(wstr);
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

void raw_set_key(hid_device *dev, const string &key) {
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

void raw_send_data(hid_device *dev, const string &value) {
    memset(buf,0,sizeof(buf));

    std::istringstream iss(value);

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
}

void raw_send_finish(hid_device *dev) {
    memset(buf,0,sizeof(buf));

    buf[0] = 0x0;
    buf[1] = 'F';

    debug("Sending F");
    int res = hid_write(dev, buf, 33);
    if (res < 0) {
        printf("Unable to write(): %ls\n", hid_error(dev));
    }

    check_ok(dev);
}


int main(int argc, char* argv[])
{
    int exit_status = 0;

    if (!isatty(fileno(stdout))) {
        // configure spdlog to write to this logfile
        string logpath = format("{}/.local/log/kb_reg.log", getenv("HOME"));
        std::freopen(logpath.c_str(), "a", stdout);
    }

    spdlog::cfg::load_env_levels();

    cxxopts::Options options(argv[0], "Used to write data to a register in a custom keyboard using raw hid");

    string key;
    int vendor_id{0};
    int product_id{0};

    options.add_options()
        ("h,help", "displays help text")
        ("k,key", "specifies register", cxxopts::value(key)->default_value(""))
        ("v,vendor", "specifies vendor id", cxxopts::value(vendor_id))
        ("p,product", "specifies product id", cxxopts::value(product_id))
        ;

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        cout << options.help() << endl;
        return 0;
    }

    string value = "";

    vector args = result.unmatched();
    if (args.size() > 0) {
        // Take all (unmatched) arguments and append them to make the value
        bool first = true;
        std::stringstream ss;
        for (const string &arg : args) {
            if (!first) {
                ss << " ";
            }
            ss << arg;
        }
        value = ss.str();
    } else {
        if (!isatty(fileno(stdin))) {
            value = read_all(stdin);
        } else {
            cout << "Input what you'd like to copy to the Keyboard ->";
            std::getline(std::cin, value);
        }
    }


    if (HID_API_VERSION != HID_API_MAKE_VERSION(hid_version()->major, hid_version()->minor, hid_version()->patch)) {
        error("Compile-time version is different than runtime version of hidapi.");
    }

    if (hid_init()) {
        return -100;
    }

    hid_device *raw_dev = open_raw(vendor_id, product_id);

    if (raw_dev) {
        string vendor = u8enc(get_vendor(raw_dev));
        string product = u8enc(get_product(raw_dev));
        debug("Found {} from {}", product, vendor);

        // Set the hid_read() function to be non-blocking.
        hid_set_nonblocking(raw_dev, 1);

        if (key != "") {
            raw_set_key(raw_dev, key);
        }

        raw_send_data(raw_dev, value);

        raw_send_finish(raw_dev);

        hid_close(raw_dev);
    } else {
        exit_status = -101;
    }

    /* Free static HIDAPI objects. */
    hid_exit();

    return exit_status;
}
