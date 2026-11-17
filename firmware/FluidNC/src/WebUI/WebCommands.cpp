// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  WebCommands.cpp - Settings and Commands for the interface
  to ESP3D_WebUI.  Code snippets extracted from commands.cpp in the
  old WebUI interface code are presented via the Settings class.
*/

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include "Configuration/JsonGenerator.h"
#include "Report.h"  // git_info
#include "Driver/SysStats.h"
#include "Module.h"

#include "../Spindles/SpindleBoard.h"  // Spindles::SpindleBoard (for $Spindle/EnableOTA)

#include <strings.h>  // strcasecmp

namespace WebUI {

    class WebCommands : public Module {
    public:
        WebCommands(const char* name) : Module(name) {}

        static std::string LeftJustify(const char* s, size_t width) {
            std::string ret(s);

            for (size_t l = ret.length(); width > l; width--) {
                ret += ' ';
            }
            return ret;
        }

        // Used by js/connectdlg.js

#ifdef ENABLE_AUTHENTICATION
        static Error setUserPassword(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP555
            if (*parameter == '\0') {
                user_password->setDefault();
                return Error::Ok;
            }
            if (user_password->setStringValue(parameter) != Error::Ok) {
                log_string(out, "Invalid Password");
                return Error::InvalidValue;
            }
            return Error::Ok;
        }
#endif

        static Error restart(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            log_info("Restarting");
            protocol_send_event(&fullResetEvent);
            return Error::Ok;
        }

        // used by js/restartdlg.js
        static Error setSystemMode(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP444
            // parameter = trim(parameter);
            if (strcasecmp(parameter, "RESTART") != 0) {
                log_string(out, "Parameter must be RESTART");
                return Error::InvalidValue;
            }
            return restart(parameter, auth_level, out);
        }

        // Used by js/statusdlg.js
        static Error showSysStatsJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420

            JSONencoder j(&out);
            j.begin();
            j.member("cmd", "420");
            j.member("status", "ok");
            j.begin_array("data");

            platform_sys_stats(j);

            for (auto const& module : ModuleFactory::objects()) {
                module->wifi_stats(j);
            }

            std::string s("FluidNC ");
            s += git_info;
            j.id_value_object("FW version", s);

            j.end_array();
            j.end();
            return Error::Ok;
        }

        static void send_json_command_response(Channel& out, uint cmdID, bool isok, const std::string& message) {
            JSONencoder j(&out);
            j.begin();
            j.member("cmd", String(cmdID).c_str());
            j.member("status", isok ? "ok" : "error");
            j.member("data", message);
            j.end();
        }

        static Error showSysStats(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP420
            if (paramIsJSON(parameter)) {
                return showSysStatsJSON(parameter, auth_level, out);
            }

            platform_sys_stats(out);

            for (auto const& module : Modules()) {
                module->build_info(out);
            }

            log_stream(out, "FW version: FluidNC " << git_info);
            return Error::Ok;
        }

        static Error setWebSetting(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP401
            // The string is of the form "P=name T=type V=value
            // We do not need the "T=" (type) parameter because the
            // Setting objects know their own type.  We do not use
            // split_params because if fails if the value string
            // contains '='
            std::string p, v;
            bool        isJSON = paramIsJSON(parameter);
            if (!(get_param(parameter, "P=", p) && get_param(parameter, "V=", v))) {
                if (isJSON) {
                    send_json_command_response(out, 401, false, errorString(Error::InvalidValue));
                }
                return Error::InvalidValue;
            }

            Error ret = do_command_or_setting(p, v, auth_level, out);
            if (isJSON) {
                send_json_command_response(out, 401, ret == Error::Ok, errorString(ret));
            }

            return ret;
        }

        // Used by js/setting.js
        static Error listSettingsJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
            JSONencoder j(&out);
            j.begin();
            j.member("cmd", "400");
            j.member("status", "ok");
            j.begin_array("data");

            // NVS settings
            j.setCategory("Flash/Settings");
            for (Setting* js : Setting::List) {
                js->addWebui(&j);
            }

            // Configuration tree
            j.setCategory("Running/Config");
            Configuration::JsonGenerator gen(j);
            config->group(gen);

            j.end_array();
            j.end();

