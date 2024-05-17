#include <iostream>
#include <string>

#include <unistd.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include <hidapi.h>
#include <cxxopts.hpp>

#include "reg.h"
#include "utf8util.h"

using namespace std;
using namespace fmt;
using namespace spdlog;

static const size_t buf_size{256};
static unsigned char buf[buf_size];

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

// The keyboard will process \ as an escape character
string escape(string str) {
    stringstream ss;
    for (size_t i=0; i<str.length(); ++i) {
        char c = str[i];
        if (c == '\\') {
            ss << "\\\\";
        } else {
            ss << c;
        }
    }
    return ss.str();
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
    bool raw;
    int vendor_id{0};
    int product_id{0};

    options.add_options()
        ("h,help", "displays help text")
        ("k,key", "specifies register", cxxopts::value(key)->default_value(""))
        ("r,raw", "Escapes \\ characters", cxxopts::value(raw))
        ("v,vendor", "specifies vendor id", cxxopts::value(vendor_id))
        ("p,product", "specifies product id", cxxopts::value(product_id))
        ;

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        cout << options.help() << endl;
        return 0;
    }

    string data = "";

    vector args = result.unmatched();
    if (args.size() > 0) {
        // Take all (unmatched) arguments and append them to make the data
        bool first = true;
        std::stringstream ss;
        for (const string &arg : args) {
            if (!first) {
                ss << " ";
            }
            ss << arg;
        }
        data = ss.str();
    } else {
        if (!isatty(fileno(stdin))) {
            data = read_all(stdin);
        } else {
            cout << "Input what you'd like to copy to the Keyboard ->";
            std::getline(std::cin, data);
        }
    }

    if (raw) {
        cout << "Escaping data (" << data << ")" << endl;
        data = escape(data);
        cout << "Data: " << data << endl;
    }

    hid_version_check();

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
            set_key(raw_dev, key);
        }

        store_data(raw_dev, data);

        hid_close(raw_dev);
    } else {
        exit_status = -101;
    }

    /* Free static HIDAPI objects. */
    hid_exit();

    return exit_status;
}
