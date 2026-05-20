#pragma once

namespace WebUI {
    class USBNetwork {
    public:
        bool begin();
        bool active() const;
    };

    extern USBNetwork usb_network;
}
