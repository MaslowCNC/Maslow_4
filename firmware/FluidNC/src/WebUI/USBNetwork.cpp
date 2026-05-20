#include "USBNetwork.h"

#include "../Config.h"

#if ARDUINO_USB_MODE == 1 && ARDUINO_USB_CDC_ON_BOOT

#    include "esp32-hal-tinyusb.h"
#    include "esp_mac.h"
#    include "USB.h"
#    include <cstring>

extern "C" {
#    include "lwip/netif.h"
#    include "lwip/pbuf.h"
#    include "lwip/etharp.h"
#    include "lwip/ethip6.h"
#    include "netif/ethernet.h"
#    include "class/net/net_device.h"
#    include "device/usbd_pvt.h"
}

namespace WebUI {
    USBNetwork usb_network;

    static bool         s_active           = false;
    static bool         s_interfaceEnabled = false;
    static struct netif s_netif;

    extern "C" {
        // 0x02 in the first octet marks this as a locally administered unicast MAC.
        uint8_t tud_network_mac_address[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    }

    static uint8_t s_ep_data_num  = 0;
    static uint8_t s_ep_notif_num = 0;

    static uint16_t load_usb_net_descriptor(uint8_t* dst, uint8_t* itf);

    struct USBNetInterfaceRegistrar {
        USBNetInterfaceRegistrar() {
            s_ep_data_num  = tinyusb_get_free_duplex_endpoint();
            s_ep_notif_num = tinyusb_get_free_in_endpoint();
            if (s_ep_data_num == 0 || s_ep_notif_num == 0) {
                return;
            }
            s_interfaceEnabled = (tinyusb_enable_interface(USB_INTERFACE_CUSTOM, TUD_CDC_ECM_DESC_LEN, load_usb_net_descriptor) == ESP_OK);
        }
    };

    static USBNetInterfaceRegistrar usbNetInterfaceRegistrar;

    static err_t usbnet_linkoutput(struct netif* netif, struct pbuf* p) {
        (void)netif;
        if (!tud_ready()) {
            return ERR_IF;
        }
        if (!tud_network_can_xmit(p->tot_len)) {
            return ERR_WOULDBLOCK;
        }
        tud_network_xmit(p, 0);
        return ERR_OK;
    }

    static err_t usbnet_output(struct netif* netif, struct pbuf* p, const ip4_addr_t* addr) {
        return etharp_output(netif, p, addr);
    }

#    if LWIP_IPV6
    static err_t usbnet_output_ip6(struct netif* netif, struct pbuf* p, const ip6_addr_t* addr) {
        return ethip6_output(netif, p, addr);
    }
#    endif

    static err_t usbnet_netif_init(struct netif* netif) {
        netif->hwaddr_len = 6;
        memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
        netif->mtu        = CFG_TUD_NET_MTU;
        netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
        netif->name[0]    = 'U';
        netif->name[1]    = 'N';
        netif->linkoutput = usbnet_linkoutput;
        netif->output     = usbnet_output;
#    if LWIP_IPV6
        netif->output_ip6 = usbnet_output_ip6;
#    endif
        return ERR_OK;
    }

    static uint16_t load_usb_net_descriptor(uint8_t* dst, uint8_t* itf) {
        if (s_ep_data_num == 0 || s_ep_notif_num == 0) {
            return 0;
        }

        uint8_t stridx_desc = tinyusb_add_string_descriptor("Maslow USB Network");
        uint8_t stridx_mac  = tinyusb_add_string_descriptor("020000000001");

        const uint8_t ep_notif = uint8_t(0x80 | s_ep_notif_num);
        const uint8_t ep_out   = s_ep_data_num;
        const uint8_t ep_in    = uint8_t(0x80 | s_ep_data_num);

        uint8_t descriptor[TUD_CDC_ECM_DESC_LEN] = {
            TUD_CDC_ECM_DESCRIPTOR(*itf, stridx_desc, stridx_mac, ep_notif, 64, ep_out, ep_in, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)
        };
        *itf += 2;
        memcpy(dst, descriptor, sizeof(descriptor));
        return TUD_CDC_ECM_DESC_LEN;
    }

    bool USBNetwork::begin() {
        if (s_active) {
            return true;
        }

        if (!s_interfaceEnabled) {
            return false;
        }

        uint8_t baseMac[6];
        if (esp_read_mac(baseMac, ESP_MAC_WIFI_STA) == ESP_OK) {
            tud_network_mac_address[0] = (baseMac[0] & 0xFEU) | 0x02U;
            tud_network_mac_address[1] = baseMac[1];
            tud_network_mac_address[2] = baseMac[2];
            tud_network_mac_address[3] = baseMac[3];
            tud_network_mac_address[4] = baseMac[4];
            tud_network_mac_address[5] = baseMac[5] ^ 0x01U;
        }

        ip4_addr_t ip;
        ip4_addr_t netmask;
        ip4_addr_t gateway;
        IP4_ADDR(&ip, 192, 168, 7, 1);
        IP4_ADDR(&netmask, 255, 255, 255, 0);
        IP4_ADDR(&gateway, 0, 0, 0, 0);

        if (netif_add(&s_netif, &ip, &netmask, &gateway, nullptr, usbnet_netif_init, ethernet_input) == nullptr) {
            log_error("USB network netif init failed");
            return false;
        }
        netif_set_default(&s_netif);
        netif_set_up(&s_netif);
        netif_set_link_up(&s_netif);
#    if LWIP_IPV6
        netif_create_ip6_linklocal_address(&s_netif, 1);
#    endif
        s_active = true;
        log_info("USB network active at 192.168.7.1");
        return true;
    }

    bool USBNetwork::active() const {
        return s_active;
    }

    // TinyUSB network callbacks
    extern "C" bool tud_network_recv_cb(const uint8_t* src, uint16_t size) {
        if (!s_active || size == 0) {
            tud_network_recv_renew();
            return true;
        }

        struct pbuf* p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p == nullptr) {
            tud_network_recv_renew();
            return true;
        }

        pbuf_take(p, src, size);
        if (s_netif.input(p, &s_netif) != ERR_OK) {
            pbuf_free(p);
        }
        tud_network_recv_renew();
        return true;
    }

