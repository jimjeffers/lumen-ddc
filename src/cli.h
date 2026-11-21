#pragma once

#include <QStringList>

// Commands that need no display, for the Omarchy bar plugin, scripts, and
// agents. Each prints JSON (or an error on stderr) and exits 0 or 1.
//
//   lumen state                         every monitor and its settings
//   lumen set TARGET SETTING VALUE      TARGET: all | serial | connector
//                                       SETTING: brightness | contrast | volume |
//                                                temp | mode | halo | 0xNN
//                                       VALUE: number (0x.. ok) or a mode/halo name
namespace Cli {

// True when argv names a headless command, so main() can skip the GUI.
bool handles(const QStringList &args);

int run(const QStringList &args);

}  // namespace Cli
