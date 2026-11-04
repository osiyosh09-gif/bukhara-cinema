#pragma once

#include <QString>

namespace bc {

// The product name is fixed and is never translated.
inline QString appName() { return QStringLiteral("BUKHARA CINEMA"); }
inline QString appVersion() { return QStringLiteral(BC_VERSION); }
inline QString orgName() { return QStringLiteral("BUKHARA CINEMA"); }

} // namespace bc
