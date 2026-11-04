#include "app/Language.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>

namespace bc {

namespace {
const char *kSettingsKey = "ui/language";
}

Language &Language::instance()
{
    static Language inst;
    return inst;
}

QStringList Language::availableCodes()
{
    return {QStringLiteral("uz_Cyrl"), QStringLiteral("uz_Latn"), QStringLiteral("ru"),
            QStringLiteral("en")};
}

QString Language::nativeName(const QString &code)
{
    if (code == QLatin1String("uz_Cyrl"))
        return QStringLiteral("Ўзбекча (кирилл)");
    if (code == QLatin1String("uz_Latn"))
        return QStringLiteral("O‘zbekcha (lotin)");
    if (code == QLatin1String("ru"))
        return QStringLiteral("Русский");
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    return code;
}

QString Language::defaultCode()
{
    return QStringLiteral("uz_Cyrl");
}

void Language::loadSaved()
{
    QSettings s;
    const QString code = s.value(kSettingsKey, defaultCode()).toString();
    if (!setLanguage(code, false))
        setLanguage(defaultCode(), false);
}

bool Language::setLanguage(const QString &code, bool persist)
{
    if (!availableCodes().contains(code))
        return false;

    QTranslator next;
    if (!next.load(QStringLiteral(":/i18n/bukhara_%1.qm").arg(code)))
        return false;

    QCoreApplication::removeTranslator(&m_translator);
    if (!m_translator.load(QStringLiteral(":/i18n/bukhara_%1.qm").arg(code)))
        return false;
    QCoreApplication::installTranslator(&m_translator); // sends LanguageChange

    m_current = code;
    if (code == QLatin1String("ru"))
        QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
    else if (code == QLatin1String("en"))
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    else if (code == QLatin1String("uz_Latn"))
        QLocale::setDefault(QLocale(QLocale::Uzbek, QLocale::LatinScript, QLocale::Uzbekistan));
    else
        QLocale::setDefault(QLocale(QLocale::Uzbek, QLocale::CyrillicScript, QLocale::Uzbekistan));

    if (persist) {
        QSettings s;
        s.setValue(kSettingsKey, code);
    }
    emit languageChanged(code);
    return true;
}

} // namespace bc
