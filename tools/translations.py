"""Interface translations for BUKHARA CINEMA.

Source texts are English (in the C++ code). Uzbek Cyrillic and Russian are
written by hand here; Uzbek Latin is produced from Uzbek Cyrillic by the
official transliteration rules (with a few manual overrides).

Usage: python tools/translations.py   (fills i18n/*.ts after lupdate)
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# source: (uz_Cyrl, ru)
T = {
    # FeatureStatus
    "Ready": ("Тайёр", "Готово"),
    "Partial": ("Қисман", "Частично"),
    "Planned": ("Режада", "Запланировано"),
    "[phase %1]": ("[%1-босқич]", "[этап %1]"),
    "Main window with five resizable zones": ("Ўлчами ўзгарадиган беш зонали асосий ойна", "Главное окно с пятью зонами изменяемого размера"),
    "Panels can be resized, moved, hidden and reset; the layout is remembered.": (
        "Панелларнинг ўлчамини ўзгартириш, кўчириш, яшириш ва асл ҳолатига қайтариш мумкин; жойлашув эсда сақланади.",
        "Панели можно менять по размеру, перемещать, скрывать и сбрасывать; расположение запоминается."),
    "Dark grey theme": ("Тўқ кулранг мавзу", "Тёмно-серая тема"),
    "Interface languages: Uzbek (Cyrillic, Latin) and Russian": ("Интерфейс тиллари: ўзбекча (кирилл, лотин) ва русча", "Языки интерфейса: узбекский (кириллица, латиница) и русский"),
    "Switch in File > Settings; applies immediately.": ("Файл > Созламалар орқали алмаштирилади; дарҳол қўлланади.", "Переключается в Файл > Настройки; применяется сразу."),
    "About window and third-party licenses": ("«Дастур ҳақида» ойнаси ва учинчи томон лицензиялари", "Окно «О программе» и лицензии сторонних компонентов"),
    "Log file for problem reports": ("Муаммолар ҳисоботи учун журнал файли", "Файл журнала для отчётов о проблемах"),
    "Help > Open log folder.": ("Ёрдам > Журнал папкасини очиш.", "Справка > Открыть папку журнала."),
    "Windows installer": ("Windows ўрнатувчиси", "Установщик для Windows"),
    "Installer wizard is in Russian or English; Uzbek wizard texts are planned.": (
        "Ўрнатиш устаси русча ёки инглизча; ўзбекча матнлари режада.",
        "Мастер установки на русском или английском; узбекские тексты запланированы."),
    "New project, open, save": ("Янги лойиҳа, очиш, сақлаш", "Новый проект, открытие, сохранение"),
    "Media import, bin, metadata, thumbnails, relinking missing files": (
        "Медиа импорт, кутубхона, метамаълумотлар, thumbnail, йўқолган файлларни қайта боғлаш",
        "Импорт медиа, медиатека, метаданные, миниатюры, повторная привязка потерянных файлов"),
    "Sequence settings: size, frame rate, audio": ("Секвенция созламалари: ўлчам, кадрлар частотаси, аудио", "Настройки эпизода: размер, частота кадров, звук"),
    "Multitrack timeline: place, move, split, trim, ripple delete, snapping": (
        "Кўп йўлакли timeline: жойлаш, кўчириш, бўлиш, trim, ripple delete, snapping",
        "Многодорожечный таймлайн: размещение, перемещение, разрезание, подрезка, ripple-удаление, привязка"),
    "Undo/redo and keyboard shortcuts": ("Бекор қилиш/қайтариш ва тезкор тугмалар", "Отмена/повтор и сочетания клавиш"),
    "Playback in Program monitor": ("Program мониторида ижро", "Воспроизведение в мониторе программы"),
    "Text titles and audio volume, fades, mute and solo": ("Матн титрлари ва овоз баландлиги, fade, mute ва solo", "Текстовые титры и громкость, затухания, mute и solo"),
    "MP4 export (H.264 + AAC) with progress and cancel": ("MP4 экспорт (H.264 + AAC), жараён кўрсаткичи ва бекор қилиш", "Экспорт MP4 (H.264 + AAC) с индикатором и отменой"),
    "Autosave, crash recovery, versions, proxies, waveforms": ("Autosave, авариядан кейин тиклаш, версиялар, proxy, waveform", "Автосохранение, восстановление после сбоя, версии, прокси, формы волны"),
    "Professional editing: insert, overwrite, roll, slip, slide, markers, nesting": (
        "Профессионал монтаж: insert, overwrite, roll, slip, slide, маркерлар, nested sequence",
        "Профессиональный монтаж: вставка, перезапись, roll, slip, slide, маркеры, вложенные эпизоды"),
    "Transform, keyframes, speed changes": ("Трансформация, keyframe, тезликни ўзгартириш", "Трансформация, ключевые кадры, изменение скорости"),
    "Transitions, effects, masks, chroma key": ("Ўтишлар, эффектлар, ниқоблар, chroma key", "Переходы, эффекты, маски, хромакей"),
    "Color correction, LUT, scopes": ("Ранг тузатиш, LUT, scopes", "Цветокоррекция, LUT, скопы"),
    "Audio mixer, EQ, compressor, noise reduction, loudness": ("Аудио микшер, эквалайзер, компрессор, шовқинни камайтириш, loudness", "Аудиомикшер, эквалайзер, компрессор, шумоподавление, громкость (loudness)"),
    "Subtitles and SRT import/export": ("Субтитрлар ва SRT импорт/экспорт", "Субтитры и импорт/экспорт SRT"),
    "Multicamera editing": ("Кўп камерали монтаж", "Многокамерный монтаж"),
    "Export queue, render preview, GPU acceleration": ("Экспорт навбати, render preview, GPU тезлаштириш", "Очередь экспорта, предпросмотр рендера, ускорение GPU"),
    "AI assistants: transcription, text-based editing, scene detection": (
        "AI ёрдамчилар: транскрипция, матн асосида монтаж, саҳналарни аниқлаш",
        "AI-помощники: транскрипция, монтаж по тексту, определение сцен"),
    "Plugins and project interchange": ("Плагинлар ва лойиҳа алмашинуви", "Плагины и обмен проектами"),
    # AboutDialog
    "Third-party components": ("Учинчи томон компонентлари", "Сторонние компоненты"),
    "BUKHARA CINEMA uses the following open-source components. They are linked dynamically (as separate DLL files), so they can be replaced with compatible versions.": (
        "BUKHARA CINEMA қуйидаги очиқ кодли компонентлардан фойдаланади. Улар алоҳида DLL файллар сифатида уланган, шунинг учун уларни мос версиялар билан алмаштириш мумкин.",
        "BUKHARA CINEMA использует следующие компоненты с открытым кодом. Они подключены динамически (отдельными DLL), поэтому их можно заменить совместимыми версиями."),
    "User interface": ("Фойдаланувчи интерфейси", "Пользовательский интерфейс"),
    "GCC runtime library exception, MIT/ZPL": ("GCC runtime library exception, MIT/ZPL", "GCC runtime library exception, MIT/ZPL"),
    "C++ runtime libraries": ("C++ runtime кутубхоналари", "Библиотеки среды выполнения C++"),
    'Full license texts are installed in the "licenses" folder next to the program. Qt source code: https://download.qt.io/': (
        "Лицензияларнинг тўлиқ матни дастур ёнидаги \"licenses\" папкасига ўрнатилади. Qt манба коди: https://download.qt.io/",
        "Полные тексты лицензий установлены в папку \"licenses\" рядом с программой. Исходный код Qt: https://download.qt.io/"),
    "The video engine (MLT, FFmpeg) is added in later phases; its licenses will be listed here then.": (
        "Видео двигатели (MLT, FFmpeg) кейинги босқичларда қўшилади; уларнинг лицензиялари ўшанда шу ерда кўрсатилади.",
        "Видеодвижок (MLT, FFmpeg) добавляется на следующих этапах; тогда его лицензии появятся здесь."),
    "About BUKHARA CINEMA": ("BUKHARA CINEMA ҳақида", "О программе BUKHARA CINEMA"),
    "Version %1": ("Версия %1", "Версия %1"),
    "Independent video editor for Windows.": ("Windows учун мустақил видеомуҳаррир.", "Независимый видеоредактор для Windows."),
    "Phase 0: interface shell. Editing features arrive in the next phases; see Help > Feature status.": (
        "0-босқич: интерфейс қобиғи. Монтаж функциялари кейинги босқичларда қўшилади; Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 0: оболочка интерфейса. Функции монтажа появятся на следующих этапах; см. Справка > Состояние функций."),
    "About": ("Дастур ҳақида", "О программе"),
    "Licenses": ("Лицензиялар", "Лицензии"),
    "Close": ("Ёпиш", "Закрыть"),
    # MainWindow
    "Source": ("Манба", "Источник"),
    "Program": ("Натижа", "Программа"),
    "Media bin": ("Медиа кутубхонаси", "Медиатека"),
    "Project files": ("Лойиҳа файллари", "Файлы проекта"),
    "Clip parameters": ("Клип параметрлари", "Параметры клипа"),
    "Effects": ("Эффектлар", "Эффекты"),
    "Color": ("Ранг", "Цвет"),
    "Audio": ("Овоз", "Звук"),
    "New project": ("Янги лойиҳа", "Новый проект"),
    "Open project...": ("Лойиҳани очиш...", "Открыть проект..."),
    "Save": ("Сақлаш", "Сохранить"),
    "Save as...": ("Бошқа ном билан сақлаш...", "Сохранить как..."),
    "Import media...": ("Медиа импорт...", "Импорт медиа..."),
    "Export...": ("Экспорт...", "Экспорт..."),
    "Undo": ("Бекор қилиш", "Отменить"),
    "Redo": ("Қайтариш", "Повторить"),
    "Cut": ("Қирқиб олиш", "Вырезать"),
    "Copy": ("Нусха олиш", "Копировать"),
    "Paste": ("Қўйиш", "Вставить"),
    "Delete": ("Ўчириш", "Удалить"),
    "Ripple delete": ("Бўшлиқсиз ўчириш (ripple)", "Удалить со сдвигом (ripple)"),
    "Keyboard shortcuts...": ("Тезкор тугмалар...", "Сочетания клавиш..."),
    "Split at playhead": ("Курсор жойида бўлиш", "Разрезать по указателю"),
    "Audio gain...": ("Овоз кучайтириш...", "Усиление звука..."),
    "Speed / duration...": ("Тезлик / давомийлик...", "Скорость / длительность..."),
    "Link / unlink audio and video": ("Видео ва овозни боғлаш / ажратиш", "Связать / разъединить видео и звук"),
    "Nest...": ("Ичма-ич секвенция...", "Вложить..."),
    "Sequence settings...": ("Секвенция созламалари...", "Настройки эпизода..."),
    "Add track": ("Йўлак қўшиш", "Добавить дорожку"),
    "Snapping": ("Ёпишиш (snapping)", "Привязка"),
    "Render preview": ("Preview рендери", "Рендер предпросмотра"),
    "Add marker": ("Маркер қўшиш", "Добавить маркер"),
    "Go to next marker": ("Кейинги маркерга ўтиш", "К следующему маркеру"),
    "New text": ("Янги матн", "Новый текст"),
    "Import subtitles (SRT)...": ("Субтитр импорти (SRT)...", "Импорт субтитров (SRT)..."),
    "&File": ("&Файл", "&Файл"),
    "&Edit": ("&Таҳрир", "&Правка"),
    "&Clip": ("&Клип", "&Клип"),
    "&Sequence": ("&Секвенция", "&Эпизод"),
    "&Markers": ("&Маркерлар", "&Маркеры"),
    "&Graphics": ("&Графика", "&Графика"),
    "&View": ("&Кўриниш", "&Вид"),
    "&Window": ("&Ойна", "&Окно"),
    "&Help": ("&Ёрдам", "&Справка"),
    "Planned for phase %1": ("%1-босқичда режалаштирилган", "Запланировано на этап %1"),
    "Settings...": ("Созламалар...", "Настройки..."),
    "Exit": ("Чиқиш", "Выход"),
    "Full screen": ("Тўлиқ экран", "Полный экран"),
    "Reset layout": ("Панеллар жойлашувини тиклаш", "Сбросить расположение панелей"),
    "Feature status...": ("Функциялар ҳолати...", "Состояние функций..."),
    "Licenses...": ("Лицензиялар...", "Лицензии..."),
    "Open log folder": ("Журнал папкасини очиш", "Открыть папку журнала"),
    "About BUKHARA CINEMA...": ("BUKHARA CINEMA ҳақида...", "О программе BUKHARA CINEMA..."),
    "Main toolbar": ("Асосий асбоблар панели", "Главная панель инструментов"),
    "Export": ("Экспорт", "Экспорт"),
    "Project": ("Лойиҳа", "Проект"),
    "Properties": ("Параметрлар", "Свойства"),
    "Timeline": ("Timeline", "Таймлайн"),
    "Clip": ("Клип", "Клип"),
    "Phase 0: interface only. Editing features are not working yet.": (
        "0-босқич: фақат интерфейс. Монтаж функциялари ҳали ишламайди.",
        "Этап 0: только интерфейс. Функции монтажа пока не работают."),
    # MonitorView
    "No video yet.\nPlayback is planned for phase 2.": ("Видео ҳали йўқ.\nИжро 2-босқичда режалаштирилган.", "Видео пока нет.\nВоспроизведение запланировано на этап 2."),
    "Playback is planned for phase 2": ("Ижро 2-босқичда режалаштирилган", "Воспроизведение запланировано на этап 2"),
    # PlaceholderPanel
    "Not working yet. Planned for phase %1.": ("Ҳали ишламайди. %1-босқичда режалаштирилган.", "Пока не работает. Запланировано на этап %1."),
    # SettingsDialog
    "Settings": ("Созламалар", "Настройки"),
    "Interface language:": ("Интерфейс тили:", "Язык интерфейса:"),
    "OK": ("OK", "OK"),
    "Cancel": ("Бекор қилиш", "Отмена"),
    # StatusDialog
    "Feature status": ("Функциялар ҳолати", "Состояние функций"),
    "Status": ("Ҳолат", "Статус"),
    "Phase": ("Босқич", "Этап"),
    "Feature": ("Функция", "Функция"),
    "Note": ("Изоҳ", "Примечание"),
    # TimelinePlaceholder
    "Timeline editing is planned for phase 2. This is an empty layout only.": (
        "Timeline’да монтаж 2-босқичда режалаштирилган. Ҳозир бу фақат бўш кўриниш.",
        "Монтаж на таймлайне запланирован на этап 2. Сейчас это только пустой макет."),
}

# Manual Latin overrides where transliteration is not ideal.
LATIN_OVERRIDES = {
    "[phase %1]": "[%1-bosqich]",
}

_MAP = {
    "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "ё": "yo", "ж": "j", "з": "z",
    "и": "i", "й": "y", "к": "k", "л": "l", "м": "m", "н": "n", "о": "o", "п": "p",
    "р": "r", "с": "s", "т": "t", "у": "u", "ф": "f", "х": "x", "ч": "ch", "ш": "sh",
    "ъ": "’", "ь": "", "э": "e", "ю": "yu", "я": "ya", "ў": "o‘", "қ": "q", "ғ": "g‘",
    "ҳ": "h", "щ": "sh", "ы": "i",
}
_VOWELS = set("аеёиоуўэюяaeiou")


def cyr_to_lat(text: str) -> str:
    out = []
    n = len(text)
    for i, ch in enumerate(text):
        low = ch.lower()
        prev = text[i - 1].lower() if i > 0 else ""
        nxt = text[i + 1] if i + 1 < n else ""
        if low == "е":
            # "ye" at word start or after a vowel / ъ
            lat = "ye" if (not prev.isalpha() or prev in _VOWELS or prev == "ъ") else "e"
        elif low == "ц":
            lat = "ts" if prev in _VOWELS else "s"
        elif low in _MAP:
            lat = _MAP[low]
        else:
            out.append(ch)
            continue
        if ch != low and lat:
            # Uppercase: whole word upper -> upper digraph, else capitalize
            if nxt.isupper() or (not nxt.isalpha() and prev.isupper()):
                lat = lat.upper()
            else:
                lat = lat[0].upper() + lat[1:]
        out.append(lat)
    return "".join(out)


def fill(ts_path: Path, lang: str) -> int:
    tree = ET.parse(ts_path)
    root = tree.getroot()
    missing = 0
    for msg in root.iter("message"):
        src = msg.findtext("source")
        tr = msg.find("translation")
        if tr is None:
            tr = ET.SubElement(msg, "translation")
        if tr.get("type") in ("obsolete", "vanished"):
            continue
        if src not in T:
            print(f"MISSING translation for: {src!r}", file=sys.stderr)
            missing += 1
            continue
        uz, ru = T[src]
        if lang == "uz_Cyrl":
            text = uz
        elif lang == "ru":
            text = ru
        else:
            text = LATIN_OVERRIDES.get(src, cyr_to_lat(uz))
        tr.text = text
        tr.attrib.pop("type", None)
    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    # keep the DOCTYPE line Qt tools expect
    data = ts_path.read_text(encoding="utf-8")
    if "<!DOCTYPE TS>" not in data:
        data = data.replace("?>\n", "?>\n<!DOCTYPE TS>\n", 1)
        ts_path.write_text(data, encoding="utf-8")
    return missing


def main() -> int:
    i18n = Path(__file__).resolve().parent.parent / "i18n"
    missing = 0
    for lang in ("uz_Cyrl", "uz_Latn", "ru"):
        missing += fill(i18n / f"bukhara_{lang}.ts", lang)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
