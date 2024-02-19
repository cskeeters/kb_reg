#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <filesystem>

#include <unistd.h>

#include <spdlog/spdlog.h>
#include <libusb.h>
#include <toml++/toml.hpp>

using namespace std;
using namespace std::filesystem;
using namespace fmt;
using namespace spdlog;

#define KB_REG_PATH "/usr/local/bin/kb_reg"

libusb_device_handle *handle = nullptr;

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
    return format("{}/.local/log/kb_detect.log", getenv("HOME"));
}

void run_kb_reg(const string &key, const string &value) {
    string cmd = format("printf '{}' | {} -k {}", value.c_str(), KB_REG_PATH, key);

    int ret = system(cmd.c_str());
    if (ret != 0) {
        error("Error running: {}", cmd);
    }
}

static int LIBUSB_CALL hotplug_callback(libusb_context *ctx, libusb_device *dev, libusb_hotplug_event event, void *user_data)
{
    struct libusb_device_descriptor desc;
    int rc;

    (void)ctx;
    (void)dev;
    (void)event;
    (void)user_data;

    rc = libusb_get_device_descriptor(dev, &desc);
    if (LIBUSB_SUCCESS == rc) {
        info("Device attached: {:04x}:{:04x}", desc.idVendor, desc.idProduct);

        auto tbl = toml::parse_file(get_config_path());

        if (is_custom_keyboard(tbl, desc.idVendor, desc.idProduct)) {

            auto keys = tbl["keys"].as_table();
            for (auto pair : *keys) {
                string key = string(pair.first.str());
                string value = pair.second.value_or(""s);

                run_kb_reg(key, value);
            }

            run_kb_reg(".", "");
            string cmd = format("printf '' | {} -k .", KB_REG_PATH);
            int ret = system(cmd.c_str());
            if (ret != 0) {
                error("Error running: {}", cmd);
            }
            info("Initialized keyboard");
        }

    } else {
        error("Error getting device descriptor: {}", libusb_strerror((enum libusb_error)rc));
    }

    if (handle) {
        libusb_close(handle);
        handle = nullptr;
    }

    rc = libusb_open (dev, &handle);
    if (LIBUSB_SUCCESS != rc) {
        error("No access to device: {}", libusb_strerror((enum libusb_error)rc));
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

    // Eventually, this needs to be replaced with libusb_init_context
    rc = libusb_init(nullptr);
    if (LIBUSB_SUCCESS != rc)
    {
        error("failed to initialise libusb: {}", libusb_strerror((enum libusb_error)rc));
        return EXIT_FAILURE;
    }

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

    if (handle) {
        libusb_close(handle);
    }

    libusb_exit(nullptr);

    info("Terminating");

    return EXIT_SUCCESS;
}
