This is QMK's [Dynamic Macros](https://docs.qmk.fm/#/feature_dynamic_macros), with the following improvements:
1. Supports more than two macros.  Roughly one for each key you can press on your keyboard.
2. Users don't have to record the macro.  The macro can be loaded with a command line tool.  This enables loading a macro from the contents of the clipboard via [pbpaste](https://ss64.com/mac/pbpaste.html).
3. Macros can be loaded from the computer automatically when the keyboard is [hot-plugged](https://libusb.sourceforge.io/api-1.0/libusb_hotplug.html).

# Why?

This can be useful if you're trying to copy text from your workstation and paste it into a remote desktop session.  Many remote desktops prevent you from using global keyboard shortcut keys configured in the host workstation.  Also, pasting in the remote desktop rightly, should paste from the remote desktop's clipboard and not the host workstation's.

This also may work where the keyboard remains powered when switch between two computers with a KVM.  I have not tested this and result may vary with the particular KVM.

This can also be useful even when working on one computer for the following reasons.
1. It provides more registers than the OS provides clipboards.
2. Using tools like Alfred's snippets don't always trigger in vim, or may trigger when you don't want them to.
3. Some websites don't allow paste in certain fields.  If you've got a long email address that you don't want to type twice, this can enhance user experience.


# Commands

This git repo contains the source code required to build `kb_reg` and `kb_detect`.

`kb_reg` sends text to be stored in keyboards that have been [custom programmed](#qmk-code) (via [QMK](https://qmk.fm/)) to accept them.  The keyboard can be triggered to type back the text stored.  Instead of one clipboard like an OS, these custom programmed keyboards store text users store text in vim registers (hence the name `kb_reg`).  Data is stored in volatile memory.

`kb_detect` detects when a keyboard attaches and can initialize multiple registers in the keyboard with text.

# Usage

This sends the text "John Doe" to the keyboard to be associated with the <kbd>n</kbd> key.

    kb_reg -k n "John Doe"

This sends the text currently in the OS's clipboard to the keyboard to be associated with the <kbd>x</kbd> key.

    pbpaste | kb_reg -k x

The text can be re-typed by the keyboard, but how to do that will depend on your keymap.c file.

# Installing

WARN: This has only been tested on macOS.

## macOS

Installation requires

 1. The installation of dependancies. (They are small and easily removed)
 2. The program must be compiled via source.

Install the small libraries first:

    brew install libusb hidapi tomlplusplus cxxargs spdlog fmt

NOTE: This program also depends on [UTF8-CPP](https://github.com/nemtrif/utfcpp), but since it's a header-only library, I just included it in the source code.

    git clone https://github.com/cskeeters/kb_reg
    cd kb_reg
    make

Install `kb_reg` and `kb_detect` in `/usr/local/bin`.

    sudo make install

Create folder for log files

    mkdir -p ~/.local/log

Create `.kb_detect.toml`

```toml
# If you install to a custom location, you need to update the path to kb_reg here.
# Otherwise kb_detect will default to /usr/local/bin/kb_reg
# kb_reg_path = "/usr/bin/kb_reg"

# Slice65 from Pizzakeyboards
[[keyboards]]
vendor = 0x504b
product = 0x707c

# kbd67mkiirgb v3 from KBDfans
[[keyboards]]
vendor = 0x4b42
product = 0x1226

# nemui from Bachoo
[[keyboards]]
vendor = 0x6400
product = 0x2371

[keys]
e = "jdoe@server.com"
n = "John Doe"
p = "(555) 555-5555"
d = """
Text followed by enter
"""
```

If you don't know your keyboard's vendor and product ids, you can plugin the keyboard while `qmk console` is running.  You can also check the log of `~/.local/log/kb_detect.log` while kb_detect is running.

### LaunchAgent

You can start `kb_detect` when you log in by adding a LaunchAgent.  Create the file `~/Library/LaunchAgents/com.github.cskeeters.kb_detect.plist`.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.github/cskeeters/kb_detect</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/kb_detect</string>
    </array>
    <key>KeepAlive</key>
    <true/>
    <key>RunAtLoad</key>
    <true/>
</dict>
</plist>
```

Then run:

    launchctl load -w ~/Library/LaunchAgents/com.github.cskeeters.kb_detect.plist

Stop `kb_detect`

    launchctl unload ~/Library/LaunchAgents/com.github.cskeeters.kb_detect.plist

#### Troubleshooting

Determine if the program is running

```sh
ps aux | grep kb_detect
```

If it's not running, try to run it manually.

```sh
/usr/local/bin/kb_detect
```

It could be that `kb_detect` requires a version of a library no longer installed on the system.  If so, recompile and reinstall.

If it runs, press `Ctrl+c` and try to start it manually.

When the program can't run due to a non-existent dependency, `launchctl load` will output the following, which is not terribly helpful.

> Load failed: 5: Input/output error


## Uninstall

Remove the LaunchAgent:

    launchctl unload ~/Library/LaunchAgents/com.github.cskeeters.kb_detect.plist
    rm -f ~/Library/LaunchAgents/com.github.cskeeters.kb_detect.plist

You can use make to remove the code:

    sudo make uninstall

Or, you can uninstall manually.

    rm -f /usr/local/bin/kb_reg
    rm -f /usr/local/bin/kb_detect

# Hammarspoon

This enables a global hotkey to copy the clipboard into the currently selected register.

    hs.hotkey.bind({"cmd", "alt", "ctrl"}, "C", function()
        local output, status, type, rc = hs.execute([[pbpaste | /usr/local/bin/kb_reg]])
        if rc == 0 then
            hs.alert.show("Copied to KB")
        else
            hs.alert.show("Error occurred.  Check ~/.local/log/kb_reg.log")
        end
    end)

Since the `-k` flag is not passed to `kb_reg`, the last set key will continue to be used.  You can specify it in the command here to always use the same key, or you can program your keyboard to switch the currently selected register with key presses.

# Debugging

You can set the environment variable `SPDLOG_LEVEL` to "DEBUG" in order to see what keyboard it's finding.

    SPDLOG_LEVEL=DEBUG kb_reg

# How it Works

Data is sent via the [Raw HID](https://docs.qmk.fm/#/feature_rawhid) available in QMK.

> Because the HID specification does not support variable length reports, all reports in both directions must be exactly RAW_EPSIZE (currently 32) bytes long, regardless of actual payload length. However, variable length payloads can potentially be implemented on top of this by creating your own data structure that may span multiple reports.

I have developed a datastructure to send text larger than 32 bytes to the keyboard.  Essentially, the first byte is a message id and then the payload follows.

| Message ID | Payload
|-----------:|:----------------------------------------
|          K | ASCII value of key
|          S | Set register (first message)
|          A | Append register (subsequent message)
|          F | Store data (Finish)

Each time a message is processed by the keyboard a 32-byte reply will be sent.  `kb_reg` checks for 'O', 'K', \0, \0, ... The keyboard may responsd with "Overflow" if the keyboard has not space to store the register.

An example message that sets **n** to the current register might be 'K', 'n', followed by 30 unused bytes.

To set the current register, send 'S' followed by the first 31 bytes of text to store.  The keyboard stores all the data sent in the packet.  If the data to be stored is less than 31 bytes long, the remainder is filled with zeros.  When the keyboard types the data stored, it will stop at the first 0 encountered.  It works like a null-terminated string in the *C* programming language.

If the data to be stored is more than 31 bytes, additional messages are sent with the 'A' message ID until all the data is sent.

The keyboard can use different methods to store the data.  Keyboards may need to limit the number of registers supported or the total amount of text that can be stored in each register given the memory limitations of the microprocessor.

# QMK Code

There are many ways to implement the code that runs on the keyboard to take the text and store it into registers.  Different keyboards use different microcontrollers and each one has different memory capacities.  You may want to customize your implementation so that it works optimally for your use case and microcontroller.

[My implementation](https://github.com/cskeeters/qmk_firmware_slice65/blob/cskeeters/keyboards/pizzakeyboards/slice65/keymaps/cskeeters/keymap.c) writes incoming data to a static global array, then upon a finish message, allocates just enough memory for the text and stores that in a linked list.  This makes good use of the available memory and enables the storage of many registers.

In my setup, registers can be over-written.  In this case, the old data is freed.  Currently, there is no way to remove a register completely other than unplugging and re-plugging in the keyboard.

## Implementation Detail

stdlib is required for `malloc` and `free`.

```c
#include <stdlib.h>
```

Here we have the data structures where the registers will be stored.

```c
#define KB_REGISTER_BUFFER_MAX 8192

struct Register
{
    // The register is looked up by this keycode
    // It is the keycode the keyboard issues to process_record_user when keys are pressed (not ASCII)
    uint16_t keycode;
    uint8_t *data;    // This will point to a string of ASCII data to be played back when keycode is triggered
    struct Register *next; // Linked List requirement
};

typedef struct Register Register;

// Single linked list of register data
Register *register_head = NULL;

// The currently selected register (by keycode)
char kb_register_next_keycode;

// This is where we write register data until F message is received
char kb_register_buffer[KB_REGISTER_BUFFER_MAX];
int  kb_register_buffer_offset;
```

`raw_hid_receive` gets called when the computer sends a message.  The following code sets the current register and stores data in the registers.

```c
// Create a new node, append it into the linked list, and initialize node->next to NULL.
Register *init_new_register(void)
{
    Register *node = (Register *) malloc(sizeof(Register));
    if (node == NULL) {
        dprintf("Error allocating memory for register\n");
        return NULL;
    }
    dprintf("Allocated memory for node\n");

    // Make sure the new node's next member doesn't point to anything
    node->next = NULL;

    if (register_head == NULL) {
        register_head = node;
        return node;
    }

    Register *prev = register_head;
    Register *cur = register_head->next;
    while (cur != NULL) {
        prev = cur;
        cur = cur->next;
    }
    prev->next = node;
    return node;
}

// Looks up a register in the linked list by keycode
Register *get_register(uint16_t keycode)
{
    Register *r = register_head;
    while (r != NULL) {
        if (r->keycode == keycode) {
            return r;
        }
        r = r->next;
    }
    // Did not find register for this keycode
    return NULL;
}

// Facilitate simple responses from the keyboard back to the computer
void send_raw_hid_response(char *msg, uint8_t length)
{
    uint8_t response[length];
    strcpy((char *)response, msg);
    raw_hid_send(response, length);
}

void raw_hid_receive(uint8_t *data, uint8_t length)
{
    dprintf("DEBUG: raw_hid_receive message: %c\n", data[0]);

    if (data[0] == 'K') { // Set Key
        // use QMK's LUT to translate ASCII to keycode
        uint8_t keycode = pgm_read_byte(&ascii_to_keycode_lut[(uint8_t)data[1]]);
        kb_register_next_keycode = keycode;
        dprintf("Set kb_register_next_keycode to %04X\n", kb_register_next_keycode);
        send_raw_hid_response("OK", length);
        return;

    } else if (data[0] == 'S') { // Initial set
        // Reinitialize kb_register to wipe out all appended data (past length)
        memset(kb_register_buffer, 0, KB_REGISTER_BUFFER_MAX);
        kb_register_buffer_offset = 0;

        // Copy over data, incrementing kb_register_buffer_offset for each byte written
        for (int i=1; i<length; i++) {
            if (data[i] == 0) {
                break;
            }
            kb_register_buffer[kb_register_buffer_offset++] = data[i];
        }

        send_raw_hid_response("OK", length);
        return;

    } else if (data[0] == 'A') { // Append

        // Copy over data, incrementing kb_register_buffer_offset for each byte written
        for (int i=1; i<length; i++) {
            if (kb_register_buffer_offset < KB_REGISTER_BUFFER_MAX) {
                if (data[i] == 0) {
                    break;
                }
                kb_register_buffer[kb_register_buffer_offset++] = data[i];
            } else {
                send_raw_hid_response("Overflow", length);
                return;
            }
        }

        send_raw_hid_response("OK", length);
        return;

    } else if (data[0] == 'F') { // Finish (Store written register)
        data = malloc(kb_register_buffer_offset+1); // add for one zero
        if (data == NULL) {
            send_raw_hid_response("Out of Memory", length);
            return;
        }
        dprintf("Allocated memory for text\n");

        Register *node = get_register(kb_register_next_keycode);
        if (node == NULL) {
            node = init_new_register();
            if (node == NULL) {
                send_raw_hid_response("Out of Memory", length);
                return;
            }
        } else {
            // Free existing node's data
            free(node->data);
        }

        node->keycode = kb_register_next_keycode;
        node->data = data;
        // Next initialized by init_new_register

        // Copy data with one zero
        memcpy(node->data, kb_register_buffer, kb_register_buffer_offset+1);

        send_raw_hid_response("OK", length);
        return;
    }

    dprintf("Unknown instruction: 0x%02x", data[0]);
}
```

To trigger playback, I use `process_record_user` as documented in [custom quantum functions](https://docs.qmk.fm/#/custom_quantum_functions?id=programming-the-behavior-of-any-keycode).  Rather than make a keycode for each register, I repurpose KC_RIGHT_GUI and then process the keycodes directly.

```c
bool process_record_user(uint16_t keycode, keyrecord_t *record)
{
    // the pressing and releasing of RGUI itself needs to be handled for get_mod to work ok
    if (keycode == KC_RGUI) {
        rgui_pressed = record->event.pressed;

        // ignore all events associated with the right gui key itself.
        return false;
    }

    if (rgui_pressed) {

        if (!record->event.pressed) {

            if ((get_mods() & MOD_BIT(KC_RSFT)) != 0) {
                kb_register_next_keycode = keycode;
                dprintf("Set kb_register_next_keycode to %04X\n", kb_register_next_keycode);
            } else {
                Register *node = get_register(keycode);
                if (node == NULL) {
                    dprintf("No register found for keycode %02X.\n", keycode);
                    register_code(KC_LEFT_GUI);
                    tap_code(keycode);
                    unregister_code(KC_LEFT_GUI);
                } else {
                    dprintf("Sending data for register for keycode %02X.\n", keycode);
                    dprintf("Sending: %s\n", node->data);
                    SEND_STRING((char*)node->data);
                }
            }
        }
        // ignore all events when right gui is currently pressed
        return false;
    }

    ...

}
```
