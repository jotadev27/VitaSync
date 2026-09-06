#pragma once

#include <QString>

namespace vsp {

inline QString applicationName() { return QStringLiteral("VitaSync"); }
inline QString versionString() { return QStringLiteral(VSP_VERSION_STRING); }

} // namespace vsp
