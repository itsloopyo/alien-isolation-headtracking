// dev's reader, compiled here whole so its file-local Load runs once per input
// instead of once per process through the cached config::Get.
#include "config.cpp"

#include "oracle_adapter.h"

namespace aiht_oracle_view {

OracleSettings RunOracle() {
    const config::Settings s = config::Load();
    return {s.world_space_yaw, s.yaw_mode_key, s.helmet_follows_head, s.local_smoothing,
            s.remote_smoothing};
}

std::string OraclePath() { return config::IniPathNextToExe(); }

}  // namespace aiht_oracle_view