    extern "C" uint16_t tud_network_xmit_cb(uint8_t* dst, void* ref, uint16_t arg) {
        (void)arg;
        struct pbuf* p = static_cast<struct pbuf*>(ref);
        return p == nullptr ? 0 : pbuf_copy_partial(p, dst, p->tot_len, 0);
    }

    extern "C" void tud_network_init_cb(void) {}

    // ECM only class driver derived from TinyUSB v0.16.0 network class driver.
    typedef struct {
        uint8_t itf_num;
        uint8_t itf_data_alt;
        uint8_t ep_notif;
        uint8_t ep_in;
        uint8_t ep_out;
        bool    ecm_mode;
        const uint8_t* ecm_desc_epdata;
    } netd_interface_t;

    struct ecm_notify_struct {
        tusb_control_request_t header;
        uint32_t               downlink;
        uint32_t               uplink;
    };

    CFG_TUD_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_netd_rx[CFG_TUD_NET_MTU + 64];
    CFG_TUD_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_netd_tx[CFG_TUD_NET_MTU + 64];
    CFG_TUD_MEM_SECTION CFG_TUSB_MEM_ALIGN static ecm_notify_struct s_notify;
    static netd_interface_t s_netd_itf;
    static bool             s_can_xmit = false;

    static void netd_do_in_xfer(uint8_t* buf, uint16_t len) {
        s_can_xmit = false;
        usbd_edpt_xfer(0, s_netd_itf.ep_in, buf, len);
    }

    extern "C" void netd_report(uint8_t* buf, uint16_t len) {
        uint8_t const rhport = 0;
        if (usbd_edpt_busy(rhport, s_netd_itf.ep_notif)) {
            return;
        }
        usbd_edpt_xfer(rhport, s_netd_itf.ep_notif, buf, len);
    }

