#pragma once

#include <QString>

namespace bc::Logging {

// Writes Qt messages to %LOCALAPPDATA%/BUKHARA CINEMA/logs/bukhara.log
// so problems can be reported. Rotates at 1 MB (keeps one old file).
void init();
QString logDirectory();

} // namespace bc::Logging
