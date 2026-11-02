#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>

namespace bc {

// Interface language manager.
// Supported codes: "uz_Cyrl" (Uzbek, Cyrillic), "uz_Latn" (Uzbek, Latin), "ru" (Russian), "en" (English;
// the source texts are English, so its catalogue repeats them).
// The chosen language is stored in QSettings and applied live: every widget
// that shows text re-reads it on QEvent::LanguageChange.
class Language : public QObject
{
    Q_OBJECT
public:
    static Language &instance();

    static QStringList availableCodes();
    // Native, untranslated name, e.g. "Ўзбекча (кирилл)".
    static QString nativeName(const QString &code);
    static QString defaultCode();

    QString current() const { return m_current; }

    // Loads the stored language (or the default one).
    void loadSaved();
    // Switches the UI language; returns false if the code is unknown or its
    // translation file could not be loaded.
    bool setLanguage(const QString &code, bool persist = true);

signals:
    void languageChanged(const QString &code);

private:
    Language() = default;
    QTranslator m_translator;
    QString m_current;
};

} // namespace bc