            return Error::Ok;
        }

        static Error listSettings(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP400
            if (parameter != NULL) {
                if (strstr(parameter, "json=yes") != NULL) {
                    return listSettingsJSON(parameter, auth_level, out);
                }
            }

            JSONencoder j(&out);

            j.begin();
            j.begin_array("EEPROM");

            // NVS settings
            j.setCategory("nvs");
            for (Setting* js : Setting::List) {
                js->addWebui(&j);
            }

            // Configuration tree
            j.setCategory("tree");
            Configuration::JsonGenerator gen(j);
            config->group(gen);

            j.end_array();
            j.end();

            return Error::Ok;
        }

        static Error showWebHelp(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP0
            log_string(out, "Persistent web settings - $name to show, $name=value to set");
            log_string(out, "ESPname FullName         Description");
            log_string(out, "------- --------         -----------");

            for (Setting* setting : Setting::List) {
                if (setting->getType() == WEBSET) {
                    log_stream(out,
                               LeftJustify(setting->getGrblName() ? setting->getGrblName() : "", 8)
                                   << LeftJustify(setting->getName(), 25 - 8) << setting->getDescription());
                }
            }
            log_string(out, "");
            log_string(out, "Other web commands: $name to show, $name=value to set");
            log_string(out, "ESPname FullName         Values");
            log_string(out, "------- --------         ------");

            for (Command* cp : Command::List) {
                if (cp->getType() == WEBCMD) {
                    LogStream s(out, "");
                    s << LeftJustify(cp->getGrblName() ? cp->getGrblName() : "", 8) << LeftJustify(cp->getName(), 25 - 8);
                    if (cp->getDescription()) {
                        s << cp->getDescription();
                    }
                }
            }
            return Error::Ok;
        }

        // Tell the spindle/Z controller board to join WiFi and start its ArduinoOTA server so
        // it can be reflashed wirelessly.  The XY board's own station credentials are pushed
        // over the inter-board link; the spindle then reports its IP back.
        static Error enableSpindleOTA(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            if (Spindles::SpindleBoard::instance == nullptr) {
                log_string(out, "No SpindleBoard spindle is configured");
                return Error::InvalidValue;
            }
            Spindles::SpindleBoard::instance->sendEnableOTA();
            return Error::Ok;
        }

        // Fetch the spindle board's trip recording (the ~2 s of motor state before its last
        // over-current or driver fault) into /spindle_trip.csv.  "$Spindle/Trip=TEST" records a
        // test one instead, to check the path end to end.
        static Error spindleTrip(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            if (Spindles::SpindleBoard::instance == nullptr) {
                log_string(out, "No SpindleBoard spindle is configured");
                return Error::InvalidValue;
            }
            bool test = parameter && strcasecmp(parameter, "TEST") == 0;
            if (parameter && *parameter && !test) {
                log_string(out, "Usage: $Spindle/Trip or $Spindle/Trip=TEST");
                return Error::InvalidValue;
            }
            Spindles::SpindleBoard::instance->sendTripRequest(test);
            return Error::Ok;
        }

        // Tell the spindle/Z controller board to run its Z homing cycle (raise the Z until the
        // top-of-travel beam breaks).  Exposed as $Spindle/Home for the web UI test button.
        static Error homeSpindle(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            if (Spindles::SpindleBoard::instance == nullptr) {
                log_string(out, "No SpindleBoard spindle is configured");
                return Error::InvalidValue;
            }
            Spindles::SpindleBoard::instance->sendHome();
            return Error::Ok;
        }

        // Tell the spindle/Z controller board to remove the loaded tool (raise the Z until the
        // tool clears the top-of-travel beam).  Exposed as $Spindle/RemoveTool for the web UI.
        static Error removeSpindleTool(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            if (Spindles::SpindleBoard::instance == nullptr) {
                log_string(out, "No SpindleBoard spindle is configured");
                return Error::InvalidValue;
            }
            Spindles::SpindleBoard::instance->sendRemoveTool();
            return Error::Ok;
        }

        void init() override {
            // If authentication enabled, display_settings skips or displays <Authentication Required>
            // RU - need user or admin password to read
            // WU - need user or admin password to set
            // WA - need admin password to set
            new WebReportCommand(NULL, WEBCMD, WU, "ESP420", "System/Stats", showSysStats, anyState);
            new WebCommand("RESTART", WEBCMD, WA, "ESP444", "System/Control", setSystemMode);

            //      new WebCommand("ON|OFF", WEBCMD, WA, "ESP115", "Radio/State", setRadioState);

            new WebCommand("P=position T=type V=value", WEBCMD, WA, "ESP401", "WebUI/Set", setWebSetting);
            new WebReportCommand(NULL, WEBCMD, WU, "ESP400", "WebUI/List", listSettings, anyState);
            new WebReportCommand(NULL, WEBCMD, WG, "ESP0", "WebUI/Help", showWebHelp, anyState);
            new WebReportCommand(NULL, WEBCMD, WG, "ESP", "WebUI/Help", showWebHelp, anyState);

            new WebCommand(NULL, WEBCMD, WA, "ESP810", "Spindle/EnableOTA", enableSpindleOTA);
            new WebCommand(NULL, WEBCMD, WU, "ESP811", "Spindle/Home", homeSpindle);
            new WebCommand(NULL, WEBCMD, WU, "ESP812", "Spindle/RemoveTool", removeSpindleTool);
            new WebCommand(NULL, WEBCMD, WU, "ESP813", "Spindle/Trip", spindleTrip);
        }
    };
    ModuleFactory::InstanceBuilder<WebCommands> web_commands_module __attribute__((init_priority(103))) ("web_commands", true);
}
