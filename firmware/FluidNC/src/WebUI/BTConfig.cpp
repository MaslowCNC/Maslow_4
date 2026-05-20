// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ENABLE_BLUETOOTH

#    include "BTConfig.h"

#    include "../Machine/MachineConfig.h"
#    include "../Report.h"  // CLIENT_*
#    include "Commands.h"   // COMMANDS
#    include "WebSettings.h"

#    include <cstdint>
#    include <deque>
#    include <cstring>

namespace WebUI {
    BTConfig  bt_config;
    BTChannel btChannel;
}

namespace {
    constexpr size_t BT_RX_BUFFER_SIZE = 512;

#    if defined(SOC_CLASSIC_BT_SUPPORTED)
    constexpr bool   kUsesBleTransport = false;
#    else
    constexpr bool   kUsesBleTransport = true;
#    endif

#    if defined(SOC_CLASSIC_BT_SUPPORTED)
    using WebUI::BTConfig;
    using WebUI::BTChannel;
    using WebUI::btChannel;

    BluetoothSerial SerialBT;

extern "C" {
    const uint8_t* esp_bt_dev_get_address(void);
}
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
    using WebUI::BTConfig;
    using WebUI::BTChannel;
    using WebUI::btChannel;

    constexpr const char* BLE_SERVICE_UUID  = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr const char* BLE_RX_UUID       = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr const char* BLE_TX_UUID       = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
    constexpr size_t      BLE_TX_CHUNK      = 180;
    constexpr size_t      BLE_ADDRESS_CHARS = 18;

    BLEServer*         bleServer           = nullptr;
    BLECharacteristic* bleTxCharacteristic = nullptr;
    BLECharacteristic* bleRxCharacteristic = nullptr;
    std::deque<uint8_t> bleRxBuffer;
    std::string         bleTxBuffer;
    portMUX_TYPE        bleBufferMux = portMUX_INITIALIZER_UNLOCKED;

    void clearBleBuffers() {
        portENTER_CRITICAL(&bleBufferMux);
        bleRxBuffer.clear();
        portEXIT_CRITICAL(&bleBufferMux);
        bleTxBuffer.clear();
    }

    bool bleClientConnected() {
        return bleServer != nullptr && bleServer->getConnectedCount() > 0;
    }

    void bleNotifyChunk(const char* data, size_t len) {
        if (!bleTxCharacteristic || !bleClientConnected() || len == 0) {
            return;
        }
        std::string payload(data, len);
        bleTxCharacteristic->setValue(reinterpret_cast<uint8_t*>(&payload[0]), payload.size());
        bleTxCharacteristic->notify();
    }

    void flushBleTxBuffer() {
        while (!bleTxBuffer.empty()) {
            const size_t chunkLen = std::min(BLE_TX_CHUNK, bleTxBuffer.size());
            bleNotifyChunk(bleTxBuffer.data(), chunkLen);
            bleTxBuffer.erase(0, chunkLen);
        }
    }

    class BleServerCallbacks : public BLEServerCallbacks {
    public:
        void onConnect(BLEServer* pServer) override {
            (void)pServer;
            log_info("BT Connected with BLE client");
        }

        void onDisconnect(BLEServer* pServer) override {
            log_info("BT Disconnected");
            if (pServer) {
                pServer->startAdvertising();
            }
        }
    };

    class BleRxCallbacks : public BLECharacteristicCallbacks {
    public:
        void onWrite(BLECharacteristic* characteristic) override {
            const std::string value = characteristic ? characteristic->getValue() : std::string();
            if (value.empty()) {
                return;
            }
            portENTER_CRITICAL(&bleBufferMux);
            for (const unsigned char c : value) {
                if (bleRxBuffer.size() < BT_RX_BUFFER_SIZE) {
                    bleRxBuffer.push_back(c);
                }
            }
            portEXIT_CRITICAL(&bleBufferMux);
        }
    };

    BleServerCallbacks bleServerCallbacks;
    BleRxCallbacks     bleRxCallbacks;
#    endif
}  // namespace

namespace WebUI {
    EnumSetting*   bt_enable;
    StringSetting* bt_name;