    extern "C" void netd_init(void) {
        memset(&s_netd_itf, 0, sizeof(s_netd_itf));
        s_can_xmit = false;
    }

    extern "C" void netd_reset(uint8_t rhport) {
        (void)rhport;
        netd_init();
    }

    extern "C" uint16_t netd_open(uint8_t rhport, const tusb_desc_interface_t* itf_desc, uint16_t max_len) {
        bool const is_ecm = (TUSB_CLASS_CDC == itf_desc->bInterfaceClass && CDC_COMM_SUBCLASS_ETHERNET_CONTROL_MODEL == itf_desc->bInterfaceSubClass
                             && 0x00 == itf_desc->bInterfaceProtocol);

        TU_VERIFY(is_ecm, 0);
        TU_ASSERT(0 == s_netd_itf.ep_notif, 0);

        s_netd_itf.ecm_mode = true;
        s_netd_itf.itf_num  = itf_desc->bInterfaceNumber;

        uint16_t       drv_len = sizeof(tusb_desc_interface_t);
        const uint8_t* p_desc  = tu_desc_next(itf_desc);

        while (TUSB_DESC_CS_INTERFACE == tu_desc_type(p_desc) && drv_len <= max_len) {
            drv_len += tu_desc_len(p_desc);
            p_desc = tu_desc_next(p_desc);
        }

        if (TUSB_DESC_ENDPOINT == tu_desc_type(p_desc)) {
            TU_ASSERT(usbd_edpt_open(rhport, (const tusb_desc_endpoint_t*)p_desc), 0);
            s_netd_itf.ep_notif = ((const tusb_desc_endpoint_t*)p_desc)->bEndpointAddress;
            drv_len += tu_desc_len(p_desc);
            p_desc = tu_desc_next(p_desc);
        }

        TU_ASSERT(TUSB_DESC_INTERFACE == tu_desc_type(p_desc), 0);
        do {
            const tusb_desc_interface_t* data_itf_desc = (const tusb_desc_interface_t*)p_desc;
            TU_ASSERT(TUSB_CLASS_CDC_DATA == data_itf_desc->bInterfaceClass, 0);
            drv_len += tu_desc_len(p_desc);
            p_desc = tu_desc_next(p_desc);
        } while ((TUSB_DESC_INTERFACE == tu_desc_type(p_desc)) && (drv_len <= max_len));

        TU_ASSERT(TUSB_DESC_ENDPOINT == tu_desc_type(p_desc), 0);
        s_netd_itf.ecm_desc_epdata = p_desc;
        drv_len += 2 * sizeof(tusb_desc_endpoint_t);
        return drv_len;
    }

    static void ecm_report(bool nc) {
        memset(&s_notify, 0, sizeof(s_notify));
        s_notify.header.bmRequestType = 0xA1;
        s_notify.header.wIndex        = s_netd_itf.itf_num;
        if (nc) {
            s_notify.header.bRequest = 0;
            s_notify.header.wValue   = 1;
            s_notify.header.wLength  = 0;
            netd_report((uint8_t*)&s_notify, sizeof(s_notify.header));
        } else {
            s_notify.header.bRequest = 0x2A;
            s_notify.header.wLength  = 8;
            // CDC-ECM speed notification is reported in bits-per-second.
            s_notify.downlink        = 9728000;
            s_notify.uplink          = 9728000;
            netd_report((uint8_t*)&s_notify, sizeof(s_notify));
        }
    }

