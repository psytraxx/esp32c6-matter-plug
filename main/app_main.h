#pragma once

// App-level glue shared between bl0937.cpp (over-power trip) and
// matter_setup.cpp/button callbacks: pushes the relay's actual state (from
// RelayIsOn()) into the OnOff cluster, so a controller subscriber sees the
// change regardless of what caused it (button press, over-power trip).
//
// Equivalent to AppTask::UpdateClusterState() in uascent-matter/src/app_task.cpp,
// but as a free function rather than a singleton method, matching this
// project's C-style module boundaries (see esp32c6-radar-demo-matter).
#ifdef __cplusplus
extern "C" {
#endif

void AppUpdateOnOffCluster(void);

#ifdef __cplusplus
}
#endif