    BTConfig* BTConfig::instance = nullptr;

    BTConfig::BTConfig() {
        bt_enable = new EnumSetting("Bluetooth Enable", WEBSET, WA, "ESP141", "Bluetooth/Enable", 1, &onoffOptions, NULL);

        bt_name = new StringSetting("Bluetooth name",
                                    WEBSET,
                                    WA,
                                    "ESP140",
                                    "Bluetooth/Name",
                                    DEFAULT_BT_NAME,
                                    WebUI::BTConfig::MIN_BTNAME_LENGTH,
                                    WebUI::BTConfig::MAX_BTNAME_LENGTH,
                                    (bool (*)(char*))BTConfig::isBTnameValid);
    }

#    if defined(SOC_CLASSIC_BT_SUPPORTED)
    void BTConfig::my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
        auto inst = instance;
        switch (event) {
            case ESP_SPP_SRV_OPEN_EVT: {  //Server connection open
                char str[18];
                str[17]       = '\0';
                uint8_t* addr = param->srv_open.rem_bda;
                sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
                inst->_btclient = str;
                log_info("BT Connected with " << str);
            } break;
            case ESP_SPP_CLOSE_EVT:  //Client connection closed
                log_info("BT Disconnected");
                inst->_btclient = "";
                break;
            default:
                break;
        }
    }
