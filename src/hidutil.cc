#include "hidutil.h"

using namespace std;

#define MAX_STR 255
wchar_t wstr[MAX_STR];

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
