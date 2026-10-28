// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "BoardId.h"
#include "../Logging.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#ifndef ADC_ATTEN_DB_12
#    define ADC_ATTEN_DB_12 ADC_ATTEN_DB_11
#endif

#define BOARD_VERSION_PIN 17
#define BOARD_VERSION_SAMPLES 16

namespace BoardId {
    namespace {
        Board detectedBoard      = Board::Unknown;
        int   detectedMillivolts = -1;

        struct Band {
            int   minMv;
            int   maxMv;
            Board board;
        };

        // Each band is centred on the divider's nominal voltage and leaves a wide gap to the next
        const Band bands[] = {
            { 400, 800, Board::Unified36V },
            { 900, 1300, Board::Unified },
            { 1450, 1850, Board::Maslow41 },
        };

        // Average the divider voltage in mV, or -1 if the ADC cannot be read.  The ADC unit is
        // deleted again afterwards: DCMotor claims ADC2 later for the Top Left current sense.
        int readMillivolts() {
            adc_unit_t    unit;
            adc_channel_t channel;
            if (adc_oneshot_io_to_channel(BOARD_VERSION_PIN, &unit, &channel) != ESP_OK) {
                return -1;
            }

            adc_oneshot_unit_handle_t   handle  = nullptr;
            adc_oneshot_unit_init_cfg_t unitCfg = {};
            unitCfg.unit_id                     = unit;
            unitCfg.ulp_mode                    = ADC_ULP_MODE_DISABLE;
            if (adc_oneshot_new_unit(&unitCfg, &handle) != ESP_OK) {
                return -1;
            }

            int                    result  = -1;
            adc_oneshot_chan_cfg_t chanCfg = {};
            chanCfg.atten                  = ADC_ATTEN_DB_12;
            chanCfg.bitwidth               = ADC_BITWIDTH_12;
            if (adc_oneshot_config_channel(handle, channel, &chanCfg) == ESP_OK) {
                int sum   = 0;
                int count = 0;
                for (int i = 0; i < BOARD_VERSION_SAMPLES; i++) {
                    int raw;
                    if (adc_oneshot_read(handle, channel, &raw) == ESP_OK) {
                        sum += raw;
                        count++;
                    }
                }
                if (count > 0) {
                    int raw = sum / count;

                    // Convert with the eFuse calibration; without it, use the nominal 12 dB full scale
                    adc_cali_handle_t               cali    = nullptr;
                    adc_cali_curve_fitting_config_t caliCfg = {};
                    caliCfg.unit_id                         = unit;
                    caliCfg.chan                            = channel;
                    caliCfg.atten                           = ADC_ATTEN_DB_12;
                    caliCfg.bitwidth                        = ADC_BITWIDTH_12;
                    if (adc_cali_create_scheme_curve_fitting(&caliCfg, &cali) == ESP_OK) {
                        if (adc_cali_raw_to_voltage(cali, raw, &result) != ESP_OK) {
                            result = -1;
                        }
                        adc_cali_delete_scheme_curve_fitting(cali);
                    }
                    if (result < 0) {
                        result = raw * 3100 / 4095;
                    }
                }
            }
            adc_oneshot_del_unit(handle);
            return result;
        }
    }

    void detect() {
        detectedMillivolts = readMillivolts();
        detectedBoard      = Board::Unknown;
        for (const auto& band : bands) {
            if (detectedMillivolts >= band.minMv && detectedMillivolts <= band.maxMv) {
                detectedBoard = band.board;
            }
        }

        if (detectedBoard == Board::Unknown) {
            log_warn("Board: not recognised (BoardVersion " << detectedMillivolts << " mV)");
        } else {
            log_info("Board: " << boardName(detectedBoard) << " (BoardVersion " << detectedMillivolts << " mV)");
        }
    }

    Board board() {
        return detectedBoard;
    }

    int millivolts() {
        return detectedMillivolts;
    }

    const char* boardName(Board board) {
        switch (board) {
            case Board::Maslow41:
                return "Maslow 4.1";
            case Board::Unified:
                return "Unified Control Board";
            case Board::Unified36V:
                return "Unified Control Board 36V";
            default:
                return "Unknown";
        }
    }
}
