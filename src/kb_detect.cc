#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

#include <unistd.h>

#include <spdlog/spdlog.h>
#include <libusb.h>
#include <toml++/toml.hpp>

#include "utf8util.h"

// from libusb/libusb/descriptor.c
#define DESC_HEADER_LENGTH 2

using namespace std;
using namespace std::filesystem;
using namespace fmt;
using namespace spdlog;

volatile bool exit_flag{false};

const string default_kb_reg_path{"/usr/local/bin/kb_reg"};

void handle_signal(int sig) {
   // INT can be issued from a terminal only
   if (sig == SIGINT) {
      exit_flag=true;
   }

   // Issued from systemd, try to shutdown gracefully
   if (sig == SIGTERM) {
      exit_flag=true;
   }
}

struct string_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wData[255]; // Make this a complete type.
} LIBUSB_PACKED;

union string_desc_buf {
        struct string_descriptor desc;
        uint8_t buf[255];       /* Some devices choke on size > 255 */
        uint16_t align;         /* Force 2-byte alignment */
};

pair<int, string> get_utf8_string(libusb_device_handle *handle, uint8_t id)
{
    /* Asking for the zero'th index is special - it returns a string
     * descriptor that contains all the language IDs supported by the
     * device. Typically there aren't many - often only one. Language
     * IDs are 16 bit numbers, and they start at the third byte in the
     * descriptor. There's also no point in trying to read descriptor 0
     * with this function. See USB 2.0 specification section 9.6.7 for
     * more information.
     */

    if (id == 0)
        return pair(LIBUSB_ERROR_INVALID_PARAM, nullptr);

    string_desc_buf str;
    int r = libusb_get_string_descriptor(handle, 0, 0, str.buf, 4);
    if (r < 0)
        return pair(r, nullptr);
    else if (r != 4 || str.desc.bLength < 4)
        return pair(LIBUSB_ERROR_IO, nullptr);
    else if (str.desc.bDescriptorType != LIBUSB_DT_STRING)
        return pair(LIBUSB_ERROR_IO, nullptr);
    else if (str.desc.bLength & 1)
        warn("suspicious bLength %u for language ID string descriptor", str.desc.bLength);

    // en-US: 0x0409
    //uint16_t langid = 0x0409;
    uint16_t langid = libusb_le16_to_cpu(str.desc.wData[0]);
    debug("String ID {} returned langid 0x{:04x}", id, langid);

    r = libusb_get_string_descriptor(handle, id, langid, str.buf, sizeof(str.buf));
    if (r < 0)
        return pair(r, nullptr);
    else if (r < DESC_HEADER_LENGTH || str.desc.bLength > r)
        return pair(LIBUSB_ERROR_IO, nullptr);
    else if (str.desc.bDescriptorType != LIBUSB_DT_STRING)
        return pair(LIBUSB_ERROR_IO, nullptr);
    else if ((str.desc.bLength & 1) || str.desc.bLength != r)
        warn("suspicious bLength %u for string descriptor (read %d)", str.desc.bLength, r);

    /* The descriptor has this number of wide characters */
    int src_max = (str.desc.bLength - 1 - 1) / 2;

    wchar_t wbuffer[256];
    for (int i = 0; i<src_max; ++i) {
        wbuffer[i] = libusb_le16_to_cpu(str.desc.wData[i]);
    }

    wstring wstr(wbuffer, src_max);

    return pair(0, u8enc(wstr));
}

string get_ascii_string(libusb_device_handle *handle, uint8_t id)
{
    unsigned char data[256];
    size_t bytes_loaded = libusb_get_string_descriptor_ascii(handle, id, data, sizeof(data));
    return string(reinterpret_cast<char*>(data), bytes_loaded);
}

bool is_custom_keyboard(toml::table tbl, int vendor_id, int product_id)
{
    auto keyboards = tbl["keyboards"].as_array();
    for (auto &&i : *keyboards) {
        toml::table *keyboard = i.as_table();
        if ((*keyboard)["vendor"].value<int64_t>() == vendor_id) {
            if ((*keyboard)["product"].value<int64_t>() == product_id) {
                return true;
            }
        }
    }

    return false;
}

string get_config_path() {
    return string(getenv("HOME")) + "/.kb_detect.toml";
}

string get_log_path() {
    return format("{}/.local/log/kb_detect.log", getenv("HOME"));
}

void run_kb_reg(const string &key, const string &value, const string &kb_reg_path, int vendor_id, int product_id) {
    string cmd = format("printf '{}' | {} -k {} -v {} -p {}", value.c_str(), kb_reg_path, key, vendor_id, product_id);

    int ret = system(cmd.c_str());
    if (ret != 0) {
        error("Error running: {}", cmd);
    }
}