#    endif

    size_t BTChannel::write(uint8_t data) {
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        static uint8_t lastchar = '\0';
        if (_addCR && data == '\n' && lastchar != '\r') {
            SerialBT.write('\r');
        }
        lastchar = data;
        return SerialBT.write(data);
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        static uint8_t lastchar = '\0';
        if (_addCR && data == '\n' && lastchar != '\r') {
            bleTxBuffer.push_back('\r');
        }
        lastchar = data;
        bleTxBuffer.push_back(static_cast<char>(data));
        if (data == '\n' || bleTxBuffer.size() >= BLE_TX_CHUNK) {
            flushBleTxBuffer();
        }
        return 1;
#    else
        (void)data;
        return 0;
#    endif
    }

    void BTChannel::flush() {
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        SerialBT.flush();
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        flushBleTxBuffer();
#    endif
    }

    int BTChannel::available() {
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        return SerialBT.available();
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        int available = 0;
        portENTER_CRITICAL(&bleBufferMux);
        available = bleRxBuffer.size();
        portEXIT_CRITICAL(&bleBufferMux);
        return available;
#    else
        return 0;
#    endif
    }

    int BTChannel::read() {
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        return SerialBT.read();
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        int value = -1;
        portENTER_CRITICAL(&bleBufferMux);
        if (!bleRxBuffer.empty()) {
            value = bleRxBuffer.front();
            bleRxBuffer.pop_front();
        }
        portEXIT_CRITICAL(&bleBufferMux);
        return value;
#    else
        return -1;
#    endif
    }

    int BTChannel::peek() {
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        return SerialBT.peek();
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        int value = -1;
        portENTER_CRITICAL(&bleBufferMux);
        if (!bleRxBuffer.empty()) {
            value = bleRxBuffer.front();
        }
        portEXIT_CRITICAL(&bleBufferMux);
        return value;
#    else
        return -1;
#    endif
    }

    int BTChannel::rx_buffer_available() {
        const int availableCount = available();
        return availableCount >= static_cast<int>(BT_RX_BUFFER_SIZE) ? 0 : static_cast<int>(BT_RX_BUFFER_SIZE) - availableCount;
    }

    std::string BTConfig::info() {
        std::string result;
        if (isOn()) {
            result += kUsesBleTransport ? "Mode=BLE:Name=" : "Mode=BT:Name=";
            result += _btname.c_str();
            result += "(";
            result += device_address();
            result += "):Status=";
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
            if (SerialBT.hasClient()) {
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
            if (bleClientConnected()) {
#    else
            if (false) {
#    endif
                result += "Connected with ";
                result += _btclient.empty() ? "BLE client" : _btclient.c_str();
            } else {
                result += "Not connected";
            }
        } else {
            result += "No BT";
        }
        return result;
    }

    bool BTConfig::isBTnameValid(const char* hostname) {
        if (!hostname) {
            return true;
        }
        char c;
        for (int i = 0; i < strlen(hostname); i++) {
            c = hostname[i];
            if (!(isdigit(c) || isalpha(c) || c == '_')) {
                return false;
            }
        }
        return true;
    }

    const char* BTConfig::device_address() {
#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        const uint8_t* point = esp_bt_dev_get_address();
        char*          str   = _deviceAddrBuffer;
        str[17]              = '\0';
        sprintf(
            str, "%02X:%02X:%02X:%02X:%02X:%02X", (int)point[0], (int)point[1], (int)point[2], (int)point[3], (int)point[4], (int)point[5]);
        return str;
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        const std::string address = BLEDevice::getInitialized() ? BLEDevice::getAddress().toString() : std::string();
        static_assert(BLE_ADDRESS_CHARS == sizeof(_deviceAddrBuffer), "Unexpected BLE address buffer size");
        snprintf(_deviceAddrBuffer, sizeof(_deviceAddrBuffer), "%s", address.c_str());
        return _deviceAddrBuffer;
#    else
        _deviceAddrBuffer[0] = '\0';
        return _deviceAddrBuffer;
#    endif
    }

    bool BTChannel::realtimeOkay(char c) {
        return _lineedit->realtime(c);
    }

    bool BTChannel::lineComplete(char* line, char c) {
        if (_lineedit->step(c)) {
            _linelen        = _lineedit->finish();
            _line[_linelen] = '\0';
            strcpy(line, _line);
            _linelen = 0;
            return true;
        }
        return false;
    }

    Channel* BTChannel::pollLine(char* line) {
        if (_lineedit == nullptr) {
            return nullptr;
        }
        return Channel::pollLine(line);
    }

    bool BTConfig::begin() {
        instance = this;

        log_debug("Begin Bluetooth setup");
        end();

        if (!bt_enable->get()) {
            log_info("Bluetooth not enabled");
            return false;
        }

        _btname = bt_name->getStringValue();
        if (_btname.empty()) {
            log_info("BT is not enabled");
            return false;
        }

#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        if (!SerialBT.begin(_btname.c_str())) {
            log_error("Bluetooth failed to start");
            return false;
        }
        SerialBT.register_callback(&my_spp_cb);
        _btStarted = true;
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        BLEDevice::init(_btname);
        bleServer = BLEDevice::createServer();
        if (!bleServer) {
            log_error("Bluetooth failed to start");
            return false;
        }
        bleServer->setCallbacks(&bleServerCallbacks);

        BLEService* service = bleServer->createService(BLE_SERVICE_UUID);
        bleTxCharacteristic = service->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
        bleTxCharacteristic->addDescriptor(new BLE2902());
        bleRxCharacteristic = service->createCharacteristic(BLE_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        bleRxCharacteristic->setCallbacks(&bleRxCallbacks);
        service->start();
        bleServer->getAdvertising()->addServiceUUID(BLE_SERVICE_UUID);
        bleServer->startAdvertising();
        clearBleBuffers();
        _btStarted = true;
#    else
        log_error("Bluetooth is not supported on this target");
        return false;
#    endif

        allChannels.registration(&btChannel);
        log_info("BT Started with " << _btname << (kUsesBleTransport ? " (BLE)" : ""));
        return true;
    }

    void BTConfig::end() {
        if (!_btStarted) {
            return;
        }

        allChannels.deregistration(&btChannel);

#    if defined(SOC_CLASSIC_BT_SUPPORTED)
        SerialBT.end();
#    elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)
        clearBleBuffers();
        if (bleServer) {
            bleServer->getAdvertising()->stop();
        }
        BLEDevice::deinit(false);
        bleServer           = nullptr;
        bleTxCharacteristic = nullptr;
        bleRxCharacteristic = nullptr;
#    endif

        _btclient.clear();
        _btStarted = false;
    }

    bool BTConfig::isOn() const {
        return _btStarted;
    }

    void BTConfig::handle() {}

    BTConfig::~BTConfig() {
        end();
    }
}

#endif
