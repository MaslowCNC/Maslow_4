#pragma once

namespace WebUI {
    class USBNetwork {
    public:
        USBNetwork();
        bool begin();
        bool active() const;
    };

    extern USBNetwork usb_network;
}