void configure_keyboard(toml::table &tbl, libusb_device *dev, struct libusb_device_descriptor &desc) {
    libusb_device_handle *handle = nullptr;
    int rc = libusb_open(dev, &handle);
    if (LIBUSB_SUCCESS != rc) {
        error("No access to device: {}", libusb_strerror((enum libusb_error)rc));
        return;
    }

    int err;
    string vendor, product;

    tie(err, vendor) = get_utf8_string(handle, desc.iManufacturer);
    if (err != 0) {
        error("Error getting vendor");
        return;
    }

    tie(err, product) = get_utf8_string(handle, desc.iProduct);
    if (err != 0) {
        error("Error getting product");
        return;
    }

    libusb_close(handle);


    string kb_reg_path = default_kb_reg_path;
    if (!!tbl["kb_reg_path"]) {
        kb_reg_path = tbl["kb_reg_path"].value<std::string>().value();
    }

    auto keys = tbl["keys"].as_table();
    for (auto pair : *keys) {
        string key = string(pair.first.str());
        string value = pair.second.value_or(""s);

        run_kb_reg(key, value, kb_reg_path, desc.idVendor, desc.idProduct);
    }

    run_kb_reg(".", "", kb_reg_path, desc.idVendor, desc.idProduct);

    info("Initialized {} from {}", product, vendor);
}

void configure_if_connected(libusb_context *usb_ctx) {
    libusb_device **usb_devices;
    ssize_t dev_count = libusb_get_device_list(usb_ctx, &usb_devices);

    for (int i=0; i<dev_count; ++i) {
        struct libusb_device_descriptor desc;
        int rc = libusb_get_device_descriptor(usb_devices[i], &desc);
        if (LIBUSB_SUCCESS != rc) {
            error("Error getting device descriptor: {}", libusb_strerror((enum libusb_error)rc));
            error("Aborting detection");
            libusb_free_device_list(usb_devices, true);
            return;
        }

        auto tbl = toml::parse_file(get_config_path());
        if (is_custom_keyboard(tbl, desc.idVendor, desc.idProduct)) {
            configure_keyboard(tbl, usb_devices[i], desc);
        }

    }

    libusb_free_device_list(usb_devices, true);
}

// return 1 to stop listening
static int LIBUSB_CALL hotplug_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
    // Prevent warnings for unused variables
    (void)ctx;
    (void)event;
    (void)user_data;

    struct libusb_device_descriptor desc;
    int rc = libusb_get_device_descriptor(dev, &desc); // desc does not need to be freed
    if (LIBUSB_SUCCESS != rc) {
        error("Error getting device descriptor: {}", libusb_strerror((enum libusb_error)rc));
        return 0;
    }

    info("Device attached: {:04x}:{:04x}", desc.idVendor, desc.idProduct);

    toml::table tbl = toml::parse_file(get_config_path());

    if (is_custom_keyboard(tbl, desc.idVendor, desc.idProduct)) {
        configure_keyboard(tbl, dev, desc);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    string config_path = get_config_path();
    if (!exists(config_path)) {
        error("You must create {}", config_path);
        return 1;
    }

    string log_folder_path = path(get_log_path()).parent_path();
    if (!exists(log_folder_path)) {
        cout << format("You must run: mkdir -p {}", log_folder_path) << endl;
        return 1;
    }

    if (!isatty(fileno(stdout))) {
        // configure spdlog to write to this logfile
        string logpath = get_log_path();
        std::freopen(logpath.c_str(), "w", stdout);
    }

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    libusb_hotplug_callback_handle hp[2];
    int product_id{0}, vendor_id{0}, class_id{0};
    int rc;

    vendor_id  = (argc > 1) ? (int)strtol (argv[1], nullptr, 0) : LIBUSB_HOTPLUG_MATCH_ANY;
    product_id = (argc > 2) ? (int)strtol (argv[2], nullptr, 0) : LIBUSB_HOTPLUG_MATCH_ANY;

    libusb_context *usb_ctx;
    // Eventually, this needs to be replaced with libusb_init_context
    rc = libusb_init(&usb_ctx);
    if (LIBUSB_SUCCESS != rc)
    {
        error("failed to initialise libusb: {}", libusb_strerror((enum libusb_error)rc));
        return EXIT_FAILURE;
    }

    configure_if_connected(usb_ctx);

    if (!libusb_has_capability (LIBUSB_CAP_HAS_HOTPLUG)) {
        error("Hotplug capabilities are not supported on this platform");
        libusb_exit(nullptr);
        return EXIT_FAILURE;
    }

    rc = libusb_hotplug_register_callback (nullptr, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, 0, vendor_id,
            product_id, class_id, hotplug_callback, nullptr, &hp[0]);
    if (LIBUSB_SUCCESS != rc) {
        error("Error registering callback 0");
        libusb_exit(nullptr);
        return EXIT_FAILURE;
    }

    info("Listening");

    while (!exit_flag) {
        // This blocks waiting for a USB event (or SIGINT/SIGTERM)
        rc = libusb_handle_events(nullptr);

        if (!exit_flag) { // Don't show error on signal interupt
            if (LIBUSB_SUCCESS != rc)
                error("libusb_handle_events() failed: {}", libusb_strerror((enum libusb_error)rc));
        }
    }

    libusb_exit(nullptr);

    info("Terminating");

    return EXIT_SUCCESS;
}
