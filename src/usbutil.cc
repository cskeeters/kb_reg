#include "usbutil.h"

#include <spdlog/spdlog.h>
#include "utf8util.h"

using namespace std;
using namespace spdlog;

// from libusb/libusb/descriptor.c
#define DESC_HEADER_LENGTH 2

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

