`kb_reg` sends text to be stored in keyboards that have been custom programmed (via [QMK](https://qmk.fm/)) to accept them.  The keyboard can be triggerred to type back the text stored.  Instead of one clipboard like an OS, these custom programmed keyboards store text users store text in vim registers (hense the name `kb_reg`).

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

    brew install libusb hidapi tomlplusplus cxxargs spdlog

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
[[keyboards]]
vendor = 0x504b
product = 0x707c

[keys]
e = "jdoe@server.com"
n = "John Doe"
p = "(555) 555-5555"
d = """
Text followed by enter
"""
```

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

## Uninstall

Remove the LaunchAgent:

    launchctl unload -w ~/Library/LaunchAgents/com.github.cskeeters.kb_detect.plist
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
            hs.alert.show("Copied to KVM")
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

Each time a message is processed by the keyboard a 32-byte reply will be sent.  `kb_reg` checks for 'O', 'K', \0, \0, ... The keyboard may responsd with "Overflow" if the keyboard has not space to store the register.

An example message that sets **n** to the current register might be 'K', 'n', followed by 30 unused bytes.

To set the **n** register, then send 'S' followed by the first 31 bytes of text to store.  The keyboard stores all the data sent in the packet.  If the data to be stored is less than 31 bytes long, the remainder is filled with zeros.  When the keyboard types the data stored, it will stop at the first 0 encountered.  It works like a null-terminated string in the *C* programming language.

If the data to be stored is more than 31 bytes, additional messages are sent with the 'A' message ID until all the data is sent.

The keyboard can use different methods to store the data.  Keyboards may need to limit the number of registers supported or the total amount of text that can be stored in each register given the memory limitations of the microprocessor.
