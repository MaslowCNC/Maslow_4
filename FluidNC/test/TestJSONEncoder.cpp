#ifdef ESP32

#include "TestFramework.h"
#include "../src/WebUI/JSONEncoder.h"
#include <string>

using namespace WebUI;

Test(JSONEncoder, NullStatusHandling) {
    // Test that null status values are handled safely
    std::string result;
    JSONencoder j(false, &result);
    
    j.begin();
    j.member("status", nullptr);  // This should not crash and should output "unknown"
    j.end();
    
    // Check that the JSON contains "unknown" instead of crashing
    Assert(result.find("\"status\":\"unknown\"") != std::string::npos, "Status should default to 'unknown' for null values");
}

Test(JSONEncoder, NullNonStatusHandling) {
    // Test that null non-status values are handled safely with empty string
    std::string result;
    JSONencoder j(false, &result);
    
    j.begin();
    j.member("message", nullptr);  // This should not crash and should output empty string
    j.end();
    
    // Check that the JSON contains empty string for non-status fields
    Assert(result.find("\"message\":\"\"") != std::string::npos, "Non-status fields should default to empty string for null values");
}

Test(JSONEncoder, ValidStatusValue) {
    // Test that valid status values work normally
    std::string result;
    JSONencoder j(false, &result);
    
    j.begin();
    j.member("status", "ready");
    j.end();
    
    // Check that the JSON contains the expected value
    Assert(result.find("\"status\":\"ready\"") != std::string::npos, "Valid status values should be preserved");
}

Test(JSONEncoder, QuotedNullHandling) {
    // Test that the quoted function handles null pointers safely
    std::string result;
    JSONencoder j(false, &result);
    
    j.begin();
    j.quoted(nullptr);  // This should output empty quotes
    j.end();
    
    // The result should contain empty quotes
    Assert(result.find("\"\"") != std::string::npos, "Quoted null should produce empty quotes");
}

#endif