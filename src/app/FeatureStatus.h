#pragma once

#include <QList>
#include <QString>

namespace bc {

// Honest list of what works, what partly works and what is only planned.
// Shown in Help > Feature status and kept in sync with docs/STATUS.md.
struct FeatureEntry
{
    enum class State { Ready, Partial, Planned };
    State state;
    int phase;          // phase in which it is (or will be) delivered
    QString title;      // translated
    QString note;       // translated, may be empty
};

namespace FeatureStatus {
QList<FeatureEntry> entries();
QString stateName(FeatureEntry::State state);
// e.g. "[phase 2]" translated, used as a suffix for planned menu items.
QString phaseTag(int phase);
}

} // namespace bc