    extern "C" bool netd_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t* request) {
        if (stage == CONTROL_STAGE_SETUP) {
            switch (request->bmRequestType_bit.type) {
                case TUSB_REQ_TYPE_STANDARD:
                    switch (request->bRequest) {
                        case TUSB_REQ_GET_INTERFACE: {
                            uint8_t const req_itfnum = (uint8_t)request->wIndex;
                            TU_VERIFY(s_netd_itf.itf_num + 1 == req_itfnum);
                            tud_control_xfer(rhport, request, &s_netd_itf.itf_data_alt, 1);
                        } break;
                        case TUSB_REQ_SET_INTERFACE: {
                            uint8_t const req_itfnum = (uint8_t)request->wIndex;
                            uint8_t const req_alt    = (uint8_t)request->wValue;
                            TU_VERIFY(s_netd_itf.itf_num + 1 == req_itfnum && req_alt < 2);
                            s_netd_itf.itf_data_alt = req_alt;
                            if (s_netd_itf.itf_data_alt) {
                                if (s_netd_itf.ep_in == 0 && s_netd_itf.ep_out == 0) {
                                    TU_ASSERT(s_netd_itf.ecm_desc_epdata);
                                    TU_ASSERT(usbd_open_edpt_pair(
                                        rhport, s_netd_itf.ecm_desc_epdata, 2, TUSB_XFER_BULK, &s_netd_itf.ep_out, &s_netd_itf.ep_in));
                                    tud_network_init_cb();
                                    s_can_xmit = true;
                                    tud_network_recv_renew();
                                }
                            }
                            tud_control_status(rhport, request);
                        } break;
                        default:
                            return false;
                    }
                    break;
                case TUSB_REQ_TYPE_CLASS:
                    TU_VERIFY(s_netd_itf.itf_num == request->wIndex);
                    if (0x43 == request->bRequest) {
                        tud_control_xfer(rhport, request, nullptr, 0);
                        ecm_report(true);
                    }
                    break;
                default:
                    return false;
            }
        }
        return true;
    }

    extern "C" void tud_network_recv_renew(void) {
        usbd_edpt_xfer(0, s_netd_itf.ep_out, s_netd_rx, sizeof(s_netd_rx));
    }

    extern "C" bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
        (void)rhport;
        (void)result;

        if (ep_addr == s_netd_itf.ep_out) {
            if (!tud_network_recv_cb(s_netd_rx, (uint16_t)xferred_bytes)) {
                tud_network_recv_renew();
            }
        }

        if (ep_addr == s_netd_itf.ep_in) {
            if (xferred_bytes && (0 == (xferred_bytes % CFG_TUD_NET_ENDPOINT_SIZE))) {
                netd_do_in_xfer(nullptr, 0);
            } else {
                s_can_xmit = true;
            }
        }

        if (ep_addr == s_netd_itf.ep_notif) {
            if (sizeof(s_notify.header) == xferred_bytes) {
                ecm_report(false);
            }
        }

        return true;
    }

    extern "C" bool tud_network_can_xmit(uint16_t size) {
        (void)size;
        return s_can_xmit;
    }

    extern "C" void tud_network_xmit(void* ref, uint16_t arg) {
        if (!s_can_xmit) {
            return;
        }
        uint16_t len = tud_network_xmit_cb(s_netd_tx, ref, arg);
        netd_do_in_xfer(s_netd_tx, len);
    }

    extern "C" void tud_network_link_state_cb(bool state) {
        if (state) {
            netif_set_link_up(&s_netif);
        } else {
            netif_set_link_down(&s_netif);
        }
    }

    extern "C" const usbd_class_driver_t* usbd_app_driver_get_cb(uint8_t* driver_count) {
        static usbd_class_driver_t net_driver;
        memset(&net_driver, 0, sizeof(net_driver));
        net_driver.init            = netd_init;
        net_driver.reset           = netd_reset;
        net_driver.open            = netd_open;
        net_driver.control_xfer_cb = netd_control_xfer_cb;
        net_driver.xfer_cb         = netd_xfer_cb;
        *driver_count              = 1;
        return &net_driver;
    }
}

#else
namespace WebUI {
    USBNetwork usb_network;

    bool USBNetwork::begin() {
        return false;
    }
    bool USBNetwork::active() const {
        return false;
    }
}
#endif
