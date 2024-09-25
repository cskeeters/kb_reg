#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

#include <unistd.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <libusb.h>
#include <toml++/toml.hpp>

#include "utf8util.h"
#include "reg.h"

using namespace std;
using namespace std::filesystem;
using namespace fmt;
using namespace spdlog;

volatile bool exit_flag{false};

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
    return fmt::format("{}/.local/log/kb_detect.log", getenv("HOME"));
}

void configure_keyboard(toml::table &tbl, struct libusb_device_descriptor &desc) {
    if (hid_init()) {
        error("Could not initialize hid");
        return;
    }

    hid_device *raw_dev = open_raw(desc.idVendor, desc.idProduct);

    if (!raw_dev) {
        error("Unable to find raw interface to device {:04x}:{:04x}", desc.idVendor, desc.idProduct);

        /* Free static HIDAPI objects. */
        hid_exit();
        return;
    }

    std::string vendor = u8enc(get_vendor(raw_dev));
    std::string product = u8enc(get_product(raw_dev));

    hid_set_nonblocking(raw_dev, 1);

    auto keys = tbl["keys"].as_table();
    for (auto pair : *keys) {
        string key = string(pair.first.str());
        string data = pair.second.value_or(""s);

        set_key(raw_dev, key);
        store_data(raw_dev, data);
    }

    set_key(raw_dev, ".");

    hid_close(raw_dev);

    /* Free static HIDAPI objects. */
    hid_exit();

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
            configure_keyboard(tbl, desc);
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
        configure_keyboard(tbl, desc);
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
        cout << fmt::format("You must run: mkdir -p {}", log_folder_path) << endl;
        return 1;
    }

    if (!isatty(fileno(stdout))) {
        // configure spdlog to write to this logfile
        string logpath = get_log_path();
        std::freopen(logpath.c_str(), "w", stdout);
    }

    spdlog::cfg::load_env_levels();

    debug("Registering Signal Handlers");
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    debug("Checking HID Version");
    hid_version_check();

    libusb_hotplug_callback_handle hp[2];
    int product_id{0}, vendor_id{0}, class_id{0};

    vendor_id  = (argc > 1) ? (int)strtol (argv[1], nullptr, 0) : LIBUSB_HOTPLUG_MATCH_ANY;
    product_id = (argc > 2) ? (int)strtol (argv[2], nullptr, 0) : LIBUSB_HOTPLUG_MATCH_ANY;

    debug("Initializing USB Library");
    libusb_context *usb_ctx;
    // Eventually, this needs to be replaced with libusb_init_context
    int rc = libusb_init(&usb_ctx);
    if (LIBUSB_SUCCESS != rc)
    {
        error("failed to initialise libusb: {}", libusb_strerror((enum libusb_error)rc));
        return EXIT_FAILURE;
    }

    debug("Configuring currently attached keyboards");
    configure_if_connected(usb_ctx);

    debug("Starting USB Listener");
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
