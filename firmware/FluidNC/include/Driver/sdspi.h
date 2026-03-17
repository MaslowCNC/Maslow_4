#include <system_error>

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1);
void sd_unmount();
void sd_deinit_slot();

// Allow up to 4 concurrent open file handles to prevent error 66 (FsFailedOpenFile)
// when the dual-core ESP32-S3 has one core serving an HTTP file preview while the
// other core simultaneously opens the same file for GCode execution.
std::error_code sd_mount(int max_files = 4);
