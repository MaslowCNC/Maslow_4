#include "Debug.h"
#include "Display.h"

#include <functional>

namespace {
    char     line[300];
    size_t   lineLen     = 0;
    int      injX        = 0;
    int      injY        = 0;
    uint32_t injUntil    = 0;
    bool     injActive   = false;

    std::function<void(const char*)> extraHandler;

    void handleLine(const char* cmd) {
        if (!strcmp(cmd, "shot")) {
            Debug::dumpScreen();
        } else if (!strncmp(cmd, "tap ", 4) || !strncmp(cmd, "hold ", 5)) {
            bool hold = cmd[0] == 'h';
            int  x = 0, y = 0, ms = hold ? 1500 : 120;
            int  n = sscanf(cmd + (hold ? 5 : 4), "%d %d %d", &x, &y, &ms);
            if (n >= 2) {
                injX      = x;
                injY      = y;
                injUntil  = millis() + ms;
                injActive = true;
                DLOG("OK touch %d,%d for %d ms\n", x, y, ms);
            }
        } else if (!strcmp(cmd, "touchraw")) {
            uint16_t raw[2] = {};
            lcd.getTouchRaw(&raw[0], &raw[1]);
            DLOG("RAW %u %u\n", raw[0], raw[1]);
        } else if (!strcmp(cmd, "heap")) {
            DLOG("HEAP free %u min %u maxblock %u\n",
                 (unsigned)ESP.getFreeHeap(),
                 (unsigned)ESP.getMinFreeHeap(),
                 (unsigned)ESP.getMaxAllocHeap());
        } else if (extraHandler) {
            extraHandler(cmd);
        }
    }
}

namespace Debug {
    void setHandler(std::function<void(const char*)> h) { extraHandler = h; }

    void begin() { Serial.begin(115200); }

    void poll() {
        while (Serial.available()) {
            int c = Serial.read();
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line[lineLen] = 0;
                if (lineLen) {
                    handleLine(line);
                }
                lineLen = 0;
            } else if (lineLen + 1 < sizeof(line)) {
                line[lineLen++] = (char)c;
            }
        }
        if (injActive && (int32_t)(millis() - injUntil) >= 0) {
            injActive = false;
        }
    }

    bool injectedTouch(int& x, int& y) {
        if (!injActive) {
            return false;
        }
        x = injX;
        y = injY;
        return true;
    }

    // Reads the panel back row by row and prints it run-length encoded:
    //   SHOT <w> <h>
    //   R <hex color>*<count> ...   (one line per row)
    //   END
    void dumpScreen() {
        int      w = lcd.width(), h = lcd.height();
        uint16_t row[320];
        Serial.printf("SHOT %d %d\n", w, h);
        for (int y = 0; y < h; ++y) {
            lcd.readRect(0, y, w, 1, row);
            Serial.print("R");
            int x = 0;
            while (x < w) {
                uint16_t c = row[x];
                int      n = 1;
                while (x + n < w && row[x + n] == c) {
                    ++n;
                }
                Serial.printf(" %x*%x", c, n);
                x += n;
            }
            Serial.print("\n");
        }
        Serial.print("END\n");
    }
}
