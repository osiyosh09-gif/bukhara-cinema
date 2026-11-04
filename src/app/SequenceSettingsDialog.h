#pragma once

#include "core/Project.h"

#include <QDialog>

class QComboBox;
class QSpinBox;
class QLabel;

namespace bc {

class SequenceSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SequenceSettingsDialog(const SequenceSettings &current, QWidget *parent = nullptr);
    SequenceSettings settings() const;

    struct Preset
    {
        const char *name; // translatable source text
        int width, height, fpsNum, fpsDen;
    };
    static QList<Preset> presets();

private:
    void applyPreset(int index);
    void syncPresetFromFields();

    QComboBox *m_preset;
    QSpinBox *m_width;
    QSpinBox *m_height;
    QComboBox *m_fps;
    QComboBox *m_rate;
    QComboBox *m_channels;
    bool m_syncing = false;
};

} // namespace bc
