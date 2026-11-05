"""Interface translations for BUKHARA CINEMA.

Source texts are English (in the C++ code); the English catalogue repeats
them (a QTranslator needs a .qm to load). Uzbek Cyrillic and Russian are
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
    "Interface languages: Uzbek (Cyrillic, Latin), Russian and English": ("Интерфейс тиллари: ўзбекча (кирилл, лотин), русча ва инглизча", "Языки интерфейса: узбекский (кириллица, латиница), русский и английский"),
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
    "Render preview": ("Превью рендер", "Просчитать превью"),
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
    # --- Phase 1 ---------------------------------------------------------
    "New project, open, save, recent projects": ("Янги лойиҳа, очиш, сақлаш, охирги лойиҳалар", "Новый проект, открытие, сохранение, последние проекты"),
    "Project file .bcproj; warns about unsaved changes.": ("Лойиҳа файли .bcproj; сақланмаган ўзгаришлар ҳақида огоҳлантиради.", "Файл проекта .bcproj; предупреждает о несохранённых изменениях."),
    "Media import: files, folders, drag and drop from Explorer": ("Медиа импорт: файллар, папкалар, Explorer’дан судраб ташлаш", "Импорт медиа: файлы, папки, перетаскивание из Проводника"),
    "Media bin: folders, search, renaming, metadata, thumbnails": ("Медиа кутубхонаси: папкалар, қидирув, қайта номлаш, метамаълумотлар, thumbnail", "Медиатека: папки, поиск, переименование, метаданные, миниатюры"),
    "Missing files: detection and relinking": ("Йўқолган файллар: аниқлаш ва қайта боғлаш", "Потерянные файлы: обнаружение и повторная привязка"),
    "Other missing files in the same folder are relinked automatically.": ("Шу папкадаги бошқа йўқолган файллар ҳам автоматик боғланади.", "Другие потерянные файлы из той же папки привязываются автоматически."),
    "Autosave, crash recovery, versions, backups": ("Autosave, авариядан кейин тиклаш, версиялар, захира нусхалар", "Автосохранение, восстановление после сбоя, версии, резервные копии"),
    "Audio waveforms and proxy files": ("Овоз тўлқин шакли (waveform) ва proxy файллар", "Формы волны звука и прокси-файлы"),
    "%1 GB": ("%1 ГБ", "%1 ГБ"),
    "%1 MB": ("%1 МБ", "%1 МБ"),
    "%1 KB": ("%1 КБ", "%1 КБ"),
    "%1 B": ("%1 Б", "%1 Б"),
    "mono": ("моно", "моно"),
    "stereo": ("стерео", "стерео"),
    "%1 ch": ("%1 канал", "%1 кан."),
    "%1 kHz, %2": ("%1 кГц, %2", "%1 кГц, %2"),
    "Video": ("Видео", "Видео"),
    "Image": ("Расм", "Изображение"),
    "Unknown": ("Номаълум", "Неизвестно"),
    "%1, %2 fps, %3": ("%1, %2 кадр/с, %3", "%1, %2 кадр/с, %3"),
    "Recent projects": ("Охирги лойиҳалар", "Последние проекты"),
    "Save project (Ctrl+S)": ("Лойиҳани сақлаш (Ctrl+S)", "Сохранить проект (Ctrl+S)"),
    "Phase 1: projects and media bin work. Timeline editing is not working yet.": (
        "1-босқич: лойиҳа ва медиа кутубхонаси ишлайди. Timeline’да монтаж ҳали ишламайди.",
        "Этап 1: проекты и медиатека работают. Монтаж на таймлайне пока не работает."),
    "Unsaved changes": ("Сақланмаган ўзгаришлар", "Несохранённые изменения"),
    'Project "%1" has unsaved changes. Save them?': ("«%1» лойиҳасида сақланмаган ўзгаришлар бор. Сақлансинми?", "В проекте «%1» есть несохранённые изменения. Сохранить?"),
    "Don't save": ("Сақламаслик", "Не сохранять"),
    "Open project": ("Лойиҳани очиш", "Открыть проект"),
    "BUKHARA CINEMA projects (*.%1)": ("BUKHARA CINEMA лойиҳалари (*.%1)", "Проекты BUKHARA CINEMA (*.%1)"),
    'Cannot open "%1":\n%2': ("«%1» очилмади:\n%2", "Не удалось открыть «%1»:\n%2"),
    "Missing files": ("Йўқолган файллар", "Потерянные файлы"),
    "Media files not found: %1. They may have been moved or renamed.": ("Топилмаган медиа файллар: %1. Улар кўчирилган ёки қайта номланган бўлиши мумкин.", "Не найдено медиафайлов: %1. Возможно, они перемещены или переименованы."),
    "Search in folder...": ("Папкадан қидириш...", "Искать в папке..."),
    "Later": ("Кейинроқ", "Позже"),
    "Cannot save the project:\n%1": ("Лойиҳа сақланмади:\n%1", "Не удалось сохранить проект:\n%1"),
    "Saved: %1": ("Сақланди: %1", "Сохранено: %1"),
    "Save project as": ("Лойиҳани бошқа ном билан сақлаш", "Сохранить проект как"),
    "(empty)": ("(бўш)", "(пусто)"),
    "File not found": ("Файл топилмади", "Файл не найден"),
    "Reading...": ("Ўқилмоқда...", "Чтение..."),
    "Cannot read file": ("Файлни ўқиб бўлмади", "Не удалось прочитать файл"),
    "Name": ("Номи", "Имя"),
    "Duration": ("Давомийлик", "Длительность"),
    "Resolution": ("Ўлчам", "Разрешение"),
    "FPS": ("Кадр/с", "Кадр/с"),
    "Size": ("Ҳажм", "Размер"),
    "Import": ("Импорт", "Импорт"),
    "Import media files (Ctrl+I). You can also drag files here from Explorer.": ("Медиа файлларни импорт қилиш (Ctrl+I). Файлларни Explorer’дан шу ерга судраб ташлаш ҳам мумкин.", "Импорт медиафайлов (Ctrl+I). Файлы можно также перетащить сюда из Проводника."),
    "Folder": ("Папка", "Папка"),
    "New folder": ("Янги папка", "Новая папка"),
    "Search by name": ("Ном бўйича қидириш", "Поиск по имени"),
    "Files: %1": ("Файллар: %1", "Файлов: %1"),
    "Not found: %1": ("Топилмади: %1", "Не найдено: %1"),
    "Import media": ("Медиа импорт", "Импорт медиа"),
    "Media files (%1)": ("Медиа файллар (%1)", "Медиафайлы (%1)"),
    "All files (*)": ("Барча файллар (*)", "Все файлы (*)"),
    "Folder name:": ("Папка номи:", "Имя папки:"),
    'Relink "%1"': ("«%1» ни қайта боғлаш", "Повторная привязка «%1»"),
    "Expected file: %1": ("Кутилган файл: %1", "Ожидаемый файл: %1"),
    "Relink": ("Қайта боғлаш", "Повторная привязка"),
    "The selected file looks different from the original (%1). Use it anyway?": ("Танланган файл асл файлдан фарқ қилади (%1). Барибир ишлатилсинми?", "Выбранный файл отличается от исходного (%1). Всё равно использовать?"),
    "Use this file": ("Шу файлни ишлатиш", "Использовать этот файл"),
    "Cannot read the file: %1": ("Файлни ўқиб бўлмади: %1", "Не удалось прочитать файл: %1"),
    "Other missing files found in the same folder and relinked: %1.": ("Шу папкада топилиб, қайта боғланган бошқа файллар: %1.", "Другие файлы, найденные в той же папке и привязанные: %1."),
    "Folder to search for missing files": ("Йўқолган файлларни қидириш учун папка", "Папка для поиска потерянных файлов"),
    "Files relinked: %1.": ("Қайта боғланган файллар: %1.", "Привязано файлов: %1."),
    "Remove from project": ("Лойиҳадан олиб ташлаш", "Убрать из проекта"),
    "Remove the selected items from the project? Files on disk are not deleted.": ("Танланганлар лойиҳадан олиб ташлансинми? Дискдаги файллар ўчирилмайди.", "Убрать выбранное из проекта? Файлы на диске не удаляются."),
    "Remove": ("Олиб ташлаш", "Убрать"),
    "Import...": ("Импорт...", "Импорт..."),
    "Rename": ("Қайта номлаш", "Переименовать"),
    "Relink...": ("Қайта боғлаш...", "Привязать заново..."),
    "Find missing files in folder...": ("Йўқолган файлларни папкадан қидириш...", "Найти потерянные файлы в папке..."),
    "Show in Explorer": ("Explorer’да кўрсатиш", "Показать в Проводнике"),
    "Untitled": ("Номсиз", "Без названия"),
    "different media type": ("медиа тури бошқа", "другой тип медиа"),
    "different duration": ("давомийлиги бошқа", "другая длительность"),
    "not saved yet": ("ҳали сақланмаган", "ещё не сохранён"),
    "Sequence": ("Секвенция", "Эпизод"),
    "Media": ("Медиа", "Медиа"),
    "Video: %1 · Audio: %2 · Images: %3": ("Видео: %1 · Аудио: %2 · Расмлар: %3", "Видео: %1 · Аудио: %2 · Изображения: %3"),
    "Total size: %1": ("Умумий ҳажм: %1", "Общий размер: %1"),
    "Files not found: %1. Right-click them in the media bin and choose Relink.": ("Топилмаган файллар: %1. Медиа кутубхонасида улар устида ўнг тугмани босиб, «Қайта боғлаш»ни танланг.", "Не найдено файлов: %1. Щёлкните по ним правой кнопкой в медиатеке и выберите «Привязать заново»."),
    "Full HD 1080p, 25 fps": ("Full HD 1080p, 25 кадр/с", "Full HD 1080p, 25 кадр/с"),
    "Full HD 1080p, 30 fps": ("Full HD 1080p, 30 кадр/с", "Full HD 1080p, 30 кадр/с"),
    "Full HD 1080p, 50 fps": ("Full HD 1080p, 50 кадр/с", "Full HD 1080p, 50 кадр/с"),
    "HD 720p, 25 fps": ("HD 720p, 25 кадр/с", "HD 720p, 25 кадр/с"),
    "4K UHD, 25 fps": ("4K UHD, 25 кадр/с", "4K UHD, 25 кадр/с"),
    "Vertical 1080x1920, 30 fps (phone)": ("Вертикал 1080x1920, 30 кадр/с (телефон)", "Вертикальное 1080x1920, 30 кадр/с (телефон)"),
    "Square 1080x1080, 30 fps": ("Квадрат 1080x1080, 30 кадр/с", "Квадрат 1080x1080, 30 кадр/с"),
    "Sequence settings": ("Секвенция созламалари", "Настройки эпизода"),
    "Custom": ("Бошқа (қўлда)", "Пользовательский"),
    " px": (" пиксел", " пикс."),
    "44.1 kHz": ("44,1 кГц", "44,1 кГц"),
    "48 kHz": ("48 кГц", "48 кГц"),
    "Mono": ("Моно", "Моно"),
    "Stereo": ("Стерео", "Стерео"),
    "Preset:": ("Тайёр шаблон:", "Шаблон:"),
    "Width:": ("Кенглик:", "Ширина:"),
    "Height:": ("Баландлик:", "Высота:"),
    "Frame rate (fps):": ("Кадрлар частотаси (кадр/с):", "Частота кадров (кадр/с):"),
    "Audio sample rate:": ("Аудио частотаси:", "Частота дискретизации:"),
    "Audio channels:": ("Аудио каналлари:", "Аудиоканалы:"),
    "Clips with a different size or frame rate are fitted to these settings in preview and export (from phase 2).": (
        "Ўлчами ёки кадрлар частотаси бошқа клиплар preview ва экспортда шу созламаларга мослаштирилади (2-босқичдан бошлаб).",
        "Клипы с другим размером или частотой кадров приводятся к этим настройкам в предпросмотре и экспорте (с этапа 2)."),
    # --- phase 2: timeline
    "Drag media from the bin to the timeline.": ("Медиани кутубхонадан timeline’га судраб ташланг.", "Перетащите медиа из медиатеки на таймлайн."),
    "No video at the playhead.": ("Курсор турган жойда видео йўқ.", "Под указателем нет видео."),
    "Phase 2: timeline editing works; the Program monitor shows the frame at the playhead. Playback comes next.": ("2-босқич: timeline’да монтаж ишлайди; Натижа мониторида курсор турган кадр кўринади. Ижро кейинги қадамда.", "Этап 2: монтаж на таймлайне работает; монитор программы показывает кадр под указателем. Воспроизведение — следующий шаг."),
    "Undo: %1": ("Бекор қилиш: %1", "Отменить: %1"),
    "Redo: %1": ("Қайтариш: %1", "Повторить: %1"),
    "Place clips": ("Клипларни жойлаш", "Размещение клипов"),
    "Move clips": ("Клипларни кўчириш", "Перемещение клипов"),
    "Trim clip": ("Клипни қирқиш", "Подрезка клипа"),
    "Split": ("Бўлиш", "Разрезание"),
    "Add video track": ("Видео йўлак қўшиш", "Добавить видеодорожку"),
    "Add audio track": ("Овоз йўлаги қўшиш", "Добавить аудиодорожку"),
    "Track setting": ("Йўлак созламаси", "Настройка дорожки"),
    "Duration: %1": ("Давомийлик: %1", "Длительность: %1"),
    "Clips stick to edges and the playhead (S)": ("Клиплар четларга ва курсорга ёпишади (S)", "Клипы прилипают к краям и указателю (S)"),
    "Select all clips": ("Барча клипларни танлаш", "Выделить все клипы"),
    "Fit": ("Сиғдириш", "Вместить"),
    "Show the whole sequence (\\)": ("Бутун секвенцияни кўрсатиш (\\)", "Показать всю секвенцию (\\)"),
    "Zoom (Ctrl + mouse wheel)": ("Масштаб (Ctrl + сичқонча ғилдираги)", "Масштаб (Ctrl + колесо мыши)"),
    "Split at playhead (Ctrl+K)": ("Курсор жойида бўлиш (Ctrl+K)", "Разрезать по указателю (Ctrl+K)"),
    "Drag media from the bin here": ("Медиани кутубхонадан шу ерга судраб ташланг", "Перетащите сюда медиа из медиатеки"),
    "Unlock track": ("Йўлак қулфини очиш", "Разблокировать дорожку"),
    "Lock track": ("Йўлакни қулфлаш", "Заблокировать дорожку"),
    "Show track in the Program monitor": ("Йўлакни Натижа мониторида кўрсатиш", "Показывать дорожку в мониторе программы"),
    "Hide track in the Program monitor": ("Йўлакни Натижа мониторида яшириш", "Скрыть дорожку в мониторе программы"),
    "Drag media from the bin; clips overwrite what is under them; linked video and audio move together; tracks can be locked and hidden.": ("Медиа кутубхонадан судраб ташланади; клип остидагини устидан ёзади; боғланган видео ва овоз бирга кўчади; йўлакларни қулфлаш ва яшириш мумкин.", "Медиа перетаскивается из медиатеки; клип перезаписывает то, что под ним; связанные видео и звук двигаются вместе; дорожки можно блокировать и скрывать."),
    "Ctrl+Z, Ctrl+Shift+Z, Ctrl+K split, Delete, Shift+Delete ripple delete, S snapping. Edit > Keyboard shortcuts changes any key.": ("Ctrl+Z, Ctrl+Shift+Z, Ctrl+K бўлиш, Delete, Shift+Delete бўшлиқсиз ўчириш, S ёпишиш. Таҳрир > Тезкор тугмалар орқали исталган тугмани ўзгартириш мумкин.", "Ctrl+Z, Ctrl+Shift+Z, Ctrl+K разрезать, Delete, Shift+Delete удалить со сдвигом, S привязка. Правка > Сочетания клавиш меняет любую клавишу."),
    "Shows the frame at the playhead. Real-time playback with sound is the next step.": ("Курсор турган кадр кўрсатилади. Овоз билан реал вақтда ижро кейинги қадамда.", "Показывается кадр под указателем. Воспроизведение со звуком в реальном времени — следующий шаг."),
}

T.update({
    'Full license texts are installed in the "licenses" folder next to the program. Source code: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html': (
        "Лицензияларнинг тўлиқ матни дастур ёнидаги \"licenses\" папкасига ўрнатилади. Манба коди: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html",
        "Полные тексты лицензий установлены в папку \"licenses\" рядом с программой. Исходный код: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html"),
    "The MLT video engine is added in phase 2; its license will be listed here then.": (
        "MLT видео двигатели 2-босқичда қўшилади; унинг лицензияси ўшанда шу ерда кўрсатилади.",
        "Видеодвижок MLT добавляется на этапе 2; тогда его лицензия появится здесь."),
    "Reading media files, thumbnails": ("Медиа файлларни ўқиш, thumbnail", "Чтение медиафайлов, миниатюры"),
    "Phase 1: projects and media bin. Editing features arrive in the next phases; see Help > Feature status.": (
        "1-босқич: лойиҳалар ва медиа кутубхонаси. Монтаж функциялари кейинги босқичларда қўшилади; Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 1: проекты и медиатека. Функции монтажа появятся на следующих этапах; см. Справка > Состояние функций."),
})

# Phase 2b: playback with sound
T.update({
    "Go to start (Home)": ("Бошига ўтиш (Home)", "В начало (Home)"),
    "Previous frame (Left)": ("Олдинги кадр (Чапга)", "Предыдущий кадр (Влево)"),
    "Play (Space)": ("Ижро (Пробел)", "Воспроизвести (Пробел)"),
    "Pause (Space)": ("Тўхтатиш (Пробел)", "Пауза (Пробел)"),
    "Next frame (Right)": ("Кейинги кадр (Ўнгга)", "Следующий кадр (Вправо)"),
    "Go to end (End)": ("Охирига ўтиш (End)", "В конец (End)"),
    "Play": ("Ижро", "Воспроизвести"),
    "Pause": ("Тўхтатиш", "Пауза"),
    "Phase 2: timeline editing and playback with sound work. Text, audio levels and export come next.": (
        "2-босқич: timeline’да монтаж ва овоз билан ижро ишлайди. Матн, овоз даражаси ва экспорт кейинги қадамларда.",
        "Этап 2: монтаж на таймлайне и воспроизведение со звуком работают. Текст, уровни звука и экспорт — следующие шаги."),
    "The video engine (MLT) could not be loaded: playback is off. See the log file.": (
        "Видео двигатели (MLT) юкланмади: ижро ўчирилган. Лог файлига қаранг.",
        "Не удалось загрузить видеодвижок (MLT): воспроизведение отключено. См. файл журнала."),
    "Video engine: timeline playback": ("Видео двигатели: timeline ижроси", "Видеодвижок: воспроизведение таймлайна"),
    "Real-time playback with sound (Space), frame steps, start and end. On a slow computer frames are skipped so sound stays in sync.": (
        "Овоз билан реал вақтда ижро (Пробел), кадрма-кадр юриш, бошига ва охирига ўтиш. Секин компьютерда овоз мос туриши учун баъзи кадрлар ташлаб кетилади.",
        "Воспроизведение со звуком в реальном времени (Пробел), шаг по кадрам, переход в начало и конец. На медленном компьютере кадры пропускаются, чтобы звук не отставал."),
    "Source monitor playback, J/K/L shuttle, in and out points": (
        "Source мониторида ижро, J/K/L, In ва Out нуқталари",
        "Воспроизведение в мониторе источника, J/K/L, точки входа и выхода"),
    "No video yet.\nSource monitor playback is planned for phase 6.": (
        "Видео ҳали йўқ.\nSource мониторида ижро 6-босқичда режалаштирилган.",
        "Видео пока нет.\nВоспроизведение в мониторе источника запланировано на этап 6."),
    "Source monitor playback is planned for phase 6": (
        "Source мониторида ижро 6-босқичда режалаштирилган",
        "Воспроизведение в мониторе источника запланировано на этап 6"),
    "Sound output": ("Овоз чиқариш", "Вывод звука"),
    "Full license texts are installed in the \"licenses\" folder next to the program. Source code: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html, MLT https://github.com/mltframework/mlt, SDL https://libsdl.org/": (
        "Лицензияларнинг тўлиқ матни дастур ёнидаги \"licenses\" папкасига ўрнатилади. Манба коди: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html, MLT https://github.com/mltframework/mlt, SDL https://libsdl.org/",
        "Полные тексты лицензий установлены в папку \"licenses\" рядом с программой. Исходный код: Qt https://download.qt.io/, FFmpeg https://ffmpeg.org/download.html, MLT https://github.com/mltframework/mlt, SDL https://libsdl.org/"),
    "Only the LGPL parts of MLT are shipped; its GPL modules are left out.": (
        "MLT’нинг фақат LGPL қисмлари қўшилган; GPL модуллари киритилмаган.",
        "Поставляются только LGPL-части MLT; его GPL-модули не включены."),
    "Phase 2: timeline editing and playback with sound. Text, audio levels and export arrive in the next phases; see Help > Feature status.": (
        "2-босқич: timeline’да монтаж ва овоз билан ижро. Матн, овоз даражаси ва экспорт кейинги босқичларда қўшилади; Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 2: монтаж на таймлайне и воспроизведение со звуком. Текст, уровни звука и экспорт появятся на следующих этапах; см. Справка > Состояние функций."),
})

# Phase 3: titles and clip audio
T.update({
    " s": (" с", " с"),
    "Add title": ("Титр қўшиш", "Добавить титр"),
    "Audio mixer": ("Овоз микшери", "Аудиомикшер"),
    "Top": ("Юқорида", "Вверху"),
    "Center": ("Ўртада", "По центру"),
    "Bottom": ("Пастда", "Внизу"),
    "Choose the text color": ("Матн рангини танлаш", "Выбрать цвет текста"),
    "Clip volume": ("Клип овози", "Громкость клипа"),
    "Color:": ("Ранг:", "Цвет:"),
    "Dark box behind the text": ("Матн орқасида қора фон", "Тёмная подложка под текстом"),
    "Edit title": ("Титрни таҳрирлаш", "Изменить титр"),
    "Fade in:": ("Аста кириш (fade in):", "Нарастание (fade in):"),
    "Fade out:": ("Аста сўниш (fade out):", "Затухание (fade out):"),
    "Graphics > New text (Ctrl+T); text, size, color, place and a dark box in the Clip panel. Clip volume in dB with fade in and out; M and S buttons on audio tracks. Font choice and animated titles come in later phases.": (
        "Графика > Янги матн (Ctrl+T); матн, ўлчам, ранг, жой ва қора фон «Клип» панелида. Клип овози dB’да, fade in ва fade out; аудио йўлакларда M ва S тугмалари. Шрифт танлаш ва анимацияли титрлар кейинги босқичларда.",
        "Графика > Новый текст (Ctrl+T); текст, размер, цвет, положение и тёмная подложка на панели «Клип». Громкость клипа в дБ, нарастание и затухание; кнопки M и S на аудиодорожках. Выбор шрифта и анимированные титры — на следующих этапах."),
    "Mute track": ("Йўлак овозини ўчириш", "Выключить звук дорожки"),
    "Turn the track sound on": ("Йўлак овозини ёқиш", "Включить звук дорожки"),
    "Phase 3: editing, playback with sound, titles and clip volume. MP4 export arrives in the next phase; see Help > Feature status.": (
        "3-босқич: монтаж, овоз билан ижро, титрлар ва клип овози. MP4 экспорт кейинги босқичда қўшилади; Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 3: монтаж, воспроизведение со звуком, титры и громкость клипов. Экспорт в MP4 появится на следующем этапе; см. Справка > Состояние функций."),
    "Phase 3: editing, playback, titles and clip volume work. MP4 export comes next.": (
        "3-босқич: монтаж, ижро, титрлар ва клип овози ишлайди. Кейинги қадам — MP4 экспорт.",
        "Этап 3: монтаж, воспроизведение, титры и громкость клипов работают. Следующий шаг — экспорт в MP4."),
    "Place:": ("Жойи:", "Положение:"),
    "Position, scale and opacity of video clips arrive in phase 7.": (
        "Видео клипнинг жойи, ўлчами ва шаффофлиги 7-босқичда қўшилади.",
        "Положение, масштаб и прозрачность видеоклипов появятся на этапе 7."),
    "Select a clip on the timeline to see its settings.": (
        "Созламаларини кўриш учун timeline’да клипни танланг.",
        "Выберите клип на таймлайне, чтобы увидеть его параметры."),
    "Several clips are selected. Select one clip.": (
        "Бир нечта клип танланган. Битта клипни танланг.",
        "Выбрано несколько клипов. Выберите один клип."),
    "Size:": ("Ўлчами:", "Размер:"),
    "Solo off": ("Solo’ни ўчириш", "Выключить соло"),
    "Solo: hear only this track (and other solo tracks)": (
        "Solo: фақат шу йўлакни (ва бошқа solo йўлакларни) эшитиш",
        "Соло: слышать только эту дорожку (и другие соло-дорожки)"),
    "Text color": ("Матн ранги", "Цвет текста"),
    "Text": ("Матн", "Текст"),
    "Text:": ("Матн:", "Текст:"),
    "Title": ("Титр", "Титр"),
    "Volume:": ("Овоз баландлиги:", "Громкость:"),
})

# Phase 4: MP4 export
T.update({
    "Browse...": ("Танлаш...", "Обзор..."),
    "Cancel export": ("Экспортни бекор қилиш", "Отменить экспорт"),
    "Choose a file name.": ("Файл номини танланг.", "Выберите имя файла."),
    "Done: %1": ("Тайёр: %1", "Готово: %1"),
    "Export cancelled. The unfinished file was deleted.": (
        "Экспорт бекор қилинди. Тугалланмаган файл ўчирилди.",
        "Экспорт отменён. Незавершённый файл удалён."),
    "Export failed: %1": ("Экспорт бажарилмади: %1", "Экспорт не удался: %1"),
    "Export to MP4 (Ctrl+M)": ("MP4’га экспорт (Ctrl+M)", "Экспорт в MP4 (Ctrl+M)"),
    "Export to MP4": ("MP4’га экспорт", "Экспорт в MP4"),
    "Exporting...": ("Экспорт қилинмоқда...", "Идёт экспорт..."),
    "File > Export (Ctrl+M). H.264 through Intel Quick Sync, Windows Media Foundation, NVIDIA or AMD; MPEG-4 Part 2 when none of them works. Size and frame rate follow the sequence settings; other sizes and an export queue come later.": (
        "Файл > Экспорт (Ctrl+M). H.264 Intel Quick Sync, Windows Media Foundation, NVIDIA ёки AMD орқали; улар ишламаса MPEG-4 Part 2. Ўлчам ва кадр тезлиги секвенция созламаларидан; бошқа ўлчамлар ва экспорт навбати кейинроқ.",
        "Файл > Экспорт (Ctrl+M). H.264 через Intel Quick Sync, Windows Media Foundation, NVIDIA или AMD; MPEG-4 Part 2, если ни один не работает. Размер и частота кадров — из настроек секвенции; другие размеры и очередь экспорта — позже."),
    "File:": ("Файл:", "Файл:"),
    "Frame %1 of %2": ("%2 кадрдан %1", "Кадр %1 из %2"),
    "H.264 is not available on this computer (no Intel Quick Sync or Windows Media Foundation encoder was found). The file will use MPEG-4 Part 2: it plays in most players, but some websites may not accept it.": (
        "Бу компьютерда H.264 мавжуд эмас (Intel Quick Sync ёки Windows Media Foundation кодловчиси топилмади). Файл MPEG-4 Part 2 форматида бўлади: кўп плеерларда очилади, лекин баъзи сайтлар қабул қилмаслиги мумкин.",
        "H.264 на этом компьютере недоступен (не найден кодировщик Intel Quick Sync или Windows Media Foundation). Файл будет в MPEG-4 Part 2: он открывается в большинстве плееров, но некоторые сайты могут его не принять."),
    "High (large file)": ("Юқори (катта файл)", "Высокое (большой файл)"),
    "MP4 video (*.mp4)": ("MP4 видео (*.mp4)", "Видео MP4 (*.mp4)"),
    "Medium": ("Ўрта", "Среднее"),
    "Open folder": ("Папкани очиш", "Открыть папку"),
    "Phase 4: editing, playback, titles, clip volume and MP4 export work.": (
        "4-босқич: монтаж, ижро, титрлар, клип овози ва MP4 экспорт ишлайди.",
        "Этап 4: монтаж, воспроизведение, титры, громкость клипов и экспорт в MP4 работают."),
    "Phase 4: the first complete version: editing, playback, titles, clip volume and MP4 export. See Help > Feature status.": (
        "4-босқич — биринчи тўлиқ версия: монтаж, ижро, титрлар, клип овози ва MP4 экспорт. Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 4 — первая полная версия: монтаж, воспроизведение, титры, громкость клипов и экспорт в MP4. См. Справка > Состояние функций."),
    "Picture: %1, %2 fps (sequence settings), %3 Mbit/s. Sound: AAC 192 kbit/s, %4 Hz. Length %5, file about %6 MB.": (
        "Тасвир: %1, %2 кадр/с (секвенция созламалари), %3 Мбит/с. Овоз: AAC 192 кбит/с, %4 Гц. Давомийлик %5, файл тахминан %6 МБ.",
        "Изображение: %1, %2 кадр/с (настройки секвенции), %3 Мбит/с. Звук: AAC 192 кбит/с, %4 Гц. Длительность %5, файл примерно %6 МБ."),
    "Quality:": ("Сифат:", "Качество:"),
    "Small file": ("Кичик файл", "Маленький файл"),
    "The file already exists. Replace it?": ("Бундай файл бор. Алмаштирилсинми?", "Файл уже существует. Заменить?"),
    "Video codec:": ("Видео кодек:", "Видеокодек:"),
    "about %1 left": ("тахминан %1 қолди", "осталось около %1"),
    "An export is already running.": ("Экспорт аллақачон бораяпти.", "Экспорт уже идёт."),
    "The MP4 writer (MLT avformat) is not available.": ("MP4 ёзувчи қисм (MLT avformat) мавжуд эмас.", "Модуль записи MP4 (MLT avformat) недоступен."),
    "The export could not start.": ("Экспорт бошланмади.", "Не удалось начать экспорт."),
    "The file was not written. Check the folder and free disk space.": (
        "Файл ёзилмади. Папкани ва дискдаги бўш жойни текширинг.",
        "Файл не записан. Проверьте папку и свободное место на диске."),
    "The sequence could not be prepared for export.": ("Секвенцияни экспортга тайёрлаб бўлмади.", "Не удалось подготовить секвенцию к экспорту."),
    "The sequence is empty.": ("Секвенция бўш.", "Секвенция пуста."),
    "The video engine (MLT) is not loaded.": ("Видео двигатели (MLT) юкланмаган.", "Видеодвижок (MLT) не загружен."),
})

# Phase 5 (part 1): autosave, recovery, versions, waveforms
T.update({
    "Phase 5: editing, playback, titles, clip volume, MP4 export, autosave with crash recovery and audio waveforms. See Help > Feature status.": (
        "5-босқич: монтаж, ижро, титрлар, клип овози, MP4 экспорт, автосақлаш ва бузилишдан кейин тиклаш, овоз тўлқини. Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 5: монтаж, воспроизведение, титры, громкость клипов, экспорт в MP4, автосохранение с восстановлением после сбоя и формы звуковой волны. См. Справка > Состояние функций."),
    "Phase 5: editing, playback, titles, MP4 export, autosave and waveforms work.": (
        "5-босқич: монтаж, ижро, титрлар, MP4 экспорт, автосақлаш ва овоз тўлқини ишлайди.",
        "Этап 5: монтаж, воспроизведение, титры, экспорт в MP4, автосохранение и формы волны работают."),
    "Open this version": ("Шу версияни очиш", "Открыть эту версию"),
    "Previous versions": ("Олдинги версиялар", "Предыдущие версии"),
    "Previous versions...": ("Олдинги версиялар...", "Предыдущие версии..."),
    "No previous versions yet. Each time you save, the old file is kept in the \"Backups\" folder next to the project.": (
        "Ҳали олдинги версиялар йўқ. Ҳар сақлаганингизда эски файл лойиҳа ёнидаги «Backups» папкасида қолади.",
        "Предыдущих версий пока нет. При каждом сохранении старый файл остаётся в папке «Backups» рядом с проектом."),
    "Each save keeps the old file in the \"Backups\" folder next to the project. An opened version replaces the project only when you save it.": (
        "Ҳар сақлашда эски файл лойиҳа ёнидаги «Backups» папкасида қолади. Очилган версия фақат сиз сақлаганингизда лойиҳанинг ўрнини эгаллайди.",
        "При каждом сохранении старый файл остаётся в папке «Backups» рядом с проектом. Открытая версия заменит проект, только когда вы его сохраните."),
    "Audio waveforms on the timeline": ("Timeline'да овоз тўлқини (waveform)", "Формы звуковой волны на таймлайне"),
    "Computed in the background once per file and kept in the cache; follow the clip volume.": (
        "Ҳар файл учун бир марта фонда ҳисобланади ва кэшда сақланади; клип овози ўзгарса, тўлқин ҳам ўзгаради.",
        "Рассчитываются в фоне один раз для каждого файла и хранятся в кэше; учитывают громкость клипа."),
    "Proxy files, cache management, collecting a project into one folder": (
        "Proxy файллар, кэшни бошқариш, лойиҳани битта папкага йиғиш",
        "Прокси-файлы, управление кэшем, сбор проекта в одну папку"),
    "Autosave, crash recovery, previous versions": (
        "Автосақлаш, бузилишдан кейин тиклаш, олдинги версиялар",
        "Автосохранение, восстановление после сбоя, предыдущие версии"),
    "A copy is written every 2 minutes while there are unsaved changes (File > Settings). After a crash the next start offers to recover it. Each save keeps the old file in the Backups folder; File > Previous versions opens one.": (
        "Сақланмаган ўзгаришлар бўлса, ҳар 2 дақиқада нусха ёзилади (Файл > Созламалар). Дастур кутилмаганда ёпилса, кейинги очилишда уни тиклаш таклиф қилинади. Ҳар сақлашда эски файл Backups папкасида қолади; Файл > Олдинги версиялар орқали очилади.",
        "Пока есть несохранённые изменения, копия записывается каждые 2 минуты (Файл > Настройки). После сбоя при следующем запуске предлагается её восстановить. При каждом сохранении старый файл остаётся в папке Backups; Файл > Предыдущие версии открывает его."),
    "Autosaved at %1": ("%1 да автосақланди", "Автосохранено в %1"),
    "Autosave failed: %1": ("Автосақлаб бўлмади: %1", "Ошибка автосохранения: %1"),
    "An earlier version is open. Save to make it the current project.": (
        "Олдинги версия очилди. Уни асосий лойиҳа қилиш учун сақланг.",
        "Открыта предыдущая версия. Сохраните, чтобы сделать её текущим проектом."),
    "Recover unsaved work": ("Сақланмаган ишни тиклаш", "Восстановление несохранённой работы"),
    "Unsaved work recovered. Save the project to keep it.": (
        "Сақланмаган иш тикланди. Йўқолмаслиги учун лойиҳани сақланг.",
        "Несохранённая работа восстановлена. Сохраните проект, чтобы её не потерять."),
    "Recover": ("Тиклаш", "Восстановить"),
    "BUKHARA CINEMA was not closed normally last time. These autosaved copies contain changes that were not saved. Recover one to continue working; then save it.": (
        "Ўтган сафар BUKHARA CINEMA одатдагидек ёпилмади. Бу автосақланган нусхаларда сақланмаган ўзгаришлар бор. Ишни давом эттириш учун бирини тикланг, кейин сақланг.",
        "В прошлый раз BUKHARA CINEMA была закрыта некорректно. В этих автосохранённых копиях есть несохранённые изменения. Восстановите одну, чтобы продолжить работу, затем сохраните."),
    "Delete this copy? The unsaved changes in it will be lost.": (
        "Бу нусха ўчирилсинми? Ундаги сақланмаган ўзгаришлар йўқолади.",
        "Удалить эту копию? Несохранённые изменения в ней будут потеряны."),
    "never saved": ("ҳали сақланмаган", "ещё не сохранялся"),
    "Autosave:": ("Автосақлаш:", "Автосохранение:"),
    "Off": ("Ўчирилган", "Выключено"),
    "Every %1 min": ("Ҳар %1 дақиқада", "Каждые %1 мин"),
    "Previous versions to keep:": ("Сақланадиган олдинги версиялар:", "Хранить предыдущих версий:"),
    "None": ("Йўқ", "Нет"),
    "Each save keeps the old project file in the \"Backups\" folder next to it.": (
        "Ҳар сақлашда эски лойиҳа файли ёнидаги «Backups» папкасида қолади.",
        "При каждом сохранении старый файл проекта остаётся в папке «Backups» рядом с ним."),
})

# Phase 5 (part 2): proxies, media cache, collect project
T.update({
    "4K and other big videos then play smoothly; export still uses the originals.": (
        "Шунда 4K ва бошқа катта видеолар равон ижро бўлади; экспорт барибир асл файллардан.",
        "Тогда 4K и другие большие видео воспроизводятся плавно; экспорт всё равно из оригиналов."),
    "A light copy for smooth playback; export still uses the original file.": (
        "Равон ижро учун енгил нусха; экспорт барибир асл файлдан.",
        "Лёгкая копия для плавного воспроизведения; экспорт всё равно из оригинала."),
    "Audio waveforms": ("Овоз тўлқинлари", "Формы звуковой волны"),
    "Collect project": ("Лойиҳани йиғиш", "Сбор проекта"),
    "Collect project...": ("Лойиҳани йиғиш...", "Собрать проект..."),
    "Collect": ("Йиғиш", "Собрать"),
    "Copies the project file and its media into one new folder, for example to move it to another computer or to archive it. The open project does not change.": (
        "Лойиҳа файли ва унинг медиасини битта янги папкага нусхалайди, масалан, бошқа компьютерга кўчириш ёки архивлаш учун. Очиқ лойиҳа ўзгармайди.",
        "Копирует файл проекта и его медиа в одну новую папку, например, чтобы перенести на другой компьютер или в архив. Открытый проект не меняется."),
    "Copy the project and its media into one folder": ("Лойиҳа ва медиани битта папкага нусхалаш", "Скопировать проект и медиа в одну папку"),
    "Could not collect the project: %1": ("Лойиҳани йиғиб бўлмади: %1", "Не удалось собрать проект: %1"),
    "Could not make a proxy: %1": ("Proxy тайёрлаб бўлмади: %1", "Не удалось создать прокси: %1"),
    "Create proxy": ("Proxy тайёрлаш", "Создать прокси"),
    "Delete all proxy videos? Big videos will play less smoothly until new proxies are made.": (
        "Барча proxy видеолар ўчирилсинми? Янги proxy тайёрлангунча катта видеолар секинроқ ижро бўлади.",
        "Удалить все прокси-видео? Пока не будут созданы новые, большие видео будут воспроизводиться менее плавно."),
    "Delete proxy": ("Proxy'ни ўчириш", "Удалить прокси"),
    "Destination folder": ("Мақсад папка", "Папка назначения"),
    "Destination folder:": ("Қаерга:", "Куда:"),
    "File > Media cache shows and deletes thumbnails, waveforms and proxies. File > Collect project copies the project and its media into a new folder.": (
        "Файл > Медиа кэши: thumbnail, waveform ва proxy ҳажмини кўриш ва ўчириш. Файл > Лойиҳани йиғиш: лойиҳа ва медиани янги папкага нусхалайди.",
        "Файл > Кэш медиа показывает и удаляет миниатюры, формы волны и прокси. Файл > Собрать проект копирует проект и медиа в новую папку."),
    "Files to copy: %1 (%2).": ("Нусхаланадиган файллар: %1 (%2).", "Файлов для копирования: %1 (%2)."),
    "Make proxies for videos larger than Full HD automatically": (
        "Full HD'дан катта видеоларга proxy'ни автоматик тайёрлаш",
        "Автоматически создавать прокси для видео больше Full HD"),
    "Making proxy: %1% (%2 more)": ("Proxy тайёрланяпти: %1% (яна %2 та)", "Создание прокси: %1% (ещё %2)"),
    "Making proxy: %1%": ("Proxy тайёрланяпти: %1%", "Создание прокси: %1%"),
    "Media cache and collecting a project into one folder": ("Медиа кэши ва лойиҳани битта папкага йиғиш", "Кэш медиа и сбор проекта в одну папку"),
    "Media cache": ("Медиа кэши", "Кэш медиа"),
    "Media cache...": ("Медиа кэши...", "Кэш медиа..."),
    "Media not used in the sequence left out: %1.": ("Секвенцияда ишлатилмаган медиа қўшилмади: %1.", "Медиа, не используемые в секвенции, пропущены: %1."),
    "Missing files that cannot be copied: %1.": ("Топилмаган ва нусхаланмайдиган файллар: %1.", "Отсутствующие файлы, которые нельзя скопировать: %1."),
    "New project: %1": ("Янги лойиҳа: %1", "Новый проект: %1"),
    "Only media used in the sequence": ("Фақат секвенцияда ишлатилган медиа", "Только медиа, используемые в секвенции"),
    "Open collected project": ("Йиғилган лойиҳани очиш", "Открыть собранный проект"),
    "Phase 5: editing, playback, titles, MP4 export, autosave, waveforms and proxies work.": (
        "5-босқич: монтаж, ижро, титрлар, MP4 экспорт, автосақлаш, овоз тўлқини ва proxy ишлайди.",
        "Этап 5: монтаж, воспроизведение, титры, экспорт в MP4, автосохранение, формы волны и прокси работают."),
    "Phase 5: editing, playback, titles, clip volume, MP4 export, autosave with crash recovery, audio waveforms and proxies. See Help > Feature status.": (
        "5-босқич: монтаж, ижро, титрлар, клип овози, MP4 экспорт, автосақлаш ва бузилишдан кейин тиклаш, овоз тўлқини ва proxy. Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 5: монтаж, воспроизведение, титры, громкость клипов, экспорт в MP4, автосохранение с восстановлением после сбоя, формы волны и прокси. См. Справка > Состояние функций."),
    "Play big videos from their light proxy copies. Export always uses the originals.": (
        "Катта видеоларни енгил proxy нусхаларидан ижро қилиш. Экспорт ҳар доим асл файллардан.",
        "Воспроизводить большие видео из лёгких прокси-копий. Экспорт всегда из оригиналов."),
    "Proxy files for smooth playback of big videos": ("Катта видеоларни равон ижро қилиш учун proxy файллар", "Прокси-файлы для плавного воспроизведения больших видео"),
    "Proxy videos": ("Proxy видеолар", "Прокси-видео"),
    "Stop making proxies": ("Proxy тайёрлашни тўхтатиш", "Остановить создание прокси"),
    "The program keeps thumbnails, audio waveforms and proxy videos made from your media in a cache folder. Deleting them is safe: they are made again when needed. Your media files and projects are not touched.": (
        "Дастур медиадан тайёрланган thumbnail, овоз тўлқини ва proxy видеоларни кэш папкасида сақлайди. Уларни ўчириш хавфсиз: керак бўлганда қайта тайёрланади. Медиа файллар ва лойиҳаларингизга тегилмайди.",
        "Программа хранит миниатюры, формы волны и прокси-видео, созданные из ваших медиа, в папке кэша. Удалять их безопасно: при необходимости они создаются заново. Ваши медиафайлы и проекты не затрагиваются."),
    "This folder already exists: choose another destination.": ("Бу папка аллақачон бор: бошқа жойни танланг.", "Эта папка уже существует: выберите другое место."),
    "Thumbnails": ("Thumbnail'лар", "Миниатюры"),
    "Total: %1 in %2": ("Жами: %1, папка: %2", "Всего: %1 в %2"),
    "Use proxies for playback": ("Ижрода proxy ишлатиш", "Использовать прокси при воспроизведении"),
    "Videos larger than Full HD get a 540p proxy in the background (File > Settings); any video can get one from the media bin menu. Sequence > Use proxies for playback switches them; export always uses the originals.": (
        "Full HD'дан катта видеоларга фонда 540p proxy тайёрланади (Файл > Созламалар); исталган видеога медиа кутубхонаси менюсидан ҳам. Секвенция > Ижрода proxy ишлатиш орқали ёқилади/ўчирилади; экспорт ҳар доим асл файллардан.",
        "Для видео больше Full HD в фоне создаётся прокси 540p (Файл > Настройки); для любого видео — из меню медиатеки. Секвенция > Использовать прокси при воспроизведении включает их; экспорт всегда из оригиналов."),
    "making proxy...": ("proxy тайёрланяпти...", "создаётся прокси..."),
    "proxy": ("proxy бор", "есть прокси"),
})


# Manual Latin overrides where transliteration is not ideal.
# Phase 6 (part 1): Source monitor, In/Out, insert/overwrite, J/K/L, markers
T.update({
    "Clear In and Out (Source)": ("In ва Out'ни олиб ташлаш (Source)", "Снять In и Out (Source)"),
    "Delete marker": ("Маркерни ўчириш", "Удалить маркер"),
    "Double-click a clip in the media bin to watch it here.": (
        "Бу ерда кўриш учун медиа кутубхонасида клипни икки марта босинг.",
        "Дважды щёлкните клип в медиатеке, чтобы посмотреть его здесь."),
    "End of the part to use (O)": ("Ишлатиладиган қисмнинг охири (O)", "Конец используемой части (O)"),
    "Go to previous marker": ("Олдинги маркерга ўтиш", "К предыдущему маркеру"),
    "Insert from Source": ("Source'дан қўйиш (Insert)", "Вставка из Source"),
    "Insert the source In-Out part at the playhead; later clips move right (,)": (
        "Source'нинг In-Out қисмини курсор жойига қўйиш; кейинги клиплар ўнгга сурилади (,)",
        "Вставить часть In-Out из Source в позицию курсора; следующие клипы сдвигаются вправо (,)"),
    "Insert": ("Қўйиш", "Вставка"),
    "Mark In (Source)": ("In белгиси (Source)", "Отметить In (Source)"),
    "Mark Out (Source)": ("Out белгиси (Source)", "Отметить Out (Source)"),
    "Marker name:": ("Маркер номи:", "Имя маркера:"),
    "No picture (sound only).": ("Тасвир йўқ (фақат овоз).", "Нет изображения (только звук)."),
    "No video yet.": ("Ҳали видео йўқ.", "Видео пока нет."),
    "Nothing to insert: open a clip in the Source monitor, and keep one video and one audio track unlocked.": (
        "Қўйиладиган нарса йўқ: Source мониторида клип очинг ва камида битта видео ҳамда аудио йўлакни қулфламанг.",
        "Нечего вставлять: откройте клип в мониторе Source и оставьте хотя бы одну видео- и аудиодорожку незаблокированной."),
    "Open in Source monitor": ("Source мониторида очиш", "Открыть в мониторе Source"),
    "Overwrite from Source": ("Source'дан устидан ёзиш (Overwrite)", "Перезапись из Source"),
    "Overwrite": ("Устидан ёзиш", "Перезапись"),
    "Pause (Space, K)": ("Тўхтатиш (Пробел, K)", "Пауза (Пробел, K)"),
    "Phase 6 (part 1): Source monitor, In/Out, insert/overwrite, J/K/L and markers work.": (
        "6-босқич (1-қисм): Source монитор, In/Out, insert/overwrite, J/K/L ва маркерлар ишлайди.",
        "Этап 6 (часть 1): монитор Source, In/Out, вставка/перезапись, J/K/L и маркеры работают."),
    "Phase 6 (part 1): editing, playback, Source monitor with In/Out, insert/overwrite, markers, titles, clip volume, MP4 export, autosave, waveforms and proxies. See Help > Feature status.": (
        "6-босқич (1-қисм): монтаж, ижро, In/Out билан Source монитор, insert/overwrite, маркерлар, титрлар, клип овози, MP4 экспорт, автосақлаш, овоз тўлқини ва proxy. Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 6 (часть 1): монтаж, воспроизведение, монитор Source с In/Out, вставка/перезапись, маркеры, титры, громкость клипов, экспорт в MP4, автосохранение, формы волны и прокси. См. Справка > Состояние функций."),
    "Play (Space, L)": ("Ижро (Пробел, L)", "Воспроизвести (Пробел, L)"),
    "Play backwards (J, again: faster)": ("Орқага ўйнатиш (J, яна босилса тезроқ)", "Назад (J, ещё раз: быстрее)"),
    "Play forwards (L, again: faster)": ("Олдинга ўйнатиш (L, яна босилса тезроқ)", "Вперёд (L, ещё раз: быстрее)"),
    "Put the source In-Out part at the playhead over what is there (.)": (
        "Source'нинг In-Out қисмини курсор жойидагининг устидан ёзиш (.)",
        "Поставить часть In-Out из Source в позицию курсора поверх того, что там есть (.)"),
    "Rename marker": ("Маркерни номлаш", "Переименовать маркер"),
    "Rename marker...": ("Маркерни номлаш...", "Переименовать маркер..."),
    "Start of the part to use (I)": ("Ишлатиладиган қисмнинг боши (I)", "Начало используемой части (I)"),
    "Stop (K)": ("Тўхташ (K)", "Стоп (K)"),
    "Insert and overwrite edits, timeline markers": (
        "Insert ва overwrite, timeline маркерлари", "Вставка и перезапись, маркеры таймлайна"),
    "Comma inserts the source part at the playhead and moves later clips right; period puts it over what is there. M adds a marker, Shift+M and Ctrl+Shift+M jump between markers; markers are saved in the project.": (
        "Вергул Source қисмини курсор жойига қўйиб, кейинги клипларни ўнгга суради; нуқта борини устидан ёзади. M маркер қўяди, Shift+M ва Ctrl+Shift+M маркерлар орасида юради; маркерлар лойиҳада сақланади.",
        "Запятая вставляет часть из Source в позицию курсора и сдвигает следующие клипы вправо; точка ставит её поверх. M добавляет маркер, Shift+M и Ctrl+Shift+M переходят между маркерами; маркеры сохраняются в проекте."),
    "Double-click a clip in the media bin. I and O mark the part to use (kept while the program runs). J, K and L play backwards, stop and play forwards; pressing J or L again plays faster (up to 8x). Backwards playback has no sound.": (
        "Медиа кутубхонасида клипни икки марта босинг. I ва O ишлатиладиган қисмни белгилайди (дастур ёпилгунча эслаб қолинади). J, K, L: орқага, тўхташ, олдинга; J ёки L яна босилса тезроқ (8 баравархача). Орқага ижрода овоз йўқ.",
        "Дважды щёлкните клип в медиатеке. I и O отмечают используемую часть (хранятся, пока программа открыта). J, K и L: назад, стоп, вперёд; повторное нажатие J или L ускоряет (до 8x). Назад без звука."),
    "Roll, slip and slide trims, copy and paste, link and unlink, nesting, custom shortcuts": (
        "Roll, slip, slide, нусхалаш ва қўйиш, боғлаш ва ажратиш, nested sequence, тугмаларни ўзгартириш",
        "Roll, slip, slide, копирование и вставка, связь и разъединение, вложенные секвенции, свои сочетания клавиш"),
})

# Phase 6 (part 2): razor, roll, slip, slide, clipboard, link
T.update({
    "Click a clip to split it there (C)": ("Клипни босилган жойдан бўлиш (C)", "Щёлкните клип, чтобы разрезать его там (C)"),
    "Drag a clip between its neighbours; they get longer or shorter (U)": (
        "Клипни қўшниларининг орасида суриш; улар узаяди ёки қисқаради (U)",
        "Сдвиг клипа между соседями; они удлиняются или укорачиваются (U)"),
    "Drag inside a clip to use another part of its source in the same place (Y)": (
        "Клип ичида суриш: шу жойда манбанинг бошқа қисми (Y)",
        "Тяните внутри клипа: другая часть источника на том же месте (Y)"),
    "Drag the cut between two clips: one gets longer, the other shorter (N)": (
        "Икки клип орасидаги кесимни суриш: бири узаяди, бошқаси қисқаради (N)",
        "Сдвиг стыка двух клипов: один удлиняется, другой укорачивается (N)"),
    "Link audio and video": ("Видео ва овозни боғлаш", "Связать видео и звук"),
    "Link": ("Боғлаш", "Связать"),
    "Paste at the playhead over what is there (Ctrl+V)": (
        "Курсор жойига, борининг устидан қўйиш (Ctrl+V)", "Вставить в позицию курсора поверх (Ctrl+V)"),
    "Paste at the playhead; later clips move right (Ctrl+Shift+V)": (
        "Курсор жойига қўйиш; кейинги клиплар ўнгга сурилади (Ctrl+Shift+V)",
        "Вставить в позицию курсора; следующие клипы сдвигаются вправо (Ctrl+Shift+V)"),
    "Paste insert": ("Суриб қўйиш", "Вставить со сдвигом"),
    "Phase 6 (part 2): roll, slip, slide, razor, copy and paste, link and unlink work.": (
        "6-босқич (2-қисм): roll, slip, slide, пичоқ, нусхалаш ва қўйиш, боғлаш ва ажратиш ишлайди.",
        "Этап 6 (часть 2): roll, slip, slide, лезвие, копирование и вставка, связь и разъединение работают."),
    "Phase 6 (part 2): editing with razor, roll, slip and slide, copy and paste, Source monitor with In/Out, insert/overwrite, markers, titles, clip volume, MP4 export, autosave, waveforms and proxies. See Help > Feature status.": (
        "6-босқич (2-қисм): пичоқ, roll, slip, slide билан монтаж, нусхалаш ва қўйиш, In/Out билан Source монитор, insert/overwrite, маркерлар, титрлар, клип овози, MP4 экспорт, автосақлаш, овоз тўлқини ва proxy. Ёрдам > Функциялар ҳолати бўлимига қаранг.",
        "Этап 6 (часть 2): монтаж лезвием, roll, slip и slide, копирование и вставка, монитор Source с In/Out, вставка/перезапись, маркеры, титры, громкость клипов, экспорт в MP4, автосохранение, формы волны и прокси. См. Справка > Состояние функций."),
    "Razor": ("Пичоқ", "Лезвие"),
    "Rolling edit": ("Roll (кесимни суриш)", "Roll (сдвиг стыка)"),
    "Select, move and trim clips (V)": ("Клипларни танлаш, кўчириш ва қисқартириш (V)", "Выделение, перемещение и подрезка клипов (V)"),
    "Selection": ("Танлаш", "Выделение"),
    "Slide clip": ("Клипни суриш (slide)", "Сдвиг клипа (slide)"),
    "Slide": ("Slide", "Slide"),
    "Slip clip": ("Клип ичини суриш (slip)", "Сдвиг содержимого (slip)"),
    "Slip": ("Slip", "Slip"),
    "Timeline tools": ("Timeline асбоблари", "Инструменты таймлайна"),
    "Unlink audio and video": ("Видео ва овозни ажратиш", "Разъединить видео и звук"),
    "Unlink": ("Ажратиш", "Разъединить"),
    "Razor, rolling edit, slip and slide tools; copy, cut and paste; link and unlink": (
        "Пичоқ, roll, slip ва slide асбоблари; нусхалаш, кесиш ва қўйиш; боғлаш ва ажратиш",
        "Лезвие, roll, slip и slide; копирование, вырезание и вставка; связь и разъединение"),
    "Tools above the timeline or keys V, C, N, Y, U. Ctrl+C, Ctrl+X, Ctrl+V paste at the playhead (Ctrl+Shift+V pushes later clips right). Ctrl+L links or unlinks the selected video and sound.": (
        "Timeline устидаги тугмалар ёки V, C, N, Y, U. Ctrl+C, Ctrl+X, Ctrl+V курсор жойига қўяди (Ctrl+Shift+V кейинги клипларни ўнгга суради). Ctrl+L танланган видео ва овозни боғлайди ёки ажратади.",
        "Кнопки над таймлайном или клавиши V, C, N, Y, U. Ctrl+C, Ctrl+X, Ctrl+V вставляют в позицию курсора (Ctrl+Shift+V сдвигает следующие клипы вправо). Ctrl+L связывает или разъединяет выбранные видео и звук."),
    "Nested sequences and custom keyboard shortcuts": (
        "Nested sequence ва тугмаларни ўзгартириш", "Вложенные секвенции и свои сочетания клавиш"),
})

T.update({
    "Phase 6 done: Source monitor, insert/overwrite, trim tools, nested sequences and custom shortcuts work.": (
        "6-босқич тайёр: Source монитор, insert/overwrite, trim асбоблари, nested sequence ва тугмаларни ўзгартириш ишлайди.",
        "Этап 6 готов: монитор источника, вставка/замена, инструменты подрезки, вложенные секвенции и свои сочетания клавиш работают."),
    "Nest": ("Nest (ички секвенция)", "Вложить (Nest)"),
    "Nested sequence %1": ("Ички секвенция %1", "Вложенная секвенция %1"),
    "Main sequence": ("Асосий секвенция", "Основная секвенция"),
    "Rename sequence": ("Секвенция номини ўзгартириш", "Переименовать секвенцию"),
    "Sequence name:": ("Секвенция номи:", "Имя секвенции:"),
    "Rename...": ("Номини ўзгартириш...", "Переименовать..."),
    "Rename the nested sequence shown on the timeline": (
        "Timeline'да кўрсатилган ички секвенция номини ўзгартириш",
        "Переименовать вложенную секвенцию, показанную на таймлайне"),
    "Put the selected clips into a new nested sequence that works as one clip": (
        "Танланган клипларни битта клип каби ишлайдиган янги ички секвенцияга жойлаш",
        "Поместить выбранные клипы в новую вложенную секвенцию, которая работает как один клип"),
    "No key": ("Тугмасиз", "Без клавиши"),
    "Default": ("Асл ҳолати", "По умолчанию"),
    "Keyboard shortcuts": ("Тезкор тугмалар", "Сочетания клавиш"),
    "Search commands": ("Буйруқларни қидириш", "Поиск команд"),
    "Command": ("Буйруқ", "Команда"),
    "Keys": ("Тугмалар", "Клавиши"),
    "Reset all": ("Ҳаммасини асл ҳолига", "Сбросить все"),
    "Press the new keys:": ("Янги тугмаларни босинг:", "Нажмите новые клавиши:"),
    "%1 was used by: %2. It was removed there.": (
        "%1 аввал «%2» учун эди. У ердан олиб ташланди.",
        "%1 использовалось для «%2». Там оно удалено."),
    "Edit > Keyboard shortcuts changes any key.": (
        "Таҳрир > Тезкор тугмалар орқали исталган тугмани ўзгартириш мумкин.",
        "Правка > Сочетания клавиш меняет любую клавишу."),
    "Select clips and choose Clip > Nest (also in the clip right-click menu): they become one clip. Double-click it to edit inside; the list above the timeline goes back to the main sequence. Edit > Keyboard shortcuts changes the key of any command; keys are kept in the settings.": (
        "Клипларни танлаб Клип > Nest танланг (клипдаги ўнг тугма менюсида ҳам бор): улар битта клипга айланади. Ичида таҳрирлаш учун уни икки марта босинг; timeline устидаги рўйхат асосий секвенцияга қайтаради. Таҳрир > Тезкор тугмалар исталган буйруқ тугмасини ўзгартиради; тугмалар созламаларда сақланади.",
        "Выберите клипы и Клип > Вложить (есть и в меню правой кнопки): они станут одним клипом. Двойной щелчок открывает его для правки; список над таймлайном возвращает к основной секвенции. Правка > Сочетания клавиш меняет клавишу любой команды; клавиши хранятся в настройках."),
})

T.update({
    "Keyframes: %1. Changes set a keyframe at the playhead.": (
        "Keyframe'лар: %1. Ўзгаришлар playhead жойига keyframe қўяди.",
        "Ключевых кадров: %1. Изменения ставят ключевой кадр под указателем."),
    "Add or remove a keyframe at the playhead": (
        "Playhead жойига keyframe қўшиш ёки олиб ташлаш",
        "Добавить или убрать ключевой кадр под указателем"),
    "Animate motion": ("Ҳаракатни анимация қилиш", "Анимировать движение"),
    "Animate": ("Анимация", "Анимация"),
    "Keyframe": ("Keyframe", "Ключевой кадр"),
    "Keyframes: the values change over time. Changing a value sets a keyframe at the playhead.": (
        "Keyframe'лар: қийматлар вақт ўтиши билан ўзгаради. Қийматни ўзгартирсангиз, playhead жойига keyframe қўйилади.",
        "Ключевые кадры: значения меняются со временем. Изменение значения ставит ключевой кадр под указателем."),
    "Motion": ("Ҳаракат", "Движение"),
    "Move the playhead over the clip to change its keyframes.": (
        "Keyframe'ларни ўзгартириш учун playhead'ни клип устига олиб келинг.",
        "Чтобы менять ключевые кадры, поставьте указатель на клип."),
    "Next keyframe": ("Кейинги keyframe", "Следующий ключевой кадр"),
    "Previous keyframe": ("Олдинги keyframe", "Предыдущий ключевой кадр"),
    "Opacity:": ("Шаффофлик:", "Непрозрачность:"),
    "Position X:": ("Жой X:", "Позиция X:"),
    "Position Y:": ("Жой Y:", "Позиция Y:"),
    "Rotation:": ("Бурилиш:", "Поворот:"),
    "Scale:": ("Ўлчам:", "Масштаб:"),
    "Reset": ("Тиклаш", "Сбросить"),
    "Reset motion": ("Ҳаракатни тиклаш", "Сброс движения"),
    "Stop animating motion": ("Ҳаракат анимациясини ўчириш", "Отключить анимацию движения"),
    "Put the picture back in place: no move, scale, rotation or keyframes": (
        "Расмни жойига қайтариш: суриш, ўлчам, бурилиш ва keyframe'ларсиз",
        "Вернуть картинку на место: без сдвига, масштаба, поворота и ключевых кадров"),
    "Phase 7, part 1: position, scale, rotation and opacity of clips with keyframes work (Clip panel).": (
        "7-босқич, 1-қисм: клип жойи, ўлчами, бурилиши ва шаффофлиги keyframe'лар билан ишлайди («Клип» панели).",
        "Этап 7, часть 1: позиция, масштаб, поворот и непрозрачность клипов с ключевыми кадрами работают (панель «Клип»)."),
    "Position, scale, rotation and opacity of clips, with keyframes": (
        "Клип жойи, ўлчами, бурилиши ва шаффофлиги, keyframe'лар билан",
        "Позиция, масштаб, поворот и непрозрачность клипов, с ключевыми кадрами"),
    "Select a video clip or title: the Motion group of the Clip panel. Animate turns keyframes on; then each change sets a keyframe at the playhead and the values change smoothly between keyframes (diamonds on the clip). Reset puts the picture back. Values are typed in the panel; dragging the picture in the monitor comes later.": (
        "Видео клип ёки титрни танланг: «Клип» панелидаги «Ҳаракат» бўлими. «Анимация» keyframe'ларни ёқади; кейин ҳар ўзгариш playhead жойига keyframe қўяди ва қийматлар keyframe'лар орасида равон ўзгаради (клипда ромбчалар). «Тиклаш» расмни жойига қайтаради. Қийматлар панелда ёзилади; мониторда суриб ўзгартириш кейинроқ.",
        "Выберите видеоклип или титр: группа «Движение» панели «Клип». «Анимация» включает ключевые кадры; затем каждое изменение ставит ключевой кадр под указателем, и значения плавно меняются между ними (ромбики на клипе). «Сбросить» возвращает картинку. Значения вводятся в панели; перетаскивание картинки в мониторе — позже."),
    "Speed changes arrive in the next part of phase 7.": (
        "Тезликни ўзгартириш 7-босқичнинг кейинги қисмида.",
        "Изменение скорости — в следующей части этапа 7."),
    "Speed changes": ("Тезликни ўзгартириш", "Изменение скорости"),
})

T.update({
    "Clip > Speed / duration (Ctrl+R): 10% to 1000%, or a new duration. Linked sound changes with the picture (its pitch too). Shift the clips after it moves later clips. Reverse playback, speed ramps and pitch-kept sound come later.": (
        "Клип > Тезлик / давомийлик (Ctrl+R): 10% дан 1000% гача ёки янги давомийлик. Боғланган овоз ҳам расм билан бирга ўзгаради (тони ҳам). «Кейинги клипларни суриш» кейинги клипларни суради. Орқага ижро, аста-секин тезлик ўзгариши ва тонни сақлаш кейинроқ.",
        "Клип > Скорость / длительность (Ctrl+R): от 10% до 1000% или новая длительность. Связанный звук меняется вместе с картинкой (и его высота тона). «Сдвинуть следующие клипы» сдвигает последующие клипы. Обратное воспроизведение, плавное изменение скорости и сохранение тона — позже."),
    "Clip speed / duration": ("Клип тезлиги / давомийлиги", "Скорость / длительность клипа"),
    "Duration:": ("Давомийлик:", "Длительность:"),
    "Phase 7 done: clip motion with keyframes (Clip panel) and speed changes (Ctrl+R) work.": (
        "7-босқич тайёр: клип ҳаракати keyframe'лар билан («Клип» панели) ва тезликни ўзгартириш (Ctrl+R) ишлайди.",
        "Этап 7 готов: движение клипов с ключевыми кадрами (панель «Клип») и изменение скорости (Ctrl+R) работают."),
    "Play the clip faster or slower (Ctrl+R)": ("Клипни тезроқ ёки секинроқ ижро қилиш (Ctrl+R)", "Воспроизводить клип быстрее или медленнее (Ctrl+R)"),
    "Shift the clips after it": ("Кейинги клипларни суриш", "Сдвинуть следующие клипы"),
    "Speed": ("Тезлик", "Скорость"),
    "Speed: Clip > Speed / duration (Ctrl+R).": ("Тезлик: Клип > Тезлик / давомийлик (Ctrl+R).", "Скорость: Клип > Скорость / длительность (Ctrl+R)."),
    "Speed:": ("Тезлик:", "Скорость:"),
    "The sound plays higher or lower with the speed. Without shifting, a longer clip stops at the next clip. Playing backwards comes later.": (
        "Тезлик билан овоз ҳам ингичкароқ ёки йўғонроқ эшитилади. Суриш белгиланмаса, узайган клип кейинги клипгача боради. Орқага ижро кейинроқ.",
        "Со скоростью звук становится выше или ниже. Без сдвига удлинённый клип доходит только до следующего клипа. Обратное воспроизведение — позже."),
    "When the clip gets longer or shorter, later clips on its tracks move with its end": (
        "Клип узайса ёки қисқарса, унинг йўлакларидаги кейинги клиплар охири билан бирга сурилади",
        "Когда клип становится длиннее или короче, следующие клипы на его дорожках сдвигаются вместе с его концом"),
})

T.update({
    "A 1-second dissolve at the edit of the selected clips nearest the playhead (Ctrl+D)": (
        "Танланган клипларнинг playhead'га энг яқин кесимига 1 сониялик dissolve (Ctrl+D)",
        "Растворение на 1 секунду в ближайшей к указателю склейке выбранных клипов (Ctrl+D)"),
    "Add transition": ("Ўтиш қўшиш", "Добавить переход"),
    "Between two touching clips the transition mixes them; at a free end the clip fades. Ctrl+D adds a 1-second one at the edit nearest the playhead.": (
        "Ёнма-ён икки клип орасида ўтиш уларни аралаштиради; бўш учда клип аста пайдо бўлади ёки йўқолади. Ctrl+D playhead'га энг яқин кесимга 1 сониялик ўтиш қўяди.",
        "Между соседними клипами переход смешивает их; на свободном конце клип плавно появляется или исчезает. Ctrl+D добавляет переход на 1 секунду в ближайшую к указателю склейку."),
    "Cross dissolve": ("Cross dissolve (аралашиш)", "Перекрёстное растворение"),
    "Crossfade": ("Crossfade (овоз аралашиши)", "Кроссфейд"),
    "Ctrl+D adds a 1-second dissolve at the edit nearest the playhead; the Clip panel sets the kind and length of the start and end. Between touching clips both are mixed (using extra frames of the files when they have them); at a free end the clip fades.": (
        "Ctrl+D playhead'га энг яқин кесимга 1 сониялик dissolve қўяди; «Клип» панелида бош ва охирдаги ўтиш тури ва узунлиги танланади. Ёнма-ён клиплар аралашиб ўтади (файлда ортиқча кадр бўлса, ундан фойдаланилади); бўш учда клип аста пайдо бўлади ёки йўқолади.",
        "Ctrl+D добавляет растворение на 1 секунду в ближайшую к указателю склейку; панель «Клип» задаёт тип и длину перехода в начале и в конце. Соседние клипы смешиваются (с запасными кадрами файлов, если они есть); на свободном конце клип плавно появляется или исчезает."),
    "Dip to black": ("Қорага ўтиш", "Через чёрный"),
    "Dip to white": ("Оққа ўтиш", "Через белый"),
    "End:": ("Охири:", "Конец:"),
    "Start:": ("Боши:", "Начало:"),
    "Phase 8, part 1: transitions between clips work (Ctrl+D, Clip panel).": (
        "8-босқич, 1-қисм: клиплар орасидаги ўтишлар ишлайди (Ctrl+D, «Клип» панели).",
        "Этап 8, часть 1: переходы между клипами работают (Ctrl+D, панель «Клип»)."),
    "Transition": ("Ўтиш", "Переход"),
    "Transitions": ("Ўтишлар", "Переходы"),
    "Transitions: cross dissolve, dip to black or white, wipe; sound crossfade": (
        "Ўтишлар: cross dissolve, қорага ёки оққа ўтиш, wipe; овоз crossfade",
        "Переходы: растворение, через чёрный или белый, вытеснение; кроссфейд звука"),
    "Wipe from the left": ("Чапдан сидириш (wipe)", "Вытеснение слева"),
    "Wipe from the right": ("Ўнгдан сидириш (wipe)", "Вытеснение справа"),
})

T.update({
    "Add": ("Қўшиш", "Добавить"),
    "Add effect": ("Эффект қўшиш", "Добавить эффект"),
    "Add the chosen effect to the clip": ("Танланган эффектни клипга қўшиш", "Добавить выбранный эффект к клипу"),
    "Amount, %": ("Кучи, %", "Сила, %"),
    "Blur": ("Хиралаштириш", "Размытие"),
    "Bottom, %": ("Пастдан, %", "Снизу, %"),
    "Brightness": ("Ёрқинлик", "Яркость"),
    "Brightness and contrast": ("Ёрқинлик ва контраст", "Яркость и контраст"),
    "Clip effects: brightness and contrast, saturation, blur, sharpen, crop, mirror": (
        "Клип эффектлари: ёрқинлик ва контраст, тўйинганлик, хиралаштириш, ўткирлаш, кесиш, ойна",
        "Эффекты клипа: яркость и контраст, насыщенность, размытие, резкость, обрезка, зеркало"),
    "Contrast": ("Контраст", "Контраст"),
    "Crop": ("Кесиш (crop)", "Обрезка"),
    "Effect": ("Эффект", "Эффект"),
    "Effects are drawn before the motion settings. Untick an effect to compare without it.": (
        "Эффектлар ҳаракат созламаларидан олдин чизилади. Эффектсиз солиштириш учун унинг белгисини олинг.",
        "Эффекты рисуются до настроек движения. Снимите отметку, чтобы сравнить без эффекта."),
    "Effects work on pictures. Select a video clip.": (
        "Эффектлар расмга қўйилади. Видео клипни танланг.",
        "Эффекты работают с изображением. Выберите видеоклип."),
    "Flip left and right": ("Чап ва ўнгни алмаштириш", "Отразить слева направо"),
    "Flip upside down": ("Тепа ва пастни алмаштириш", "Отразить сверху вниз"),
    "Left, %": ("Чапдан, %", "Слева, %"),
    "Mirror": ("Ойна (акс)", "Зеркало"),
    "Move down": ("Пастга", "Вниз"),
    "Move effect": ("Эффектни кўчириш", "Переместить эффект"),
    "Move up (effects work from top to bottom)": (
        "Юқорига (эффектлар юқоридан пастга ишлайди)",
        "Вверх (эффекты работают сверху вниз)"),
    "Phase 8, part 2: clip effects work (Effects panel).": (
        "8-босқич, 2-қисм: клип эффектлари ишлайди («Эффектлар» панели).",
        "Этап 8, часть 2: эффекты клипов работают (панель «Эффекты»)."),
    "Radius, px": ("Радиус, px", "Радиус, пикс."),
    "Remove effect": ("Эффектни ўчириш", "Удалить эффект"),
    "Right, %": ("Ўнгдан, %", "Справа, %"),
    "Saturation": ("Тўйинганлик", "Насыщенность"),
    "Saturation, %": ("Тўйинганлик, %", "Насыщенность, %"),
    "Select a video clip and open the Effects tab: add effects, untick to compare, reorder with the arrows, change values. Effects work from top to bottom, before the motion settings. Values do not change over time yet.": (
        "Видео клипни танлаб, «Эффектлар» панелини очинг: эффект қўшинг, белгисини олиб солиштиринг, стрелкалар билан тартибини ўзгартиринг, қийматини ёзинг. Эффектлар юқоридан пастга, ҳаракат созламаларидан олдин ишлайди. Қийматлар ҳозирча вақт бўйича ўзгармайди.",
        "Выберите видеоклип и откройте вкладку «Эффекты»: добавляйте эффекты, снимайте отметку для сравнения, меняйте порядок стрелками и задавайте значения. Эффекты работают сверху вниз, до настроек движения. Значения пока не меняются во времени."),
    "Select a video clip on the timeline to add effects.": (
        "Эффект қўшиш учун timeline'да видео клипни танланг.",
        "Выберите видеоклип на таймлайне, чтобы добавить эффекты."),
    "Sharpen": ("Ўткирлаш", "Резкость"),
    "Top, %": ("Юқоридан, %", "Сверху, %"),
    "Turn effect off": ("Эффектни ўчириб қўйиш", "Выключить эффект"),
    "Turn effect on": ("Эффектни ёқиш", "Включить эффект"),
    "Values": ("Қийматлар", "Значения"),
})

T.update({
    "Centre X, %": ("Марказ X, %", "Центр X, %"),
    "Centre Y, %": ("Марказ Y, %", "Центр Y, %"),
    "Chroma key (remove a colour)": ("Chroma key (рангни олиб ташлаш)", "Хромакей (убрать цвет)"),
    "Chroma key and masks": ("Chroma key ва маскалар", "Хромакей и маски"),
    "Ellipse": ("Эллипс", "Эллипс"),
    "Height, %": ("Баландлик, %", "Высота, %"),
    "In the Effects panel: Chroma key makes one colour (green or blue screen) transparent so the track below shows through; Mask keeps a rectangle or ellipse (or hides it with Invert) with a soft edge. Picking the colour with a dropper, drawn (bezier) masks and moving masks come later.": (
        "«Эффектлар» панелида: Chroma key битта рангни (яшил ёки кўк фон) шаффоф қилади, остидаги йўлак кўринади; Маска тўртбурчак ёки эллипсни қолдиради («Тескари» билан яширади), чети юмшоқ бўлади. Рангни томизғич билан олиш, қўлда чизилган (bezier) ва ҳаракатланадиган маскалар кейинроқ.",
        "На панели «Эффекты»: хромакей делает один цвет (зелёный или синий фон) прозрачным, и видна дорожка ниже; маска оставляет прямоугольник или эллипс (или скрывает его с «Инвертировать») с мягким краем. Выбор цвета пипеткой, рисованные (безье) и движущиеся маски — позже."),
    "Invert (hide the inside)": ("Тескари (ичини яшириш)", "Инвертировать (скрыть внутри)"),
    "Keep greys below, %": ("Кулранг ҳимояси, %", "Защита серого, %"),
    "Key colour (hue), °": ("Ранг (hue), °", "Цвет ключа (тон), °"),
    "Mask (rectangle or ellipse)": ("Маска (тўртбурчак ёки эллипс)", "Маска (прямоугольник или эллипс)"),
    "Phase 8 done: effects, chroma key and masks work (Effects panel).": (
        "8-босқич тугади: эффектлар, chroma key ва маскалар ишлайди («Эффектлар» панели).",
        "Этап 8 завершён: эффекты, хромакей и маски работают (панель «Эффекты»)."),
    "Soft edge, px": ("Юмшоқ чет, px", "Мягкий край, пикс."),
    "Softness, °": ("Юмшоқлик, °", "Мягкость, °"),
    "Spill removal, %": ("Spill тозалаш, %", "Подавление ореола, %"),
    "Tolerance, °": ("Чегара, °", "Допуск, °"),
    "Width, %": ("Кенглик, %", "Ширина, %"),
})

T.update({
    "Basic colour correction: white balance, exposure, contrast, highlights, shadows, saturation, vibrance": (
        "Асосий ранг тузатиш: оқ баланс, экспозиция, контраст, ёруғ ва қоронғи жойлар, тўйинганлик, vibrance",
        "Базовая цветокоррекция: баланс белого, экспозиция, контраст, света, тени, насыщенность, красочность"),
    "Colour": ("Ранг", "Цвет"),
    "Colour correction": ("Ранг тузатиш", "Цветокоррекция"),
    "Colour correction is applied before the effects. Values do not change over time yet.": (
        "Ранг тузатиш эффектлардан олдин қўйилади. Қийматлар ҳозирча вақт бўйича ўзгармайди.",
        "Цветокоррекция применяется до эффектов. Значения пока не меняются во времени."),
    "Colour correction works on pictures. Select a video clip.": (
        "Ранг тузатиш расмга қўйилади. Видео клипни танланг.",
        "Цветокоррекция работает с изображением. Выберите видеоклип."),
    "Exposure": ("Экспозиция", "Экспозиция"),
    "Highlights": ("Ёруғ жойлар", "Света"),
    "In stops: +1 is twice as bright": ("Ступенларда: +1 икки баравар ёруғ", "В ступенях: +1 — вдвое ярче"),
    "LUT, colour wheels, scopes": ("LUT, ранг ғилдираклари, scope'лар", "LUT, цветовые круги, осциллографы"),
    "Left: cooler (blue), right: warmer (yellow)": ("Чапга: совуқроқ (кўк), ўнгга: иссиқроқ (сариқ)", "Влево: холоднее (синее), вправо: теплее (желтее)"),
    "Left: greener, right: more magenta": ("Чапга: яшилроқ, ўнгга: пуштироқ", "Влево: зеленее, вправо: пурпурнее"),
    "Light": ("Ёруғлик", "Свет"),
    "Phase 9, part 1: basic colour correction works (Colour panel).": (
        "9-босқич, 1-қисм: асосий ранг тузатиш ишлайди («Ранг» панели).",
        "Этап 9, часть 1: базовая цветокоррекция работает (панель «Цвет»)."),
    "Reset colour": ("Рангни асл ҳолига", "Сбросить цвет"),
    "Select a video clip and open the Colour tab. Applied before the effects; Reset colour returns to the original. Values do not change over time yet.": (
        "Видео клипни танлаб, «Ранг» панелини очинг. Эффектлардан олдин қўйилади; «Рангни асл ҳолига» тугмаси аслини қайтаради. Қийматлар ҳозирча вақт бўйича ўзгармайди.",
        "Выберите видеоклип и откройте вкладку «Цвет». Применяется до эффектов; «Сбросить цвет» возвращает исходный вид. Значения пока не меняются во времени."),
    "Select a video clip on the timeline to correct its colour.": (
        "Рангини тузатиш учун timeline'да видео клипни танланг.",
        "Выберите видеоклип на таймлайне, чтобы исправить его цвет."),
    "Shadows": ("Қоронғи жойлар", "Тени"),
    "Temperature": ("Ҳарорат", "Температура"),
    "Tint": ("Тус", "Оттенок"),
    "Vibrance": ("Vibrance (жонлилик)", "Красочность"),
    "White balance": ("Оқ баланс", "Баланс белого"),
})

T.update({
    "%1 (not found or not a valid .cube file: the picture is left as it is)": (
        "%1 (топилмади ёки тўғри .cube файл эмас: расм ўзгармайди)",
        "%1 (не найден или не является файлом .cube: изображение не меняется)"),
    "Apply LUT": ("LUT қўйиш", "Применить LUT"),
    "Choose .cube file...": (".cube файлни танлаш...", "Выбрать файл .cube..."),
    "Choose a LUT": ("LUT танлаш", "Выбор LUT"),
    "Colour panel > Look (LUT): choose a .cube file (3D or 1D) and its strength. Applied after the basic correction. Collect project copies the LUT files too. A missing file leaves the picture unchanged and is shown in red.": (
        "«Ранг» панели > «Кўриниш (LUT)»: .cube файлни (3D ёки 1D) ва кучини танланг. Асосий ранг тузатишдан кейин қўйилади. «Лойиҳани йиғиш» LUT файлларини ҳам нусхалайди. Файл топилмаса, расм ўзгармайди ва номи қизил кўринади.",
        "Панель «Цвет» > «Образ (LUT)»: выберите файл .cube (3D или 1D) и силу. Применяется после базовой коррекции. «Собрать проект» копирует и файлы LUT. Если файла нет, изображение не меняется, а имя показано красным."),
    "Colour wheels, scopes": ("Ранг ғилдираклари, scope'лар", "Цветовые круги, осциллографы"),
    "LUT files (*.cube)": ("LUT файллари (*.cube)", "Файлы LUT (*.cube)"),
    "LUT looks (.cube files)": ("LUT кўринишлари (.cube файллар)", "Образы LUT (файлы .cube)"),
    "Look (LUT)": ("Кўриниш (LUT)", "Образ (LUT)"),
    "No LUT": ("LUT йўқ", "Нет LUT"),
    "Phase 9, part 2: colour correction and LUT looks work (Colour panel).": (
        "9-босқич, 2-қисм: ранг тузатиш ва LUT ишлайди («Ранг» панели).",
        "Этап 9, часть 2: цветокоррекция и LUT работают (панель «Цвет»)."),
    "Remove LUT": ("LUT'ни олиб ташлаш", "Убрать LUT"),
    "Strength, %": ("Кучи, %", "Сила, %"),
    "This file could not be read as a .cube LUT:\n%1": (
        "Бу файлни .cube LUT сифатида ўқиб бўлмади:\n%1",
        "Не удалось прочитать файл как LUT .cube:\n%1"),
})

T.update({
    "Colour wheels": ("Ранг ғилдираклари", "Цветовые круги"),
    "Histogram": ("Гистограмма", "Гистограмма"),
    "Phase 9, part 3: colour correction, LUTs and scopes work (Colour panel, Window > Scopes).": (
        "9-босқич, 3-қисм: ранг тузатиш, LUT ва scope'лар ишлайди («Ранг» панели, «Ойна > Scope'лар»).",
        "Этап 9, часть 3: цветокоррекция, LUT и осциллографы работают (панель «Цвет», «Окно > Осциллографы»)."),
    "RGB parade": ("RGB parade", "RGB-парад"),
    "Scopes": ("Scope'лар", "Осциллографы"),
    "Scopes: waveform, RGB parade, vectorscope, histogram": (
        "Scope'лар: waveform, RGB parade, vectorscope, гистограмма",
        "Осциллографы: вейвформ, RGB-парад, вектороскоп, гистограмма"),
    "Shows the picture in the Program monitor. Waveform and parade: 0 = black, 100 = white. Vectorscope: squares are pure colours, the dashed line is skin tone.": (
        "Program мониторидаги расмни кўрсатади. Waveform ва parade: 0 = қора, 100 = оқ. Vectorscope: квадратлар соф ранглар, штрих чизиқ тери ранги.",
        "Показывает изображение монитора программы. Вейвформ и парад: 0 = чёрный, 100 = белый. Вектороскоп: квадраты — чистые цвета, пунктир — тон кожи."),
    "Vectorscope (hue and saturation)": ("Vectorscope (ранг тури ва тўйинганлик)", "Вектороскоп (тон и насыщенность)"),
    "Waveform (brightness)": ("Waveform (ёруғлик)", "Вейвформ (яркость)"),
    "Window > Scopes opens them; they show the frame in the Program monitor and work only while open (up to about 12 times a second while playing).": (
        "«Ойна > Scope'лар» очади; Program мониторидаги кадрни кўрсатади ва фақат очиқ турганда ишлайди (ижро пайтида секундига 12 мартагача).",
        "«Окно > Осциллографы» открывает их; они показывают кадр монитора программы и работают, только пока открыты (при воспроизведении примерно до 12 раз в секунду)."),
})

T.update({
    "Colour wheels: lift, gamma, gain": ("Ранг ғилдираклари: lift, gamma, gain", "Цветовые круги: lift, gamma, gain"),
    "Colour panel > Colour wheels: drag the point towards a colour to tint the dark, middle or bright tones; the slider under a wheel makes them darker or brighter. Double-click puts a wheel back.": (
        "«Ранг» панели > «Ранг ғилдираклари»: нуқтани рангга томон суринг, қоронғи, ўрта ёки ёруғ жойлар шу рангга киради; ғилдирак остидаги слайдер уларни қоронғироқ ёки ёруғроқ қилади. Икки марта босиш ғилдиракни қайтаради.",
        "Панель «Цвет» > «Цветовые круги»: перетащите точку к цвету, чтобы окрасить тени, средние тона или света; ползунок под кругом делает их темнее или светлее. Двойной щелчок возвращает круг."),
    "Phase 9 is done: colour correction, colour wheels, LUTs and scopes work (Colour panel, Window > Scopes).": (
        "9-босқич тугади: ранг тузатиш, ранг ғилдираклари, LUT ва scope'лар ишлайди («Ранг» панели, «Ойна > Scope'лар»).",
        "Этап 9 завершён: цветокоррекция, цветовые круги, LUT и осциллографы работают (панель «Цвет», «Окно > Осциллографы»)."),
    "Lift": ("Lift", "Lift"),
    "Gamma": ("Gamma", "Gamma"),
    "Gain": ("Gain", "Gain"),
    "Reset wheels": ("Ғилдиракларни тиклаш", "Сбросить круги"),
    "Drag the point towards the colour to add; double-click puts it back": (
        "Нуқтани қўшиладиган рангга томон суринг; икки марта босиш уни қайтаради",
        "Перетащите точку к добавляемому цвету; двойной щелчок возвращает её"),
    "Lift: the dark tones (black level)": ("Lift: қоронғи жойлар (қора даража)", "Lift: тени (уровень чёрного)"),
    "Gamma: the middle tones": ("Gamma: ўрта тонлар", "Gamma: средние тона"),
    "Gain: the bright tones (white level)": ("Gain: ёруғ жойлар (оқ даража)", "Gain: света (уровень белого)"),
    "Colour wheel": ("Ранг ғилдираги", "Цветовой круг"),
    "Reset colour wheels": ("Ранг ғилдиракларини тиклаш", "Сброс цветовых кругов"),
})

T.update({
    "Audio mixer: track volume and balance, master volume, level meters": (
        "Аудио микшер: йўлак овози ва баланси, мастер овоз, сатҳ индикаторлари",
        "Аудиомикшер: громкость и баланс дорожек, мастер-громкость, индикаторы уровня"),
    "Audio tab on the right: a strip for each audio track (balance, fader in dB, M and S) and a master strip; the meters move while the Program monitor plays. The faders act on export too.": (
        "Ўнгдаги «Аудио» панели: ҳар бир аудио йўлак учун полоса (баланс, dB фейдер, M ва S) ва мастер полосаси; индикаторлар Program монитори ижро этаётганда ҳаракатланади. Фейдерлар экспортга ҳам таъсир қилади.",
        "Вкладка «Аудио» справа: полоса для каждой аудиодорожки (баланс, фейдер в дБ, M и S) и мастер-полоса; индикаторы движутся во время воспроизведения монитора программы. Фейдеры действуют и при экспорте."),
    "EQ, compressor, noise reduction, loudness": (
        "EQ, компрессор, шовқинни камайтириш, loudness",
        "Эквалайзер, компрессор, шумоподавление, громкость (loudness)"),
    "Phase 10, part 1: the audio mixer works (Audio panel on the right).": (
        "10-босқич, 1-қисм: аудио микшер ишлайди (ўнгдаги «Аудио» панели).",
        "Этап 10, часть 1: аудиомикшер работает (панель «Аудио» справа)."),
    "C": ("C", "Ц"),
    "L %1": ("Ч %1", "Л %1"),
    "R %1": ("Ў %1", "П %1"),
    "Volume, dB (0 = unchanged)": ("Овоз, dB (0 = ўзгармайди)", "Громкость, дБ (0 = без изменений)"),
    " dB": (" dB", " дБ"),
    "Level, dBFS: keep the loudest parts below 0 (red)": (
        "Сатҳ, dBFS: энг баланд жойлар 0 дан (қизил) пастда бўлсин",
        "Уровень, dBFS: самые громкие места должны быть ниже 0 (красный)"),
    "Master": ("Мастер", "Мастер"),
    "Balance: left / right": ("Баланс: чап / ўнг", "Баланс: левый / правый"),
    "Mute the track": ("Йўлак овозини ўчириш", "Выключить звук дорожки"),
    "Solo: hear only the solo tracks": ("Solo: фақат solo йўлакларни эшитиш", "Соло: слышать только дорожки соло"),
    "This sequence has no audio tracks.": ("Бу секвенцияда аудио йўлак йўқ.", "В этой последовательности нет аудиодорожек."),
    "Track faders and balance act after the clip volume. The meters move while the Program monitor plays.": (
        "Йўлак фейдерлари ва баланс клип овозидан кейин ишлайди. Индикаторлар Program монитори ижро этаётганда ҳаракатланади.",
        "Фейдеры и баланс дорожек действуют после громкости клипа. Индикаторы движутся во время воспроизведения монитора программы."),
    "Track volume": ("Йўлак овози", "Громкость дорожки"),
    "Track balance": ("Йўлак баланси", "Баланс дорожки"),
    "Master volume": ("Мастер овоз", "Мастер-громкость"),
})

T.update({
    "Sound effects: equalizer and compressor": ("Овоз эффектлари: эквалайзер ва компрессор", "Звуковые эффекты: эквалайзер и компрессор"),
    "Select an audio clip (or press Sound for a linked clip) in the Effects tab: low cut, bass, middle and treble; compressor with threshold, ratio, attack, release and make-up gain. They work before the clip volume.": (
        "«Эффектлар» панелида аудио клипни танланг (боғланган клипда «Овоз» тугмасини босинг): low cut, бас, ўрта ва юқори; компрессор: чегара, нисбат, attack, release ва make-up. Клип овозидан олдин ишлайди.",
        "Выберите аудиоклип (для связанного клипа нажмите «Звук») на вкладке «Эффекты»: срез низких, басы, середина и верха; компрессор с порогом, степенью, атакой, восстановлением и компенсацией. Работают до громкости клипа."),
    "Noise reduction and loudness": ("Шовқинни камайтириш ва loudness", "Шумоподавление и громкость (loudness)"),
    "Phase 10, part 2: audio mixer, equalizer and compressor work (Audio and Effects panels).": (
        "10-босқич, 2-қисм: аудио микшер, эквалайзер ва компрессор ишлайди («Аудио» ва «Эффектлар» панеллари).",
        "Этап 10, часть 2: аудиомикшер, эквалайзер и компрессор работают (панели «Аудио» и «Эффекты»)."),
    "Equalizer (EQ)": ("Эквалайзер (EQ)", "Эквалайзер (EQ)"),
    "Compressor": ("Компрессор", "Компрессор"),
    "Low cut, Hz (0 = off)": ("Low cut, Hz (0 = ўчиқ)", "Срез низких, Гц (0 = выкл.)"),
    "Bass, dB": ("Бас, dB", "Басы, дБ"),
    "Bass frequency, Hz": ("Бас частотаси, Hz", "Частота басов, Гц"),
    "Middle, dB": ("Ўрта, dB", "Середина, дБ"),
    "Middle frequency, Hz": ("Ўрта частота, Hz", "Частота середины, Гц"),
    "Middle width (Q)": ("Ўрта кенглиги (Q)", "Ширина середины (Q)"),
    "Treble, dB": ("Юқори, dB", "Верха, дБ"),
    "Treble frequency, Hz": ("Юқори частота, Hz", "Частота верхов, Гц"),
    "Threshold, dB": ("Чегара, dB", "Порог, дБ"),
    "Ratio (x:1)": ("Нисбат (x:1)", "Степень (x:1)"),
    "Attack, ms": ("Attack, мс", "Атака, мс"),
    "Release, ms": ("Release, мс", "Восстановление, мс"),
    "Make-up gain, dB": ("Make-up, dB", "Компенсация, дБ"),
    "Picture": ("Расм", "Изображение"),
    "Sound": ("Овоз", "Звук"),
    "Sound effects work before the clip volume and the track fader. Untick an effect to compare without it.": (
        "Овоз эффектлари клип овозидан ва йўлак фейдеридан олдин ишлайди. Эффектсиз солиштириш учун белгини олинг.",
        "Звуковые эффекты действуют до громкости клипа и фейдера дорожки. Снимите флажок, чтобы сравнить без эффекта."),
    "Select a clip on the timeline to add effects.": (
        "Эффект қўшиш учун timeline'да клипни танланг.",
        "Выберите клип на таймлайне, чтобы добавить эффекты."),
})

T.update({
    "Noise reduction": ("Шовқинни камайтириш", "Шумоподавление"),
    "Hum removal (50/60 Hz)": ("Электр гувиллашини олиб ташлаш (50/60 Hz)", "Удаление сетевого гула (50/60 Гц)"),
    "Reduction, dB": ("Пасайтириш, dB", "Подавление, дБ"),
    "Sensitivity, %": ("Сезгирлик, %", "Чувствительность, %"),
    "Mains frequency, Hz": ("Тармоқ частотаси, Hz", "Частота сети, Гц"),
    "Harmonics": ("Гармоникалар", "Гармоники"),
    "Notch width, Hz": ("Кесим кенглиги, Hz", "Ширина вырезки, Гц"),
    "Noise reduction and hum removal": ("Шовқинни камайтириш ва электр гувиллашини олиб ташлаш", "Шумоподавление и удаление сетевого гула"),
    "Effects tab, audio clip: noise reduction learns the steady background noise and lowers it (speech; about 21 ms delay); hum removal cuts 50 or 60 Hz mains hum and its harmonics.": (
        "«Эффектлар» панели, аудио клип: шовқинни камайтириш доимий фон шовқинини ўрганиб пасайтиради (нутқ учун; тахминан 21 мс кечикиш); гувиллашни олиб ташлаш 50 ёки 60 Hz электр гувиллашини ва унинг гармоникаларини кесади.",
        "Вкладка «Эффекты», аудиоклип: шумоподавление изучает постоянный фоновый шум и снижает его (для речи; задержка около 21 мс); удаление гула вырезает сетевой гул 50 или 60 Гц и его гармоники."),
    "Loudness (LUFS) normalisation": ("Овоз баландлигини (LUFS) текислаш", "Нормализация громкости (LUFS)"),
    "Phase 10, part 3: noise reduction and hum removal work (Effects panel, audio clips).": (
        "10-босқич, 3-қисм: шовқинни камайтириш ва электр гувиллашини олиб ташлаш ишлайди («Эффектлар» панели, аудио клиплар).",
        "Фаза 10, часть 3: работают шумоподавление и удаление сетевого гула (панель «Эффекты», аудиоклипы)."),
})

T.update({
    "Loudness (LUFS) normalisation": ("Овоз баландлигини (LUFS) текислаш", "Нормализация громкости (LUFS)"),
    "Clip > Normalize loudness: measures the selected audio clips as in EBU R128 and sets their volume to -14, -16 or -23 LUFS (or your own target) with a peak ceiling. Measuring the whole mix comes with phase 13.": (
        "Клип > Овоз баландлигини текислаш: танланган аудио клиплар EBU R128 бўйича ўлчанади ва овози −14, −16 ёки −23 LUFS га (ёки ўзингиз танлаган қийматга) келтирилади, энг баланд нуқта чегарадан ошмайди. Бутун аралашмани ўлчаш 13-босқичда.",
        "Клип > Нормализовать громкость: выбранные аудиоклипы измеряются по EBU R128, их громкость приводится к -14, -16 или -23 LUFS (или своему значению) с ограничением пиков. Измерение всего микса — в фазе 13."),
    "Export queue, render preview, GPU acceleration, loudness of the whole mix": ("Экспорт навбати, render preview, GPU тезлаштириш, бутун аралашма баландлиги", "Очередь экспорта, предпросмотр рендера, ускорение GPU, громкость всего микса"),
    "Phase 10 done: loudness normalisation works (Clip > Normalize loudness).": (
        "10-босқич тугади: овоз баландлигини текислаш ишлайди (Клип > Овоз баландлигини текислаш).",
        "Фаза 10 завершена: работает нормализация громкости (Клип > Нормализовать громкость)."),
    "Normalize loudness": ("Овоз баландлигини текислаш", "Нормализовать громкость"),
    "Normalize loudness...": ("Овоз баландлигини текислаш...", "Нормализовать громкость..."),
    "Measure the selected audio clips (LUFS) and set their volume to a target loudness": (
        "Танланган аудио клипларни ўлчаш (LUFS) ва овозини мақсад баландликка келтириш",
        "Измерить выбранные аудиоклипы (LUFS) и привести их громкость к целевой"),
    "-14 LUFS: YouTube, Instagram, Telegram": ("−14 LUFS: YouTube, Instagram, Telegram", "-14 LUFS: YouTube, Instagram, Telegram"),
    "-16 LUFS: podcasts, phones": ("−16 LUFS: подкастлар, телефонлар", "-16 LUFS: подкасты, телефоны"),
    "-23 LUFS: television (EBU R128)": ("−23 LUFS: телевидение (EBU R128)", "-23 LUFS: телевидение (EBU R128)"),
    "No sample may be louder than this after the change (sample peak)": (
        "Ўзгаришдан кейин ҳеч бир нуқта бундан баланд бўлмайди (sample peak)",
        "После изменения ни один отсчёт не будет громче этого (sample peak)"),
    "One volume change for all clips (keeps their balance)": (
        "Ҳамма клипларга битта ўзгариш (нисбати сақланади)",
        "Одно изменение для всех клипов (сохраняет их баланс)"),
    "For one recording cut into several clips: they are measured together": (
        "Бир неча бўлакка кесилган битта ёзув учун: бўлаклар бирга ўлчанади",
        "Для одной записи, разрезанной на несколько клипов: они измеряются вместе"),
    "Loudness": ("Баландлик", "Громкость"),
    "Peak": ("Энг баланд нуқта", "Пик"),
    "New volume": ("Янги овоз", "Новая громкость"),
    "Target:": ("Мақсад:", "Цель:"),
    "Peak ceiling:": ("Энг баланд нуқта чегараси:", "Потолок пиков:"),
    "The sound of the clip file is measured, before its volume and sound effects. Silence and very quiet parts are not counted.": (
        "Клип файлидаги овоз ўлчанади: клип овози ва овоз эффектларидан олдин. Жимлик ва жуда паст жойлар ҳисобга олинмайди.",
        "Измеряется звук файла клипа, до его громкости и звуковых эффектов. Тишина и очень тихие места не учитываются."),
    "Apply": ("Қўллаш", "Применить"),
    "measuring...": ("ўлчанмоқда...", "измерение..."),
    "silent": ("жимлик", "тишина"),
    "Now %1": ("Ҳозир %1", "Сейчас %1"),
    "Select audio clips (or video clips with linked sound) on the timeline first.": (
        "Аввал timeline'да аудио клипларни (ёки овози боғланган видео клипларни) танланг.",
        "Сначала выберите на таймлайне аудиоклипы (или видеоклипы со связанным звуком)."),
    "Measuring... clips left: %1": ("Ўлчанмоқда... қолган клиплар: %1", "Измерение... осталось клипов: %1"),
    "Nothing to change: the clips are silent.": ("Ўзгартирадиган нарса йўқ: клипларда овоз йўқ.", "Нечего менять: в клипах тишина."),
    "Some clips stay quieter than the target so that their peaks stay below the ceiling. A compressor (Effects tab) can help them.": (
        "Баъзи клиплар энг баланд нуқтаси чегарадан ошмаслиги учун мақсаддан пастроқ қолади. «Эффектлар»даги компрессор ёрдам беради.",
        "Некоторые клипы останутся тише цели, чтобы пики не превысили потолок. Поможет компрессор (вкладка «Эффекты»)."),
    "Ready: Apply sets the clip volumes (one undo step).": (
        "Тайёр: «Қўллаш» клип овозларини ўрнатади (битта Ctrl+Z қадами).",
        "Готово: «Применить» установит громкость клипов (один шаг отмены)."),
})

T.update({
    "%1 subtitles imported; %2 broken blocks were skipped.": ("%1 та субтитр олинди; %2 та бузилган блок ташлаб кетилди.", "Импортировано субтитров: %1; пропущено повреждённых блоков: %2."),
    "+ Add": ("+ Қўшиш", "+ Добавить"),
    "A new subtitle at the playhead (3 seconds)": ("Курсор жойига янги субтитр (3 сония)", "Новый субтитр в позиции курсора (3 секунды)"),
    "Add subtitle": ("Субтитр қўшиш", "Добавить субтитр"),
    "Also save the subtitles as an .srt file next to the video": ("Субтитрларни видео ёнига .srt файл қилиб ҳам сақлаш", "Также сохранить субтитры в файл .srt рядом с видео"),
    "Burn the subtitles into the picture": ("Субтитрларни расмга ёзиш", "Впечатать субтитры в изображение"),
    "Cannot open %1: %2": ("%1 очилмади: %2", "Не удалось открыть %1: %2"),
    "Cannot write %1: %2": ("%1 ёзилмади: %2", "Не удалось записать %1: %2"),
    "Could not write the subtitle file %1.": ("%1 субтитр файли ёзилмади.", "Не удалось записать файл субтитров %1."),
    "Delete subtitle": ("Субтитрни ўчириш", "Удалить субтитр"),
    "Delete subtitles": ("Субтитрларни ўчириш", "Удалить субтитры"),
    "Delete the selected subtitles": ("Танланган субтитрларни ўчириш", "Удалить выбранные субтитры"),
    "Export SRT": ("SRT сақлаш", "Экспорт SRT"),
    "Export subtitles (SRT)...": ("Субтитрларни сақлаш (SRT)...", "Экспорт субтитров (SRT)..."),
    "Export subtitles": ("Субтитрларни сақлаш", "Экспорт субтитров"),
    "From the bottom:": ("Пастдан:", "Снизу:"),
    "Import SRT": ("SRT очиш", "Импорт SRT"),
    "Import subtitles": ("Субтитрларни очиш", "Импорт субтитров"),
    "Look of all subtitles": ("Ҳамма субтитрлар кўриниши", "Вид всех субтитров"),
    "No subtitles were found in %1.": ("%1 ичида субтитр топилмади.", "В %1 не найдено субтитров."),
    "No subtitles yet. Press \"+ Add\" to write one at the playhead, or import an .srt file.": (
        "Ҳали субтитр йўқ. Курсор жойига ёзиш учун «+ Қўшиш»ни босинг ёки .srt файлни очинг.",
        "Субтитров пока нет. Нажмите «+ Добавить», чтобы написать субтитр в позиции курсора, или импортируйте файл .srt."),
    "Phase 11, part 1: subtitles and .srt files work (Subtitles tab, Graphics menu).": (
        "11-босқич, 1-қисм: субтитрлар ва .srt файллар ишлайди («Субтитрлар» панели, Графика менюси).",
        "Фаза 11, часть 1: работают субтитры и файлы .srt (вкладка «Субтитры», меню «Графика»)."),
    "Replace": ("Алмаштириш", "Заменить"),
    "Selected subtitle": ("Танланган субтитр", "Выбранный субтитр"),
    "Show in the picture (and burn in on export)": ("Расмда кўрсатиш (экспортда видеога ҳам ёзилади)", "Показывать в изображении (и впечатывать при экспорте)"),
    "Start": ("Бошланиши", "Начало"),
    "Subtitle style": ("Субтитр кўриниши", "Стиль субтитров"),
    "Subtitle text (Enter starts a new line)": ("Субтитр матни (Enter — янги қатор)", "Текст субтитра (Enter — новая строка)"),
    "Subtitle text": ("Субтитр матни", "Текст субтитра"),
    "Subtitle timing": ("Субтитр вақти", "Время субтитра"),
    "Subtitle track on the timeline (drag and trim)": ("Timeline'да субтитр йўлаги (суриш ва қисқартириш)", "Дорожка субтитров на таймлайне (перетаскивание и подрезка)"),
    "Subtitles (*.srt)": ("Субтитрлар (*.srt)", "Субтитры (*.srt)"),
    "Subtitles (*.srt);;All files (*)": ("Субтитрлар (*.srt);;Ҳамма файллар (*)", "Субтитры (*.srt);;Все файлы (*)"),
    "Subtitles from speech (automatic)": ("Нутқдан автоматик субтитр", "Автоматические субтитры по речи"),
    "Subtitles from speech (automatic)...": ("Нутқдан автоматик субтитр...", "Автоматические субтитры по речи..."),
    "Subtitles tab on the right: add at the playhead, edit time and text, size, colour, box and height for all; drawn in the Program monitor and burned in on export (optional). Graphics menu: import and export .srt (UTF-8 or Windows-1251); export can also write an .srt next to the video.": (
        "Ўнгдаги «Субтитрлар» панели: курсор жойига қўшиш, вақти ва матнини таҳрирлаш; ҳаммаси учун ўлчам, ранг, фон ва баландлик. Program мониторида кўринади ва экспортда видеога ёзилади (хоҳишга кўра). Графика менюси: .srt очиш ва сақлаш (UTF-8 ёки Windows-1251); экспорт видео ёнига .srt ҳам ёзиши мумкин.",
        "Вкладка «Субтитры» справа: добавление в позиции курсора, правка времени и текста; размер, цвет, подложка и высота для всех. Видны в мониторе Program и впечатываются при экспорте (по желанию). Меню «Графика»: импорт и экспорт .srt (UTF-8 или Windows-1251); экспорт может записать .srt рядом с видео."),
    "Subtitles": ("Субтитрлар", "Субтитры"),
    "Subtitles:": ("Субтитрлар:", "Субтитры:"),
    "Text colour": ("Матн ранги", "Цвет текста"),
    "The file is too large for a subtitle file.": ("Файл субтитр файли учун жуда катта.", "Файл слишком большой для файла субтитров."),
    "The sequence already has subtitles. Replace them or add the new ones?": (
        "Секвенцияда субтитрлар бор. Уларни алмаштирайми ёки янгиларини қўшайми?",
        "В секвенции уже есть субтитры. Заменить их или добавить новые?"),
    "The sequence has no subtitles with text.": ("Секвенцияда матнли субтитр йўқ.", "В секвенции нет субтитров с текстом."),
    "Import subtitles (SRT)...": ("Субтитрларни очиш (SRT)...", "Импорт субтитров (SRT)..."),
})

T.update({
    "Subtitles: drag to move, drag an edge to change the time, double-click a free place to add one": (
        "Субтитрлар: суриб жойини, четини суриб вақтини ўзгартиринг; бўш жойни икки марта босиб янгисини қўшинг",
        "Субтитры: перетащите, чтобы сдвинуть, тяните край, чтобы изменить время; двойной щелчок по пустому месту добавляет новый"),
    "A Subtitles strip above the tracks: drag a subtitle to move it, drag its edge to change the time (snaps to the playhead, markers and clips), double-click a free place to add one. A click opens it in the Subtitles tab.": (
        "Йўлаклар тепасидаги «Субтитрлар» қатори: субтитрни суриб жойини, четини суриб вақтини ўзгартирасиз (курсор, маркер ва клипларга ёпишади); бўш жойни икки марта босиш янгисини қўшади. Босилган субтитр «Субтитрлар» панелида очилади.",
        "Строка «Субтитры» над дорожками: перетащите субтитр, чтобы сдвинуть его, тяните край, чтобы изменить время (прилипает к курсору, маркерам и клипам); двойной щелчок по пустому месту добавляет новый. Щелчок открывает его во вкладке «Субтитры»."),
    "Phase 11, part 2: subtitles have their own strip on the timeline (drag to move, drag an edge to retime).": (
        "11-босқич, 2-қисм: субтитрлар timeline'да алоҳида қаторда (суриб жойини, четини суриб вақтини ўзгартиринг).",
        "Этап 11, часть 2: у субтитров своя строка на таймлайне (перетаскивание и изменение времени за край)."),
})

T.update({
    "Multicamera sequence lined up by sound": ("Овоз бўйича синхронланган мультикам секвенция", "Мультикамерная секвенция, синхронизированная по звуку"),
    "Select two or more recordings of one event in the media bin, then Sequence > Create multicamera sequence: they are lined up by their sound (or by file start) and one clip goes to the timeline. Split it and pick the camera of each part in the Clip tab (picture and sound separately); double-click it to fine-tune the sync.": (
        "Медиа кутубхонасида бир воқеанинг икки ёки ундан кўп ёзувини танланг, кейин «Секвенция > Мультикам секвенция яратиш»: улар овози бўйича (ёки файл бошидан) тенглаштирилади ва timeline'га битта клип қўйилади. Клипни бўлинг ва ҳар бир бўлагининг камерасини «Клип» панелида танланг (расм ва овоз алоҳида); синхронни қўлда тўғрилаш учун клипни икки марта босинг.",
        "Выберите в медиатеке две или больше записей одного события, затем «Секвенция > Создать мультикамерную секвенцию»: они выравниваются по звуку (или по началу файлов), и на таймлайн ставится один клип. Разрежьте его и выберите камеру для каждой части во вкладке «Клип» (картинка и звук отдельно); двойной щелчок открывает его для точной подстройки."),
    "Multicamera monitor: all angles at once, switching while playing (keys 1-9)": ("Мультикам монитор: ҳамма ракурслар бир экранда, ижро пайтида алмаштириш (1–9 тугмалари)", "Мультикамерный монитор: все ракурсы сразу, переключение во время воспроизведения (клавиши 1–9)"),
    "Multicamera": ("Мультикам", "Мультикамера"),
    "Picture:": ("Расм:", "Картинка:"),
    "Sound:": ("Овоз:", "Звук:"),
    "Which camera this part of the clip shows. Split the clip (Ctrl+K) to switch cameras.": ("Клипнинг бу бўлаги қайси камерани кўрсатади. Камерани алмаштириш учун клипни бўлинг (Ctrl+K).", "Какую камеру показывает эта часть клипа. Чтобы переключить камеру, разрежьте клип (Ctrl+K)."),
    "Phase 12, part 1: multicamera sequences lined up by sound (Sequence menu); pick the camera in the Clip tab.": ("12-босқич, 1-қисм: овоз бўйича синхронланган мультикам секвенциялар (Секвенция менюси); камерани «Клип» панелида танланг.", "Этап 12, часть 1: мультикамерные секвенции с синхронизацией по звуку (меню «Секвенция»); камера выбирается во вкладке «Клип»."),
    "Create multicamera sequence...": ("Мультикам секвенция яратиш...", "Создать мультикамерную секвенцию..."),
    "Select two or more recordings of one event in the media bin (or their clips on the timeline): they are lined up by sound and become the angles of one clip": ("Медиа кутубхонасида (ёки timeline'да уларнинг клипларини) бир воқеанинг икки ёки ундан кўп ёзувини танланг: улар овози бўйича тенглаштирилиб, битта клипнинг ракурсларига айланади", "Выберите в медиатеке (или их клипы на таймлайне) две или больше записей одного события: они выравниваются по звуку и становятся ракурсами одного клипа"),
    "Create multicamera sequence": ("Мультикам секвенция яратиш", "Создать мультикамерную секвенцию"),
    "By sound (the cameras recorded the same sound)": ("Овоз бўйича (камералар бир хил овозни ёзган)", "По звуку (камеры записали один и тот же звук)"),
    "By file start (all start together)": ("Файл бошидан (ҳаммаси бирга бошланади)", "По началу файла (все начинаются вместе)"),
    "Angle": ("Ракурс", "Ракурс"),
    "File": ("Файл", "Файл"),
    "Shift": ("Силжиш", "Сдвиг"),
    "Angle 1 is shown first. You can change the angle of each part of the clip in the Clip tab; double-click the clip to open the sequence and fine-tune the sync by hand.": (
        "Аввал 1-ракурс кўринади. Клипнинг ҳар бир бўлаги ракурсини «Клип» панелида ўзгартирасиз; синхронни қўлда тўғрилаш учун клипни икки марта босиб секвенцияни очинг.",
        "Сначала показан ракурс 1. Ракурс каждой части клипа меняется во вкладке «Клип»; чтобы подправить синхронизацию вручную, откройте секвенцию двойным щелчком по клипу."),
    "Sync:": ("Синхронлаш:", "Синхронизация:"),
    "Sound from:": ("Овоз манбаи:", "Звук из:"),
    "Create": ("Яратиш", "Создать"),
    "Listening to the sound of each recording...": ("Ҳар бир ёзувнинг овози тингланмоқда...", "Анализ звука каждой записи..."),
    "The sound of some recordings did not match (marked below); they start with angle 1. Press Create to make the sequence anyway and line them up by hand, or choose sync by file start.": (
        "Баъзи ёзувларнинг овози тўғри келмади (пастда белгиланган); улар 1-ракурс билан бирга бошланади. Барибир яратиб қўлда тўғрилаш учун «Яратиш»ни босинг ёки файл бошидан синхронлашни танланг.",
        "Звук некоторых записей не совпал (отмечены ниже); они начинаются вместе с ракурсом 1. Нажмите «Создать», чтобы всё равно создать секвенцию и выровнять вручную, или выберите синхронизацию по началу файла."),
    "Create anyway": ("Барибир яратиш", "Всё равно создать"),
    "%1 s": ("%1 с", "%1 с"),
    "not found": ("топилмади", "не найдено"),
    "Could not create the multicamera sequence.": ("Мультикам секвенция яратилмади.", "Не удалось создать мультикамерную секвенцию."),
    "Multicam %1": ("Мультикам %1", "Мультикам %1"),
    "Angle %1": ("%1-ракурс", "Ракурс %1"),
    "Change angle": ("Ракурсни алмаштириш", "Смена ракурса"),
})

T.update({
    "Camera %1": ("%1-камера", "Камера %1"),
    "Sound of camera %1": ("%1-камера овози", "Звук камеры %1"),
    "Switch camera": ("Камерани алмаштириш", "Переключить камеру"),
    "Switch sound": ("Овозни алмаштириш", "Переключить звук"),
    "Multicamera monitor": ("Мультикам монитор", "Мультикамерный монитор"),
    "Move the playhead onto a multicamera clip to see all its cameras here.": ("Бу ерда ҳамма камераларни кўриш учун курсорни мультикам клип устига қўйинг.", "Поставьте курсор на мультикамерный клип, чтобы увидеть здесь все его камеры."),
    "The video engine is not available.": ("Видео двигатели ишламаяпти.", "Видеодвижок недоступен."),
    "Phase 12, part 2: multicamera monitor (Window menu); press 1-9 to switch cameras while playing.": ("12-босқич, 2-қисм: мультикам монитор (Ойна менюси); ижро пайтида камерани 1–9 тугмалари билан алмаштиринг.", "Этап 12, часть 2: мультикамерный монитор (меню «Окно»); переключайте камеры клавишами 1–9 во время воспроизведения."),
    "Window > Multicamera monitor shows every camera of the multicamera clip at the playhead; the camera in the Program monitor has a red frame. Press 1-9 (Alt+1-9 for the sound) or click a camera: the clip is cut at the playhead and the rest shows that camera, also while playing. Each switch is one undo step.": (
        "«Ойна > Мультикам монитор» курсор турган мультикам клипнинг ҳамма камерасини кўрсатади; Program мониторидаги камера қизил ромкада. 1–9 тугмаларини (овоз учун Alt+1–9) босинг ёки камерани босинг: клип курсор жойида кесилади ва давоми шу камерани кўрсатади, ижро пайтида ҳам. Ҳар бир алмаштириш битта бекор қилиш қадами.",
        "«Окно > Мультикамерный монитор» показывает все камеры мультикамерного клипа под курсором; камера в мониторе Program обведена красной рамкой. Нажмите 1–9 (Alt+1–9 для звука) или щёлкните камеру: клип разрезается в позиции курсора, и дальше показывается эта камера, в том числе во время воспроизведения. Каждое переключение — один шаг отмены."),
})

# Phase 13a: export queue
T.update({
    "Export queue": ("Экспорт навбати", "Очередь экспорта"),
    "In the Export window press Add to queue instead of Export: a copy of the project as it is now waits in File > Export queue. Add several files (other names, qualities or project versions), then Start queue: they are exported one after another while you keep editing. Stop, Remove, Try again and Clear finished are there too.":
        ("Экспорт ойнасида «Экспорт» ўрнига «Навбатга қўшиш»ни босинг: лойиҳанинг шу ондаги нусхаси «Файл > Экспорт навбати»да кутиб туради. Бир неча файл қўшинг (бошқа ном, сифат ёки лойиҳанинг бошқа ҳолати), кейин «Навбатни бошлаш»: улар кетма-кет чиқарилади, сиз эса монтажни давом эттирасиз. «Тўхтатиш», «Олиб ташлаш», «Қайта уриниш» ва «Тугаганларни тозалаш» ҳам бор.",
         "В окне экспорта нажмите «В очередь» вместо «Экспорт»: копия проекта в текущем виде ждёт в «Файл > Очередь экспорта». Добавьте несколько файлов (другие имена, качество или версии проекта), затем «Запустить очередь»: они экспортируются один за другим, пока вы продолжаете монтаж. Есть также «Остановить», «Убрать», «Повторить» и «Очистить завершённые»."),
    "Render preview, GPU acceleration, loudness of the whole mix": ("Рендер превью, GPU тезлатиш, бутун аралашманинг овоз баландлиги", "Просчёт превью, ускорение GPU, громкость всего микса"),
    "Add to queue": ("Навбатга қўшиш", "В очередь"),
    "Export later, one after another (File > Export queue). The queue keeps the project as it is now.":
        ("Кейинроқ, кетма-кет экспорт қилиш (Файл > Экспорт навбати). Навбат лойиҳани ҳозирги ҳолатида сақлайди.",
         "Экспортировать позже, один за другим (Файл > Очередь экспорта). Очередь сохраняет проект в текущем виде."),
    "Added to the export queue (waiting: %1). File > Export queue starts it.":
        ("Экспорт навбатига қўшилди (кутмоқда: %1). «Файл > Экспорт навбати»да бошланг.",
         "Добавлено в очередь экспорта (ожидает: %1). Запустите в «Файл > Очередь экспорта»."),
    "Another export in the queue already writes %1.": ("Навбатдаги бошқа экспорт аллақачон %1 файлига ёзади.", "Другой экспорт в очереди уже пишет %1."),
    "Waiting": ("Кутмоқда", "Ожидает"),
    "Exporting: %1%": ("Экспорт: %1%", "Экспорт: %1%"),
    "Exporting": ("Экспорт қилинмоқда", "Экспортируется"),
    "Done": ("Тайёр", "Готово"),
    "Failed: %1": ("Хато: %1", "Ошибка: %1"),
    "Cancelled": ("Бекор қилинди", "Отменено"),
    "Start queue": ("Навбатни бошлаш", "Запустить очередь"),
    "Stop": ("Тўхтатиш", "Остановить"),
    "Try again": ("Қайта уриниш", "Повторить"),
    "Clear finished": ("Тугаганларни тозалаш", "Очистить завершённые"),
    "State": ("Ҳолат", "Состояние"),
    "The queue is empty. In the Export window, press \"Add to queue\" to put an export here.":
        ("Навбат бўш. Экспорт ойнасида «Навбатга қўшиш»ни босинг, иш шу ерга тушади.",
         "Очередь пуста. В окне экспорта нажмите «В очередь», чтобы добавить сюда экспорт."),
    "Exporting one after another. You can go on editing; queued jobs use the project as it was when they were added.":
        ("Кетма-кет экспорт қилинмоқда. Монтажни давом эттиришингиз мумкин: навбатдаги ишлар лойиҳанинг қўшилган пайтдаги ҳолатини ишлатади.",
         "Экспорт идёт один за другим. Можно продолжать монтаж: задания в очереди используют проект в том виде, в каком он был при добавлении."),
    "Waiting: %1. Press \"Start queue\" to export them one after another.":
        ("Кутмоқда: %1. Кетма-кет экспорт қилиш учун «Навбатни бошлаш»ни босинг.",
         "Ожидает: %1. Нажмите «Запустить очередь», чтобы экспортировать их один за другим."),
    "Export queue finished: %1 done, %2 failed.": ("Экспорт навбати тугади: %1 тайёр, %2 хато.", "Очередь экспорта завершена: готово %1, ошибок %2."),
    "Export queue finished: %1 done.": ("Экспорт навбати тугади: %1 тайёр.", "Очередь экспорта завершена: готово %1."),
    "Export queue...": ("Экспорт навбати...", "Очередь экспорта..."),
    "Phase 13, part 1: export queue (File menu); keep editing while the files are exported.":
        ("13-босқич, 1-қисм: экспорт навбати (Файл менюси); файллар чиқарилаётганда монтажни давом эттиринг.",
         "Этап 13, часть 1: очередь экспорта (меню «Файл»); продолжайте монтаж, пока файлы экспортируются."),
    "Exports in the queue are not finished. Close anyway? They will be lost.":
        ("Навбатдаги экспортлар тугамаган. Барибир ёпилсинми? Улар йўқолади.",
         "Экспорты в очереди не завершены. Всё равно закрыть? Они будут потеряны."),
    "Another export is running. Wait for it to finish or cancel it.":
        ("Бошқа экспорт кетмоқда. Тугашини кутинг ёки уни бекор қилинг.",
         "Идёт другой экспорт. Дождитесь его завершения или отмените его."),
})

# Phase 13b: render preview
T.update({
    "A line on the time ruler shows red where the picture is heavy (effects, colour, motion, speed, transitions, nested sequences, several layers). Sequence > Render preview (Enter on the timeline) renders those parts of the selected clips, or of the whole sequence, once in the background; they turn green and play from the file. An edit makes a part red again; undo brings its file back. Sound is not rendered. Export always uses the full quality.":
        ("Вақт чизғичидаги чизиқ тасвир оғир жойларни (эффект, ранг, ҳаракат, тезлик, ўтишлар, ички секвенция, бир неча қатлам) қизил қилиб кўрсатади. «Секвенция > Превью рендер» (timeline'да Enter) танланган клипларнинг (ёки бутун секвенциянинг) шу жойларини фонда бир марта рендер қилади: улар яшил бўлади ва файлдан ўйнатилади. Таҳрирдан кейин жой яна қизил бўлади, бекор қилсангиз файл яна ишлатилади. Овоз рендер қилинмайди. Экспорт доим тўлиқ сифатда.",
         "Полоса на линейке времени показывает красным тяжёлые места (эффекты, цвет, движение, скорость, переходы, вложенные секвенции, несколько слоёв). «Секвенция > Просчитать превью» (Enter на таймлайне) один раз просчитывает в фоне эти места выбранных клипов (или всей секвенции): они становятся зелёными и играют из файла. Правка снова делает место красным, отмена возвращает его файл. Звук не просчитывается. Экспорт всегда в полном качестве."),
    "GPU acceleration, loudness of the whole mix": ("GPU тезлатиш, бутун аралашманинг овоз баландлиги", "Ускорение GPU, громкость всего микса"),
    "The program keeps thumbnails, audio waveforms, proxy videos and rendered previews made from your media in a cache folder. Deleting them is safe: they are made again when needed. Your media files and projects are not touched.":
        ("Дастур медиангиздан ясалган кичик расмлар, овоз тўлқинлари, прокси видеолар ва рендер қилинган превьюларни кеш папкасида сақлайди. Уларни ўчириш хавфсиз: керак бўлганда қайта ясалади. Медиа файлларингиз ва лойиҳаларингизга тегилмайди.",
         "Программа хранит миниатюры, волны звука, прокси-видео и просчитанные превью из ваших медиа в папке кеша. Удалять их безопасно: при необходимости они создаются заново. Ваши медиафайлы и проекты не затрагиваются."),
    "Rendered previews": ("Рендер қилинган превьюлар", "Просчитанные превью"),
    "Rendering preview: %1%": ("Превью рендер: %1%", "Просчёт превью: %1%"),
    "Preview rendered: the green parts play from the rendered files.":
        ("Превью тайёр: яшил жойлар рендер қилинган файлдан ўйнайди.", "Превью готово: зелёные места играют из просчитанных файлов."),
    "Could not render the preview: %1": ("Превьюни рендер қилиб бўлмади: %1", "Не удалось просчитать превью: %1"),
    "Preview files deleted: %1.": ("Превью файллари ўчирилди: %1.", "Файлы превью удалены: %1."),
    "Render the red parts of the selected clips (or of the whole sequence) once, so they play smoothly (Enter on the timeline)":
        ("Танланган клипларнинг (ёки бутун секвенциянинг) қизил жойларини бир марта рендер қилиш, улар текис ўйнаши учун (timeline'да Enter)",
         "Один раз просчитать красные места выбранных клипов (или всей секвенции), чтобы они играли плавно (Enter на таймлайне)"),
    "Delete preview files": ("Превью файлларини ўчириш", "Удалить файлы превью"),
    "Phase 13, part 2: render preview (Sequence menu, Enter): red parts on the ruler play smoothly once rendered.":
        ("13-босқич, 2-қисм: превью рендер (Секвенция менюси, Enter): чизғичдаги қизил жойлар рендердан кейин текис ўйнайди.",
         "Этап 13, часть 2: просчёт превью (меню «Секвенция», Enter): красные места на линейке после просчёта играют плавно."),
    "A preview is being rendered already.": ("Превью аллақачон рендер қилинмоқда.", "Превью уже просчитывается."),
    "Nothing to render here: these parts are rendered already or play without it.":
        ("Бу ерда рендер қиладиган нарса йўқ: бу жойлар аллақачон рендер қилинган ёки усиз ҳам текис ўйнайди.",
         "Здесь нечего просчитывать: эти места уже просчитаны или играют и так."),
})

# Phase 13c: mix loudness
T.update({
    "Clip > Normalize loudness: measures the selected audio clips as in EBU R128 and sets their volume to -14, -16 or -23 LUFS (or your own target) with a peak ceiling. For the whole mix see Sequence > Mix loudness.": (
        "Клип > Овоз баландлигини текислаш: танланган аудио клиплар EBU R128 бўйича ўлчанади ва овози −14, −16 ёки −23 LUFS га (ёки ўзингиз танлаган қийматга) келтирилади, энг баланд нуқта чегарадан ошмайди. Бутун аралашма учун: «Секвенция > Аралашма овоз баландлиги».",
        "Клип > Нормализовать громкость: выбранные аудиоклипы измеряются по EBU R128, их громкость приводится к -14, -16 или -23 LUFS (или своему значению) с ограничением пиков. Для всего микса: «Эпизод > Громкость микса»."),
    "Loudness of the whole mix": ("Бутун аралашманинг овоз баландлиги", "Громкость всего микса"),
    "Sequence > Mix loudness measures the whole mixed sound as it will be exported (every track with its volume, effects and fader, and the master) in LUFS, with its peak. Apply sets the master volume so the mix reaches -14, -16 or -23 LUFS (or your own target) without peaks above the ceiling; the balance between the tracks stays.": (
        "«Секвенция > Аралашма овоз баландлиги» бутун аралашган овозни экспортдагидек (ҳар бир трек ўз овози, эффектлари ва фейдери билан, ҳамда мастер) LUFS да ва энг баланд нуқтаси билан ўлчайди. «Қўллаш» мастер овозини шундай ўрнатадики, аралашма −14, −16 ёки −23 LUFS га (ёки ўзингиз танлаган қийматга) етади ва чўққилар чегарадан ошмайди; треклар орасидаги мувозанат сақланади.",
        "«Эпизод > Громкость микса» измеряет весь сведённый звук так, как он будет экспортирован (каждая дорожка со своей громкостью, эффектами и фейдером, и мастер), в LUFS и с пиком. «Применить» ставит громкость мастера так, чтобы микс достиг -14, -16 или -23 LUFS (или своего значения) без пиков выше потолка; баланс между дорожками сохраняется."),
    "GPU acceleration": ("GPU тезлаштириш", "Ускорение GPU"),
    "Mix loudness...": ("Аралашма овоз баландлиги...", "Громкость микса..."),
    "Measure the loudness of the whole mix (LUFS) and set the master volume to a target":
        ("Бутун аралашманинг овоз баландлигини (LUFS) ўлчаш ва мастер овозини мақсадга келтириш",
         "Измерить громкость всего микса (LUFS) и привести громкость мастера к цели"),
    "Phase 13, part 3: loudness of the whole mix (Sequence > Mix loudness): measure in LUFS and set the master volume.":
        ("13-босқич, 3-қисм: бутун аралашманинг овоз баландлиги (Секвенция > Аралашма овоз баландлиги): LUFS да ўлчаш ва мастер овозини ўрнатиш.",
         "Этап 13, часть 3: громкость всего микса («Эпизод > Громкость микса»): измерение в LUFS и установка громкости мастера."),
    "Mix loudness": ("Аралашма овоз баландлиги", "Громкость микса"),
    "The whole sequence is measured as it will be exported: every track with its volume, sound effects and fader, and the master. Apply changes only the master volume (Audio tab), so the balance between the tracks stays.":
        ("Бутун секвенция экспортдагидек ўлчанади: ҳар бир трек ўз овози, товуш эффектлари ва фейдери билан, ҳамда мастер. «Қўллаш» фақат мастер овозини ўзгартиради («Аудио» ёрлиғи), шунинг учун треклар орасидаги мувозанат сақланади.",
         "Весь эпизод измеряется так, как будет экспортирован: каждая дорожка со своей громкостью, звуковыми эффектами и фейдером, и мастер. «Применить» меняет только громкость мастера (вкладка «Аудио»), поэтому баланс между дорожками сохраняется."),
    "Measure again": ("Қайта ўлчаш", "Измерить снова"),
    "Could not measure: %1": ("Ўлчаб бўлмади: %1", "Не удалось измерить: %1"),
    "Measuring the mix...": ("Аралашма ўлчанмоқда...", "Измерение микса..."),
    "The sound of the whole sequence is rendered; you can keep working.":
        ("Бутун секвенциянинг овози рендер қилинмоқда; ишлашда давом этишингиз мумкин.",
         "Звук всего эпизода просчитывается; можно продолжать работу."),
    "The sequence has changed since the last measurement.":
        ("Охирги ўлчашдан кейин секвенция ўзгарди.", "Эпизод изменился после последнего измерения."),
    "Press Measure again.": ("«Қайта ўлчаш» ни босинг.", "Нажмите «Измерить снова»."),
    "The mix is silent.": ("Аралашмада овоз йўқ.", "Микс беззвучен."),
    "Loudness: %1   Peak: %2": ("Баландлик: %1   Чўққи: %2", "Громкость: %1   Пик: %2"),
    "The mix is at the target already (master volume %1).":
        ("Аралашма аллақачон мақсадда (мастер овози %1).", "Микс уже на цели (громкость мастера %1)."),
    "The peaks are at the ceiling already: the mix cannot get louder without a compressor or limiter (Effects tab).":
        ("Чўққилар аллақачон чегарада: компрессор ёки лимитерсиз аралашмани баландроқ қилиб бўлмайди («Эффектлар» ёрлиғи).",
         "Пики уже у потолка: без компрессора или лимитера микс громче не сделать (вкладка «Эффекты»)."),
    "Apply changes the master volume from %1 to %2.":
        ("«Қўллаш» мастер овозини %1 дан %2 га ўзгартиради.", "«Применить» изменит громкость мастера с %1 на %2."),
    "The mix stays at %1 so that its peaks stay below the ceiling; a compressor (Effects tab) can help.":
        ("Чўққилар чегарадан ошмаслиги учун аралашма %1 да қолади; компрессор ёрдам бериши мумкин («Эффектлар» ёрлиғи).",
         "Микс останется на %1, чтобы пики не превысили потолок; поможет компрессор (вкладка «Эффекты»)."),
    "The mix is being measured already.": ("Аралашма аллақачон ўлчанмоқда.", "Микс уже измеряется."),
    "The sound could not be rendered.": ("Овозни рендер қилиб бўлмади.", "Не удалось просчитать звук."),
    "Normalize mix loudness": ("Аралашма овоз баландлигини текислаш", "Нормализовать громкость микса"),
})

# Phase 13d: GPU decoding
T.update({
    "GPU acceleration: video decoding on the video card": ("GPU тезлатиш: видеони видеокартада декодлаш", "Ускорение GPU: декодирование видео на видеокарте"),
    "The video card decodes H.264, HEVC and other videos for playback, render preview, export and proxy making (Windows: D3D11VA, DXVA2 or NVIDIA CUDA; Linux: VAAPI or CUDA); when it cannot decode a file, the processor does. Export encodes on the card too (Intel Quick Sync, NVIDIA, AMD, Windows Media Foundation). File > Settings shows what was found and can switch decoding off. Effects, colour and titles are still computed on the processor.": (
        "Видеокарта H.264, HEVC ва бошқа видеоларни ўйнатиш, превью рендер, экспорт ва proxy тайёрлаш учун декодлайди (Windows: D3D11VA, DXVA2 ёки NVIDIA CUDA; Linux: VAAPI ёки CUDA); видеокарта файлни декодлай олмаса, процессор декодлайди. Экспортда кодлаш ҳам видеокартада (Intel Quick Sync, NVIDIA, AMD, Windows Media Foundation). «Файл > Созламалар» нима топилганини кўрсатади ва декодлашни ўчириш мумкин. Эффектлар, ранг ва матнлар ҳали процессорда ҳисобланади.",
        "Видеокарта декодирует H.264, HEVC и другие видео для воспроизведения, просчёта превью, экспорта и создания прокси (Windows: D3D11VA, DXVA2 или NVIDIA CUDA; Linux: VAAPI или CUDA); если она не может декодировать файл, это делает процессор. Экспорт тоже кодирует на видеокарте (Intel Quick Sync, NVIDIA, AMD, Windows Media Foundation). «Файл > Настройки» показывает, что найдено, и позволяет отключить декодирование. Эффекты, цвет и титры пока считаются на процессоре."),
    "Phase 13, part 4: video decoding on the video card (File > Settings shows what was found).":
        ("13-босқич, 4-қисм: видеони видеокартада декодлаш (нима топилгани «Файл > Созламалар»да).",
         "Этап 13, часть 4: декодирование видео на видеокарте (что найдено — в «Файл > Настройки»)."),
    "Video card decoding:": ("Видеокартада декодлаш:", "Декодирование на видеокарте:"),
    "Automatic": ("Автоматик", "Автоматически"),
    "Off (processor only)": ("Ўчирилган (фақат процессор)", "Выключено (только процессор)"),
    "The video card decodes H.264, HEVC and other videos for playback, export and proxies. If it cannot decode a file, the processor does it.":
        ("Видеокарта H.264, HEVC ва бошқа видеоларни ўйнатиш, экспорт ва proxy учун декодлайди. Бирор файлни декодлай олмаса, уни процессор декодлайди.",
         "Видеокарта декодирует H.264, HEVC и другие видео для воспроизведения, экспорта и прокси. Если она не может декодировать файл, это делает процессор."),
    "No video card decoder was found on this computer: the processor decodes.":
        ("Бу компьютерда видеокарта декодери топилмади: процессор декодлайди.",
         "На этом компьютере декодер видеокарты не найден: декодирует процессор."),
    "Found: %1.": ("Топилди: %1.", "Найдено: %1."),
    "Export: no hardware H.264 encoder, MPEG-4 is used.":
        ("Экспорт: H.264 аппарат кодери йўқ, MPEG-4 ишлатилади.", "Экспорт: аппаратного кодера H.264 нет, используется MPEG-4."),
    "Export can use: %1.": ("Экспорт ишлата олади: %1.", "Экспорт может использовать: %1."),
    "Effects and colour are computed on the processor. A change applies to videos opened after it; restart to apply it everywhere.":
        ("Эффектлар ва ранг процессорда ҳисобланади. Ўзгартириш ундан кейин очилган видеоларга таъсир қилади; ҳаммасига таъсир қилиши учун дастурни қайта ишга туширинг.",
         "Эффекты и цвет считаются на процессоре. Изменение действует на видео, открытые после него; чтобы применить везде, перезапустите программу."),
})

# Phase 14a: scene change detection
T.update({
    "Scene change detection": ("Саҳна алмашишларини топиш", "Поиск смены сцен"),
    "Select video clips on the timeline, then Clip > Detect scene changes: each video is read once on this computer (no internet, no AI model) and the hard cuts between shots are found. Split cuts the clips (with their sound) at every new shot, or markers are put there instead; one undo step. A slider sets the sensitivity. Slow fades between shots are not found.": (
        "Timeline'да видео клипларни танланг, кейин «Клип > Саҳна алмашишларини топиш»: ҳар бир видео шу компьютерда бир марта ўқилади (интернетсиз, AI моделсиз) ва кадрлар орасидаги кескин кесимлар топилади. «Клипларни бўлиш» клипларни (овози билан) ҳар бир янги кадр бошида бўлади, ёки ўрнига маркер қўйилади; битта бекор қилиш қадами. Сезгирликни ползунок билан ўзгартириш мумкин. Секин алмашадиган (fade) ўтишлар топилмайди.",
        "Выделите видеоклипы на таймлайне, затем «Клип > Найти смену сцен»: каждое видео один раз читается на этом компьютере (без интернета и без модели ИИ), и находятся резкие склейки между планами. «Разрезать клипы» режет клипы (вместе со звуком) в начале каждого нового плана, либо там ставятся маркеры; один шаг отмены. Чувствительность задаётся ползунком. Плавные переходы (затемнения) не находятся."),
    "AI assistants: transcription, text-based editing": ("AI ёрдамчилар: нутқни матнга айлантириш, матн орқали монтаж", "ИИ-помощники: расшифровка речи, монтаж по тексту"),
    "Phase 14, part 1: scene change detection (Clip > Detect scene changes), on this computer without the internet.":
        ("14-босқич, 1-қисм: саҳна алмашишларини топиш (Клип > Саҳна алмашишларини топиш), шу компьютерда, интернетсиз.",
         "Этап 14, часть 1: поиск смены сцен («Клип > Найти смену сцен»), на этом компьютере без интернета."),
    "Split at scene changes": ("Саҳна алмашишларида бўлиш", "Разрезать по смене сцен"),
    "Mark scene changes": ("Саҳна алмашишларига маркер қўйиш", "Отметить смену сцен"),
    "Detect scene changes": ("Саҳна алмашишларини топиш", "Найти смену сцен"),
    "Detect scene changes...": ("Саҳна алмашишларини топиш...", "Найти смену сцен..."),
    "Find the cuts between shots in the selected video clips and split them there or add markers":
        ("Танланган видео клиплардаги кадрлар орасидаги кесимларни топиш ва шу ерда бўлиш ёки маркер қўйиш",
         "Найти склейки между планами в выбранных видеоклипах и разрезать их там или поставить маркеры"),
    "Higher finds softer cuts too; lower finds only clear cuts":
        ("Юқорироқ бўлса юмшоқроқ кесимлар ҳам топилади; пастроқ бўлса фақат аниқ кесимлар",
         "Выше — находятся и мягкие склейки; ниже — только явные"),
    "Split the clips at each new shot": ("Клипларни ҳар бир янги кадр бошида бўлиш", "Разрезать клипы в начале каждого плана"),
    "Put a marker at each new shot": ("Ҳар бир янги кадрга маркер қўйиш", "Поставить маркер на каждый новый план"),
    "Low": ("Паст", "Низкая"),
    "High": ("Юқори", "Высокая"),
    "Sensitivity:": ("Сезгирлик:", "Чувствительность:"),
    "Result:": ("Натижа:", "Результат:"),
    "Finds hard cuts between shots in the selected video clips. It works on this computer, without the internet; each video is read once and remembered. Slow fades between shots are not found.":
        ("Танланган видео клиплардаги кадрлар орасидаги кескин кесимларни топади. Шу компьютерда, интернетсиз ишлайди; ҳар бир видео бир марта ўқилади ва эсда сақланади. Секин алмашадиган (fade) ўтишлар топилмайди.",
         "Находит резкие склейки между планами в выбранных видеоклипах. Работает на этом компьютере без интернета; каждое видео читается один раз и запоминается. Плавные переходы (затемнения) не находятся."),
    "Add markers": ("Маркер қўйиш", "Поставить маркеры"),
    "Split clips": ("Клипларни бўлиш", "Разрезать клипы"),
    "Select video clips on the timeline first.": ("Аввал timeline'да видео клипларни танланг.", "Сначала выделите видеоклипы на таймлайне."),
    "Reading the video... you can keep working.": ("Видео ўқилмоқда... ишлашда давом этишингиз мумкин.", "Видео читается... можно продолжать работу."),
    "Scene changes found: %1.": ("Топилган саҳна алмашишлари: %1.", "Найдено смен сцен: %1."),
    "No scene changes found in the selected clips.": ("Танланган клипларда саҳна алмашиши топилмади.", "В выбранных клипах смен сцен не найдено."),
})

# Phase 15a: export for other editors
T.update({
    "Export for other editors": ("Бошқа дастурлар учун экспорт", "Экспорт для других монтажных программ"),
    "Export for other editors...": ("Бошқа дастурлар учун экспорт...", "Экспорт для других монтажных программ..."),
    "Save the main sequence as XML, OpenTimelineIO or EDL for Premiere Pro, DaVinci Resolve and other editors": (
        "Асосий секвенцияни Premiere Pro, DaVinci Resolve ва бошқа дастурлар учун XML, OpenTimelineIO ёки EDL сифатида сақлаш",
        "Сохранить основной эпизод как XML, OpenTimelineIO или EDL для Premiere Pro, DaVinci Resolve и других программ"),
    "Saved for other editors: %1": ("Бошқа дастурлар учун сақланди: %1", "Сохранено для других программ: %1"),
    "Could not write the file %1: %2": ("%1 файлини ёзиб бўлмади: %2", "Не удалось записать файл %1: %2"),
    "Final Cut Pro 7 XML (Premiere Pro, DaVinci Resolve)": (
        "Final Cut Pro 7 XML (Premiere Pro, DaVinci Resolve)", "Final Cut Pro 7 XML (Premiere Pro, DaVinci Resolve)"),
    "OpenTimelineIO (.otio)": ("OpenTimelineIO (.otio)", "OpenTimelineIO (.otio)"),
    "EDL (CMX 3600): video track V1 only": ("EDL (CMX 3600): фақат V1 видео йўлаги", "EDL (CMX 3600): только видеодорожка V1"),
    "Writes the main sequence for Premiere Pro, DaVinci Resolve and other editors. Only the edit travels: which part of which file lies where. The media files are not copied; keep them where they are.": (
        "Асосий секвенцияни Premiere Pro, DaVinci Resolve ва бошқа дастурлар учун ёзади. Фақат монтажнинг ўзи ўтади: қайси файлнинг қайси қисми қаерда туриши. Медиа файллар нусхаланмайди; уларни жойида қолдиринг.",
        "Записывает основной эпизод для Premiere Pro, DaVinci Resolve и других программ. Переносится только сам монтаж: какая часть какого файла где стоит. Медиафайлы не копируются; оставьте их на месте."),
    "Format:": ("Формат:", "Формат:"),
    "Text titles: %1": ("Матн титрлари: %1", "Текстовые титры: %1"),
    "Picture effects, chroma key and masks: %1 clips": ("Тасвир эффектлари, chroma key ва ниқоблар: %1 клип", "Эффекты изображения, хромакей и маски: клипов — %1"),
    "Colour correction and LUTs: %1 clips": ("Ранг тузатиш ва LUT: %1 клип", "Цветокоррекция и LUT: клипов — %1"),
    "Position, scale, rotation and opacity: %1 clips": ("Жой, ўлчам, бурилиш ва шаффофлик: %1 клип", "Положение, масштаб, поворот и непрозрачность: клипов — %1"),
    "Speed changes: %1 clips (they play at normal speed from the same first frame)": (
        "Тезлик ўзгариши: %1 клип (улар ўша биринчи кадрдан оддий тезликда ўйнайди)",
        "Изменение скорости: клипов — %1 (они играют с обычной скоростью с того же первого кадра)"),
    "Dips to black or white and wipes: %1 (they become cross dissolves)": (
        "Қорага ёки оққа ўтиш ва wipe: %1 (улар оддий dissolve бўлади)",
        "Переходы через чёрный или белый и шторки: %1 (станут обычным растворением)"),
    "Transitions: %1": ("Ўтишлар: %1", "Переходы: %1"),
    "Fades at free clip ends: %1": ("Клипларнинг бўш учидаги fade: %1", "Затухания на свободных краях клипов: %1"),
    "Sound fades in and out: %1 clips": ("Овознинг fade in ва fade out'и: %1 клип", "Нарастание и затухание звука: клипов — %1"),
    "Clip volume: %1 clips": ("Клип овоз баландлиги: %1 клип", "Громкость клипа: клипов — %1"),
    "Sound effects (equalizer, compressor, noise, hum): %1 clips": (
        "Овоз эффектлари (эквалайзер, компрессор, шовқин, гувиллаш): %1 клип",
        "Звуковые эффекты (эквалайзер, компрессор, шум, гул): клипов — %1"),
    "Track volume and balance, master volume": ("Йўлак овози ва баланси, мастер овоз", "Громкость и баланс дорожек, мастер-громкость"),
    "Subtitles: %1 (save them with Export SRT in the Subtitles tab)": (
        "Субтитрлар: %1 (уларни «Субтитрлар» панелидаги «SRT сақлаш» тугмаси билан сақланг)",
        "Субтитры: %1 (сохраните их кнопкой «Экспорт SRT» на вкладке «Субтитры»)"),
    "Markers: %1": ("Маркерлар: %1", "Маркеры: %1"),
    "Clips on tracks other than V1, and all sound: %1": ("V1 дан бошқа йўлаклардаги клиплар ва бутун овоз: %1", "Клипы на дорожках кроме V1 и весь звук: %1"),
    "Everything in this sequence will be transferred.": ("Бу секвенциядаги ҳамма нарса ўтказилади.", "Всё в этом эпизоде будет перенесено."),
    "Will not be transferred:": ("Ўтказилмайди:", "Не будет перенесено:"),
    "Clips to write: %1": ("Ёзиладиган клиплар: %1", "Клипов к записи: %1"),
    "Nested and multicamera clips: %1. They are written as the clips inside them.": (
        "Nested ва мультикам клиплар: %1. Улар ичидаги клиплар сифатида ёзилади.",
        "Вложенные и мультикамерные клипы: %1. Они записываются как клипы внутри них."),
    "Missing media files: %1. The other editor will ask for them.": (
        "Топилмаган медиа файллар: %1. Бошқа дастур уларни сўрайди.",
        "Отсутствующие медиафайлы: %1. Другая программа запросит их."),
    "Final Cut Pro XML (*.xml)": ("Final Cut Pro XML (*.xml)", "Final Cut Pro XML (*.xml)"),
    "OpenTimelineIO (*.otio)": ("OpenTimelineIO (*.otio)", "OpenTimelineIO (*.otio)"),
    "EDL (*.edl)": ("EDL (*.edl)", "EDL (*.edl)"),
    "Export for other editors: Final Cut Pro XML, OpenTimelineIO, EDL": (
        "Бошқа дастурлар учун экспорт: Final Cut Pro XML, OpenTimelineIO, EDL",
        "Экспорт для других программ: Final Cut Pro XML, OpenTimelineIO, EDL"),
    "File > Export for other editors writes the main sequence for Premiere Pro, DaVinci Resolve and other editors: which part of which file lies where on which track, cross dissolves, markers and (XML) clip volume. Nested and multicamera clips become the clips inside them. Before saving, the window lists what will not be transferred: titles, effects, colour, motion, speed changes, subtitles and more. The EDL holds video track V1 only. Opening the files in Premiere Pro and DaVinci Resolve has not been tested by us.": (
        "Файл > Бошқа дастурлар учун экспорт асосий секвенцияни Premiere Pro, DaVinci Resolve ва бошқа дастурлар учун ёзади: қайси файлнинг қайси қисми қайси йўлакда қаерда туриши, cross dissolve ўтишлари, маркерлар ва (XML'да) клип овози. Nested ва мультикам клиплар ичидаги клипларга айланади. Сақлашдан олдин ойна нима ўтказилмаслигини кўрсатади: титрлар, эффектлар, ранг, ҳаракат, тезлик ўзгариши, субтитрлар ва бошқалар. EDL'да фақат V1 видео йўлаги бўлади. Файлларни Premiere Pro ва DaVinci Resolve'да очиш биз томондан синалмаган.",
        "Файл > Экспорт для других монтажных программ записывает основной эпизод для Premiere Pro, DaVinci Resolve и других программ: какая часть какого файла где лежит на какой дорожке, растворения, маркеры и (в XML) громкость клипа. Вложенные и мультикамерные клипы становятся клипами внутри них. Перед сохранением окно показывает, что не будет перенесено: титры, эффекты, цвет, движение, изменение скорости, субтитры и другое. EDL содержит только видеодорожку V1. Открытие файлов в Premiere Pro и DaVinci Resolve нами не проверялось."),
    "Import from other editors (XML, OpenTimelineIO, EDL)": (
        "Бошқа дастурлардан импорт (XML, OpenTimelineIO, EDL)", "Импорт из других программ (XML, OpenTimelineIO, EDL)"),
    "Audio plugins (VST3)": ("Аудио плагинлар (VST3)", "Аудиоплагины (VST3)"),
})

# Phase 15b: import from other editors
T.update({
    "Import from other editors": ("Бошқа дастурлардан импорт", "Импорт из других монтажных программ"),
    "Import from other editors...": ("Бошқа дастурлардан импорт...", "Импорт из других монтажных программ..."),
    "Open a sequence from Premiere Pro, DaVinci Resolve or another editor: Final Cut Pro XML, OpenTimelineIO or EDL": (
        "Premiere Pro, DaVinci Resolve ёки бошқа дастурда қилинган секвенцияни очиш: Final Cut Pro XML, OpenTimelineIO ёки EDL",
        "Открыть эпизод из Premiere Pro, DaVinci Resolve или другой программы: Final Cut Pro XML, OpenTimelineIO или EDL"),
    "Timelines (*.xml *.otio *.edl)": ("Монтаж файллари (*.xml *.otio *.edl)", "Файлы монтажа (*.xml *.otio *.edl)"),
    "Imported from %1: %2 clips": ("%1 дан импорт қилинди: %2 клип", "Импортировано из %1: клипов — %2"),
    "Could not open the file %1: %2": ("%1 файлини очиб бўлмади: %2", "Не удалось открыть файл %1: %2"),
    "This file could not be read as a timeline.": ("Бу файлни монтаж сифатида ўқиб бўлмади.", "Этот файл не удалось прочитать как монтаж."),
    "The file holds no clips.": ("Файлда клиплар йўқ.", "В файле нет клипов."),
    "The clips could not be placed: the first video and audio tracks are locked.": (
        "Клипларни қўйиб бўлмади: биринчи видео ва аудио йўлаклар қулфланган.",
        "Клипы не удалось разместить: первые видео- и аудиодорожки заблокированы."),
    "As one nested clip at the playhead (double-click it to edit)": (
        "Курсор жойига битта nested клип бўлиб (таҳрирлаш учун уни икки марта босинг)",
        "Одним вложенным клипом под указателем (дважды щёлкните, чтобы редактировать)"),
    "Clips straight onto the open sequence at the playhead": (
        "Клиплар тўғридан-тўғри очиқ секвенцияга, курсор жойига",
        "Клипы прямо в открытый эпизод под указатель"),
    "Import": ("Импорт", "Импорт"),
    "File: %1": ("Файл: %1", "Файл: %1"),
    "Final Cut Pro XML": ("Final Cut Pro XML", "Final Cut Pro XML"),
    "OpenTimelineIO": ("OpenTimelineIO", "OpenTimelineIO"),
    "EDL (CMX 3600)": ("EDL (CMX 3600)", "EDL (CMX 3600)"),
    "Format: %1": ("Формат: %1", "Формат: %1"),
    "Clips: %1 on %2 video and %3 audio tracks": ("Клиплар: %1 та, %2 видео ва %3 аудио йўлакда", "Клипов: %1 на %2 видео- и %3 аудиодорожках"),
    "Media files: %1 found, %2 not found. Files not found stay offline; relink them later in the media bin.": (
        "Медиа файллар: %1 таси топилди, %2 таси топилмади. Топилмаганлари офлайн бўлиб қолади; уларни кейин медиа кутубхонасида қайта боғланг.",
        "Медиафайлы: найдено %1, не найдено %2. Ненайденные останутся офлайн; привяжите их позже в медиатеке."),
    "An EDL has no frame rate: the sequence rate (%1 fps) is used.": (
        "EDL'да кадр тезлиги йўқ: секвенция тезлиги (%1 fps) олинади.",
        "В EDL нет частоты кадров: используется частота эпизода (%1 fps)."),
    "EDL source times are timecodes: for camera files with their own start timecode the clips may show the wrong part. XML and OpenTimelineIO do not have this problem.": (
        "EDL'даги манба вақтлари timecode: ўз бошланғич timecode'и бор камера файлларида клип нотўғри қисмни кўрсатиши мумкин. XML ва OpenTimelineIO'да бу муаммо йўқ.",
        "Время источника в EDL — это таймкод: у файлов камеры с собственным начальным таймкодом клип может показать не ту часть. В XML и OpenTimelineIO такой проблемы нет."),
    "The file runs at %1 fps, the sequence at %2 fps: times are converted to the sequence.": (
        "Файл %1 fps, секвенция %2 fps: вақтлар секвенцияга ўгирилади.",
        "Файл — %1 fps, эпизод — %2 fps: время пересчитывается под эпизод."),
    "Everything in the file will be imported.": ("Файлдаги ҳамма нарса импорт қилинади.", "Всё из файла будет импортировано."),
    "Not imported:": ("Импорт қилинмайди:", "Не импортируется:"),
    "Titles, colour mattes and other generated clips: %1": ("Титрлар, рангли фонлар ва бошқа ясалган клиплар: %1", "Титры, цветные подложки и другие генерируемые клипы: %1"),
    "Effects, motion or colour settings: %1 clips": ("Эффектлар, ҳаракат ёки ранг созламалари: %1 клип", "Эффекты, движение или настройки цвета: клипов — %1"),
    "Speed changes: %1 clips (they play at normal speed)": ("Тезлик ўзгариши: %1 клип (улар оддий тезликда ўйнайди)", "Изменение скорости: клипов — %1 (они играют с обычной скоростью)"),
    "Nested sequences and multicamera clips: %1": ("Nested секвенциялар ва мультикам клиплар: %1", "Вложенные эпизоды и мультикамерные клипы: %1"),
    "Transitions other than dissolves: %1 (they become dissolves)": ("Dissolve'дан бошқа ўтишлар: %1 (улар dissolve бўлади)", "Переходы, кроме растворения: %1 (станут растворением)"),
    "Clips switched off: %1": ("Ўчириб қўйилган клиплар: %1", "Выключенные клипы: %1"),
    "Clips without a media file: %1": ("Медиа файли йўқ клиплар: %1", "Клипы без медиафайла: %1"),
    "Import from other editors: Final Cut Pro XML, OpenTimelineIO, EDL": (
        "Бошқа дастурлардан импорт: Final Cut Pro XML, OpenTimelineIO, EDL",
        "Импорт из других программ: Final Cut Pro XML, OpenTimelineIO, EDL"),
    "File > Import from other editors opens a sequence made in Premiere Pro, DaVinci Resolve or another editor: clips with their places, dissolves, markers and (XML) clip volume, as one nested clip or straight onto the open sequence at the playhead; one undo step. Media files are found by their path, next to the file or in the bin; files not found stay offline for relinking. Effects, titles, speed changes and nested sequences of the other editor are not imported; the window lists them first. Files from Premiere Pro and DaVinci Resolve themselves have not been tested by us.": (
        "Файл > Бошқа дастурлардан импорт Premiere Pro, DaVinci Resolve ёки бошқа дастурда қилинган секвенцияни очади: клиплар ўз жойи билан, dissolve ўтишлари, маркерлар ва (XML'да) клип овози; битта nested клип бўлиб ёки тўғридан-тўғри очиқ секвенцияга, курсор жойига; битта бекор қилиш қадами. Медиа файллар йўли бўйича, файл ёнидан ёки кутубхонадан топилади; топилмаганлари қайта боғлаш учун офлайн қолади. Бошқа дастурдаги эффектлар, титрлар, тезлик ўзгариши ва nested секвенциялар импорт қилинмайди; ойна буни олдиндан кўрсатади. Premiere Pro ва DaVinci Resolve'нинг ўзидан олинган файллар биз томондан синалмаган.",
        "Файл > Импорт из других монтажных программ открывает эпизод из Premiere Pro, DaVinci Resolve или другой программы: клипы на своих местах, растворения, маркеры и (в XML) громкость клипа; одним вложенным клипом или прямо в открытый эпизод под указатель; один шаг отмены. Медиафайлы находятся по пути, рядом с файлом или в медиатеке; ненайденные остаются офлайн для привязки. Эффекты, титры, изменение скорости и вложенные эпизоды другой программы не импортируются; окно заранее показывает это. Файлы из самих Premiere Pro и DaVinci Resolve нами не проверялись."),
})

# Phase 15c: VST 3 audio plug-ins
T.update({
    "VST 3 plug-in": ("VST 3 плагин", "Плагин VST 3"),
    "Look for VST 3 plug-ins again. Found: %1": ("VST 3 плагинларни қайта қидириш. Топилди: %1", "Снова найти плагины VST 3. Найдено: %1"),
    "%1 (%2)": ("%1 (%2)", "%1 (%2)"),
    "VST 3: %1": ("VST 3: %1", "VST 3: %1"),
    "Find plug-ins": ("Плагинларни топиш", "Найти плагины"),
    "Look for VST 3 plug-ins again": ("VST 3 плагинларни қайта қидириш", "Снова найти плагины VST 3"),
    "%1 (VST 3)": ("%1 (VST 3)", "%1 (VST 3)"),
    'The plug-in "%1" is not on this computer or did not start, so the sound passes unchanged. Install it and press "Find plug-ins".\n%2': (
        "«%1» плагини бу компьютерда йўқ ёки ишга тушмади, шунинг учун овоз ўзгаришсиз ўтади. Уни ўрнатинг ва «Плагинларни топиш»ни босинг.\n%2",
        "Плагина «%1» нет на этом компьютере, или он не запустился, поэтому звук проходит без изменений. Установите его и нажмите «Найти плагины».\n%2"),
    "%1, %2": ("%1, %2", "%1, %2"),
    "This plug-in has no values to set here.": ("Бу плагинда бу ерда созланадиган қиймат йўқ.", "У этого плагина нет значений для настройки здесь."),
    "Only the first %1 of %2 values are shown. The plug-in's own window comes in a later version.": (
        "%2 та қийматдан фақат биринчи %1 таси кўрсатилган. Плагиннинг ўз ойнаси кейинги версияда келади.",
        "Показаны только первые %1 из %2 значений. Собственное окно плагина появится в следующей версии."),
    "The plug-in did not start: %1": ("Плагин ишга тушмади: %1", "Плагин не запустился: %1"),
    "Plug-ins work on sound clips.": ("Плагинлар овоз клипларида ишлайди.", "Плагины работают со звуковыми клипами."),
    "Add plug-in": ("Плагин қўшиш", "Добавить плагин"),
    "BUKHARA CINEMA uses the following open-source components. Except the VST 3 SDK, which is built into the program, they are linked dynamically (as separate DLL files), so they can be replaced with compatible versions.": (
        "BUKHARA CINEMA қуйидаги очиқ кодли компонентлардан фойдаланади. Дастур ичига қурилган VST 3 SDK'дан ташқари, улар алоҳида DLL файллар сифатида уланган, шунинг учун уларни мос версиялар билан алмаштириш мумкин.",
        "BUKHARA CINEMA использует следующие компоненты с открытым кодом. Кроме встроенного в программу VST 3 SDK, они подключены динамически (отдельными DLL), поэтому их можно заменить совместимыми версиями."),
    "Hosting VST 3 audio plug-ins (built into the program)": ("VST 3 аудио плагинларни ишлатиш (дастур ичига қурилган)", "Работа с аудиоплагинами VST 3 (встроено в программу)"),
    # Steinberg's attribution stays in English in every language.
    "VST is a registered trademark of Steinberg Media Technologies GmbH.": (
        "VST is a registered trademark of Steinberg Media Technologies GmbH.",
        "VST is a registered trademark of Steinberg Media Technologies GmbH."),
    "Works with VST® 3 audio plug-ins.": ("VST® 3 аудио плагинлар билан ишлайди.", "Работает с аудиоплагинами VST® 3."),
    "VST 3 audio plug-ins": ("VST 3 аудио плагинлар", "Аудиоплагины VST 3"),
    "Audio effects installed on the computer as VST 3 plug-ins are listed in the Effects panel of a sound clip (standard VST 3 folder; Find plug-ins looks again). Their values appear as sliders and switches, change in one undo step, are saved in the project and work in playback and export. A plug-in missing on another computer lets the sound pass unchanged. Tested in CI with a small test plug-in built from the Steinberg SDK; commercial plug-ins have not been tested by us.": (
        "Компьютерга VST 3 плагин сифатида ўрнатилган аудио эффектлар овоз клипининг «Эффектлар» панелида кўринади (стандарт VST 3 папкаси; «Плагинларни топиш» қайта қидиради). Қийматлари слайдер ва белгилар бўлиб чиқади, битта бекор қилиш қадамида ўзгаради, лойиҳада сақланади, ижро ва экспортда ишлайди. Бошқа компьютерда плагин бўлмаса овоз ўзгаришсиз ўтади. CI'да Steinberg SDK'дан йиғилган кичик синов плагини билан текширилган; тижорат плагинлари биз томондан синалмаган.",
        "Аудиоэффекты, установленные на компьютере как плагины VST 3, показываются в панели «Эффекты» звукового клипа (стандартная папка VST 3; «Найти плагины» ищет заново). Их значения — ползунки и переключатели, меняются одним шагом отмены, сохраняются в проекте и работают при воспроизведении и экспорте. Если плагина нет на другом компьютере, звук проходит без изменений. Проверено в CI небольшим тестовым плагином, собранным из Steinberg SDK; коммерческие плагины нами не проверялись."),
    "Author: %1": ("Муаллиф: %1", "Автор: %1"),
    "VST 3 plug-in windows and crash protection": ("VST 3 плагин ойналари ва ишдан чиқишдан ҳимоя", "Окна плагинов VST 3 и защита от сбоев"),
})

# Phase 15c-2: plug-ins in the host program, plug-in windows
T.update({
    'The plug-in "%1" stopped working (%2). BUKHARA CINEMA keeps running; the sound of this effect passes unchanged until the plug-in is started again.': (
        "«%1» плагини ишдан чиқди (%2). BUKHARA CINEMA ишлашда давом этади; плагин қайта ишга туширилгунча бу эффектнинг овози ўзгаришсиз ўтади.",
        "Плагин «%1» перестал работать (%2). BUKHARA CINEMA продолжает работу; звук этого эффекта проходит без изменений, пока плагин не будет запущен снова."),
    "it stopped answering": ("жавоб бермай қолди", "перестал отвечать"),
    "it crashed": ("хато билан ёпилди", "аварийно завершился"),
    "Start the plug-in again": ("Плагинни қайта ишга тушириш", "Запустить плагин снова"),
    "Open plug-in window": ("Плагин ойнасини очиш", "Открыть окно плагина"),
    "Plug-in windows work in the Windows version of the program.": (
        "Плагин ойналари дастурнинг Windows версиясида ишлайди.",
        "Окна плагинов работают в версии программы для Windows."),
    "This plug-in has no window of its own.": ("Бу плагиннинг ўз ойнаси йўқ.", "У этого плагина нет собственного окна."),
    "What you change in the plug-in window is saved in the project and can be undone.": (
        "Плагин ойнасида ўзгартирганингиз лойиҳада сақланади ва бекор қилиш мумкин.",
        "Изменения в окне плагина сохраняются в проекте, и их можно отменить."),
    "The plug-in window did not open: %1": ("Плагин ойнаси очилмади: %1", "Окно плагина не открылось: %1"),
    "Only the first %1 of %2 values are shown here.": (
        "Бу ерда %2 та қийматдан фақат биринчи %1 таси кўрсатилган.",
        "Здесь показаны только первые %1 из %2 значений."),
    "Each plug-in runs in a small program of its own (BukharaPluginHost). A plug-in that crashes or stops answering ends only that program: BUKHARA CINEMA keeps running, the sound passes unchanged and the Effects panel offers to start the plug-in again. Open plug-in window shows the plug-in's own window (Windows); what is changed there is saved in the project, can be undone and is heard in playback and export. Tested in CI with a test plug-in that crashes and hangs on purpose; commercial plug-ins have not been tested by us.": (
        "Ҳар бир плагин ўзининг кичик дастурида ишлайди (BukharaPluginHost). Плагин ишдан чиқса ёки жавоб бермай қолса, фақат ўша дастур ёпилади: BUKHARA CINEMA ишлашда давом этади, овоз ўзгаришсиз ўтади ва «Эффектлар» панели плагинни қайта ишга туширишни таклиф қилади. «Плагин ойнасини очиш» плагиннинг ўз ойнасини кўрсатади (Windows); у ерда ўзгартирилгани лойиҳада сақланади, бекор қилинади, ижро ва экспортда эшитилади. CI'да атайлаб ишдан чиқадиган ва қотадиган синов плагини билан текширилган; тижорат плагинлари биз томондан синалмаган.",
        "Каждый плагин работает в собственной небольшой программе (BukharaPluginHost). Если плагин аварийно завершается или перестаёт отвечать, закрывается только эта программа: BUKHARA CINEMA продолжает работу, звук проходит без изменений, а панель «Эффекты» предлагает запустить плагин снова. «Открыть окно плагина» показывает собственное окно плагина (Windows); изменения в нём сохраняются в проекте, отменяются и слышны при воспроизведении и экспорте. Проверено в CI тестовым плагином, который намеренно падает и зависает; коммерческие плагины нами не проверялись."),
})

# Phase 16a: automatic multicamera edit
T.update({
    "Automatic multicamera edit": ("Автоматик мультикам монтаж", "Автоматический многокамерный монтаж"),
    "Automatic multicamera edit...": ("Автоматик мультикам монтаж...", "Автоматический многокамерный монтаж..."),
    "Automatic multicamera edit done. Ctrl+Z undoes it.": (
        "Автоматик мультикам монтаж тайёр. Ctrl+Z уни бекор қилади.",
        "Автоматический многокамерный монтаж готов. Ctrl+Z отменяет его."),
    "Cut a multicamera clip between its cameras by who is speaking, or in a rhythm": (
        "Мультикам клипни камералар орасида ким гапираётганига қараб ёки ритм бўйича кесиш",
        "Нарезать многокамерный клип между камерами по говорящему или в ритме"),
    "Whoever is speaking (each camera's own sound)": (
        "Ким гапираётган бўлса (ҳар бир камеранинг ўз овози)",
        "Кто говорит (собственный звук каждой камеры)"),
    "In a rhythm (cameras in turn)": ("Ритм бўйича (камералар навбат билан)", "В ритме (камеры по очереди)"),
    "Camera %1: %2": ("%1-камера: %2", "Камера %1: %2"),
    "Shown when nobody speaks or several people speak at once, and between long shots": (
        "Ҳеч ким гапирмаганда ёки бир неча киши бирдан гапирганда ва узун кадрлар орасида кўрсатилади",
        "Показывается, когда никто не говорит или говорят несколько человек сразу, и между длинными планами"),
    "No limit": ("Чекланмаган", "Без ограничения"),
    "How much louder the speaker's camera must hear than the others. Lower switches more readily.": (
        "Гапираётган одамнинг камераси қолганларидан қанча баланд эшитиши керак. Кичикроқ қиймат камерани тезроқ алмаштиради.",
        "Насколько громче камера говорящего должна слышать, чем остальные. Меньшее значение переключает охотнее."),
    "Switch by:": ("Алмаштириш:", "Переключать:"),
    "Wide shot:": ("Умумий план:", "Общий план:"),
    "Shortest shot:": ("Энг қисқа кадр:", "Самый короткий план:"),
    "Longest shot:": ("Энг узун кадр:", "Самый длинный план:"),
    "Speaker louder by:": ("Гапирувчи баландроқ:", "Говорящий громче на:"),
    "Works best when each camera (or each person's microphone) records its own sound: the camera that hears a person loudest shows that person. It works on this computer, without the internet; the sound of each recording is read once and remembered. The result is one undo step, and every cut can be changed afterwards with the camera keys 1-9 or in the Clip panel.": (
        "Ҳар бир камера (ёки ҳар бир одамнинг микрофони) ўз овозини ёзса, энг яхши ишлайди: одамни энг баланд эшитган камера уни кўрсатади. Бу компьютерда, интернетсиз ишлайди; ҳар бир ёзувнинг овози бир марта ўқилади ва эслаб қолинади. Натижа битта бекор қилиш қадами; ҳар бир кесимни кейин 1-9 камера тугмалари ёки «Клип» панели орқали ўзгартириш мумкин.",
        "Лучше всего работает, когда каждая камера (или микрофон каждого человека) записывает свой звук: камера, которая громче всех слышит человека, показывает его. Работает на этом компьютере, без интернета; звук каждой записи читается один раз и запоминается. Результат — один шаг отмены, каждую склейку можно потом изменить клавишами камер 1-9 или в панели «Клип»."),
    "Edit": ("Монтаж қилиш", "Смонтировать"),
    "Put the playhead on a multicamera clip, or select multicamera clips on the timeline first (Sequence > Create multicamera sequence makes one).": (
        "Аввал курсорни мультикам клип устига қўйинг ёки timeline'да мультикам клипларни танланг (уни «Секвенция > Мультикам секвенция яратиш» ясайди).",
        "Сначала поставьте курсор на многокамерный клип или выделите многокамерные клипы на таймлайне (их создаёт «Эпизод > Создать многокамерный эпизод»)."),
    "Listening to the cameras... you can keep working.": (
        "Камералар тингланмоқда... ишлашни давом эттиришингиз мумкин.",
        "Прослушиваем камеры... можно продолжать работу."),
    "The sound of the cameras could not be read.": ("Камераларнинг овозини ўқиб бўлмади.", "Не удалось прочитать звук камер."),
    "%1 %2%": ("%1 %2%", "%1 %2%"),
    "Cuts: %1. On screen: %2.": ("Кесимлар: %1. Экранда: %2.", "Склеек: %1. На экране: %2."),
    "No cuts with these settings.": ("Бу созламалар билан кесим йўқ.", "С этими настройками склеек нет."),
    'The cameras hear almost the same sound, so who is speaking is hard to tell. Try "In a rhythm", a lower "Speaker louder by", or choose a wide shot.': (
        "Камералар деярли бир хил овоз эшитади, шунинг учун ким гапираётганини аниқлаш қийин. «Ритм бўйича»ни, кичикроқ «Гапирувчи баландроқ»ни синаб кўринг ёки умумий планни танланг.",
        "Камеры слышат почти одинаковый звук, поэтому трудно понять, кто говорит. Попробуйте «В ритме», меньшее «Говорящий громче на» или выберите общий план."),
    "Sequence > Automatic multicamera edit cuts a multicamera clip between its cameras: by whoever is speaking (the camera that hears a person loudest shows that person; a wide shot when nobody or several people speak, and between long shots) or in a rhythm. Shortest and longest shot can be set; the result is one undo step and every cut can be changed afterwards. Works best when each camera or person has its own microphone. Without the internet and without an AI model; tested in CI with recordings made for the test, not with real interviews.": (
        "«Секвенция > Автоматик мультикам монтаж» мультикам клипни камералар орасида ўзи кесади: ким гапираётган бўлса (одамни энг баланд эшитган камера уни кўрсатади; ҳеч ким ёки бир неча киши гапирса ва узун кадрлар орасида — умумий план) ёки ритм бўйича. Энг қисқа ва энг узун кадр созланади; натижа битта бекор қилиш қадами, ҳар бир кесимни кейин ўзгартириш мумкин. Ҳар бир камера ёки одамнинг ўз микрофони бўлса яхши ишлайди. Интернетсиз ва AI моделсиз; CI'да тест учун ясалган ёзувлар билан текширилган, ҳақиқий интервьюлар билан эмас.",
        "«Эпизод > Автоматический многокамерный монтаж» сам режет многокамерный клип между камерами: по говорящему (камера, которая громче всех слышит человека, показывает его; общий план, когда никто или несколько человек говорят, и между длинными планами) или в ритме. Настраиваются самый короткий и самый длинный план; результат — один шаг отмены, каждую склейку можно потом изменить. Лучше всего работает, когда у каждой камеры или человека свой микрофон. Без интернета и без AI-модели; проверено в CI на записях, сделанных для теста, а не на реальных интервью."),
})

# Phase 14b: subtitles from speech (Vosk)
T.update({
    "Edit by transcript (delete words to cut)...": (
        "Матн орқали монтаж (сўзни ўчириб кесиш)...", "Монтаж по тексту (удалить слова, чтобы вырезать)..."),
    "Edit by transcript: delete words to cut the video": (
        "Матн орқали монтаж: сўзни ўчириб видеони кесиш", "Монтаж по тексту: удаление слов вырезает видео"),
    "Subtitles from speech": ("Нутқдан субтитр", "Субтитры из речи"),
    "Subtitles from speech: %1 added.": ("Нутқдан субтитр: %1 та қўшилди.", "Субтитры из речи: добавлено %1."),
    "Hear the clips on this computer and write what is said as subtitles": (
        "Клипларни шу компьютерда тинглаб, айтилганларни субтитр қилиб ёзиш",
        "Прослушать клипы на этом компьютере и записать сказанное субтитрами"),
    "Speech recognition for subtitles (libvosk.dll; models downloaded separately)": (
        "Субтитр учун нутқни таниш (libvosk.dll; моделлар алоҳида юкланади)",
        "Распознавание речи для субтитров (libvosk.dll; модели загружаются отдельно)"),
    " letters": (" ҳарф", " букв"),
    "%1: download (%2 MB)": ("%1: юклаб олиш (%2 МБ)", "%1: загрузить (%2 МБ)"),
    "Cannot write %1.": ("%1 га ёзиб бўлмади.", "Не удаётся записать %1."),
    "Clips to hear: %1.": ("Тингланадиган клиплар: %1.", "Клипов для прослушивания: %1."),
    "Download a model for this language first, or choose a model folder.": (
        "Аввал бу тил учун моделни юклаб олинг ёки модел папкасини танланг.",
        "Сначала загрузите модель для этого языка или выберите папку модели."),
    "Download": ("Юклаб олиш", "Загрузить"),
    "Downloading the model...": ("Модел юкланмоқда...", "Загрузка модели..."),
    "English": ("Инглизча", "Английский"),
    "Russian": ("Русча", "Русский"),
    "Uzbek": ("Ўзбекча", "Узбекский"),
    "Other language": ("Бошқа тил", "Другой язык"),
    "Folder of a Vosk model": ("Vosk модели папкаси", "Папка модели Vosk"),
    "Language:": ("Тил:", "Язык:"),
    "Line length:": ("Қатор узунлиги:", "Длина строки:"),
    "Listening... you can keep working. A minute of sound takes about half a minute.": (
        "Тингланмоқда... ишлашни давом эттиришингиз мумкин. Бир дақиқалик овоз тахминан ярим дақиқада тингланади.",
        "Идёт прослушивание... можно продолжать работу. Минута звука занимает около полминуты."),
    "Make subtitles": ("Субтитр ясаш", "Создать субтитры"),
    "Model folder...": ("Модел папкаси...", "Папка модели..."),
    "No speech was found in these clips.": ("Бу клипларда нутқ топилмади.", "В этих клипах речь не найдена."),
    "Replace the subtitles there were where these clips are": (
        "Бу клиплар турган жойдаги эски субтитрларни алмаштириш",
        "Заменить прежние субтитры там, где эти клипы"),
    "Select the clips to hear on the timeline (or none: then the first audio track is heard).": (
        "Тингланадиган клипларни timeline'да танланг (ҳеч бирини танламасангиз, биринчи аудио йўлак тингланади).",
        "Выделите на таймлайне клипы для прослушивания (если ничего не выделено, прослушивается первая аудиодорожка)."),
    "Speech could not be recognized: %1": ("Нутқни таниб бўлмади: %1", "Не удалось распознать речь: %1"),
    "Speech is recognized on this computer with Vosk (free, Apache 2.0); the sound is not sent anywhere. A model is downloaded once (about 40-50 MB) from alphacephei.com. About one word in eight may be wrong: read the subtitles through in the Subtitles panel. Each recording is heard once and remembered.": (
        "Нутқ шу компьютерда Vosk (бепул, Apache 2.0) билан танилади; овоз ҳеч қаёққа юборилмайди. Модел alphacephei.com'дан бир марта юкланади (тахминан 40–50 МБ). Ҳар 8 сўздан бири хато бўлиши мумкин: субтитрларни «Субтитрлар» панелида ўқиб чиқинг. Ҳар бир ёзув бир марта тингланади ва эслаб қолинади.",
        "Речь распознаётся на этом компьютере с помощью Vosk (бесплатно, Apache 2.0); звук никуда не отправляется. Модель загружается один раз (около 40–50 МБ) с alphacephei.com. Примерно каждое восьмое слово может быть неверным: прочитайте субтитры в панели «Субтитры». Каждая запись прослушивается один раз и запоминается."),
    "Stopped.": ("Тўхтатилди.", "Остановлено."),
    "Subtitles added: %1. Ctrl+Z removes them.": ("Субтитрлар қўшилди: %1. Ctrl+Z уларни олиб ташлайди.", "Добавлено субтитров: %1. Ctrl+Z удаляет их."),
    "The model could not be downloaded: %1": ("Моделни юклаб бўлмади: %1", "Не удалось загрузить модель: %1"),
    "The model could not be unpacked: %1": ("Моделни очиб бўлмади: %1", "Не удалось распаковать модель: %1"),
    "The speech recognition library (libvosk) is missing: %1": (
        "Нутқни таниш кутубхонаси (libvosk) йўқ: %1", "Нет библиотеки распознавания речи (libvosk): %1"),
    "This folder holds no Vosk model (no am/final.mdl in it).": (
        "Бу папкада Vosk модели йўқ (ичида am/final.mdl йўқ).", "В этой папке нет модели Vosk (нет am/final.mdl)."),
    "Unpacking the model...": ("Модел очилмоқда...", "Распаковка модели..."),
    "Use a Vosk model you already have (an unpacked folder)": (
        "Ўзингизда бор Vosk моделини ишлатиш (очилган папка)", "Использовать уже имеющуюся модель Vosk (распакованная папка)"),
    "Write Uzbek in Cyrillic letters": ("Ўзбекчани кирилл ҳарфларида ёзиш", "Писать узбекский кириллицей"),
    "Graphics > Subtitles from speech hears the selected clips (or the first audio track) on this computer with Vosk (Apache 2.0) and writes what is said as subtitle lines: one undo step, then they can be corrected in the Subtitles panel. Models for Uzbek, Russian and English are downloaded once (40-50 MB) or taken from a folder; Uzbek can be written in Cyrillic letters. Nothing is sent anywhere. About one word in eight may be wrong. Tested in CI with English speech made by a speech synthesizer, not with real Uzbek recordings.": (
        "«Графика > Нутқдан субтитр» танланган клипларни (ёки биринчи аудио йўлакни) шу компьютерда Vosk (Apache 2.0) билан тинглайди ва айтилганларни субтитр қаторларига ёзади: битта бекор қилиш қадами, кейин «Субтитрлар» панелида тузатилади. Ўзбек, рус ва инглиз моделлари бир марта юкланади (40–50 МБ) ёки папкадан олинади; ўзбекча кириллда ёзилиши мумкин. Ҳеч нарса ҳеч қаёққа юборилмайди. Ҳар 8 сўздан бири хато бўлиши мумкин. CI'да нутқ синтезатори айтган инглизча гап билан текширилган, ҳақиқий ўзбекча ёзувлар билан эмас.",
        "«Графика > Субтитры из речи» прослушивает выделенные клипы (или первую аудиодорожку) на этом компьютере с помощью Vosk (Apache 2.0) и записывает сказанное строками субтитров: один шаг отмены, затем их можно исправить в панели «Субтитры». Модели для узбекского, русского и английского загружаются один раз (40–50 МБ) или берутся из папки; узбекский можно писать кириллицей. Ничего никуда не отправляется. Примерно каждое восьмое слово может быть неверным. Проверено в CI на английской речи синтезатора, а не на реальных узбекских записях."),
})

LATIN_OVERRIDES = {
    "[phase %1]": "[%1-bosqich]",
    "%1 kHz, %2": "%1 kHz, %2",
    "44.1 kHz": "44,1 kHz",
    "48 kHz": "48 kHz",
}

_MAP = {
    "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "ё": "yo", "ж": "j", "з": "z",
    "и": "i", "й": "y", "к": "k", "л": "l", "м": "m", "н": "n", "о": "o", "п": "p",
    "р": "r", "с": "s", "т": "t", "у": "u", "ф": "f", "х": "x", "ч": "ch", "ш": "sh",
    "ъ": "’", "ь": "", "э": "e", "ю": "yu", "я": "ya", "ў": "o‘", "қ": "q", "ғ": "g‘",
    "ҳ": "h", "щ": "sh", "ы": "i",
}
_VOWELS = set("аеёиоуўэюяaeiou")


# Export falls back to the encoder that needs no hardware (deep check)
T.update({
    'The export stopped at frame %1 of %2. Try another video codec.': ('Экспорт %2 кадрдан %1-кадрда тўхтади. Бошқа видео кодекни танланг.', 'Экспорт остановился на кадре %1 из %2. Выберите другой видеокодек.'),
    'The written file has no picture. Try another video codec.': ('Ёзилган файлда тасвир йўқ. Бошқа видео кодекни танланг.', 'В записанном файле нет изображения. Выберите другой видеокодек.'),
    '%1 did not work on this computer; the file was written with %2.': ('%1 бу компьютерда ишламади; файл %2 билан ёзилди.', '%1 не сработал на этом компьютере; файл записан с помощью %2.'),
})

# Why grey menu commands are off (audit)
T.update({
    'Select clips on the timeline first.': ("Аввал timeline'да клипларни танланг.", 'Сначала выделите клипы на таймлайне.'),
    'Open a clip in the Source monitor first: double-click it in the media bin.': ('Аввал клипни Source мониторида очинг: медиа кутубхонасида уни икки марта босинг.', 'Сначала откройте клип в мониторе источника: дважды щёлкните его в медиатеке.'),
    'Nothing to undo yet.': ('Ҳозирча бекор қиладиган амал йўқ.', 'Пока нечего отменять.'),
    'Nothing to redo.': ('Қайтарадиган амал йўқ.', 'Нечего повторять.'),
    'Copy or cut clips first.': ('Аввал клипларни нусхаланг ёки кесиб олинг.', 'Сначала скопируйте или вырежьте клипы.'),
    'Save the project first: older versions are kept each time it is saved again.': ('Аввал лойиҳани сақланг: ҳар сафар қайта сақлаганда эски версиялари қолади.', 'Сначала сохраните проект: при каждом новом сохранении старые версии сохраняются.'),
    'Put clips on the timeline first.': ("Аввал timeline'га клип қўйинг.", 'Сначала поместите клипы на таймлайн.'),
    'Put the playhead over a clip on the timeline first.': ("Аввал курсорни timeline'даги клип устига қўйинг.", 'Сначала поставьте курсор на клип на таймлайне.'),
    'Select audio clips on the timeline first.': ("Аввал timeline'да аудио клипларни танланг.", 'Сначала выделите аудиоклипы на таймлайне.'),
    'Select one video or audio clip on the timeline first.': ("Аввал timeline'да битта видео ёки аудио клипни танланг.", 'Сначала выделите на таймлайне один видео- или аудиоклип.'),
    'Select video clips on the timeline first.': ("Аввал timeline'да видео клипларни танланг.", 'Сначала выделите видеоклипы на таймлайне.'),
    'Select a clip next to a cut first.': ('Аввал кесим ёнидаги клипни танланг.', 'Сначала выделите клип рядом со склейкой.'),
    'Select two or more recordings of one event first (Ctrl+click): in the media bin or on the timeline.': ('Аввал бир воқеанинг иккита ёки ундан кўп ёзувини танланг (Ctrl+босиш): медиа кутубхонасида ёки timeline\'да.', 'Сначала выделите две или больше записей одного события (Ctrl+щелчок): в медиатеке или на таймлайне.'),
    'Put the playhead on a multicamera clip that has this camera (Sequence > Create multicamera sequence).': ('Курсорни шу камераси бор мультикам клип устига қўйинг («Секвенция > Мультикам секвенция яратиш»).', 'Поставьте курсор на многокамерный клип с этой камерой («Эпизод > Создать многокамерный эпизод»).'),
    'The video engine could not be loaded: see the log file.': ('Видео двигатели юкланмади: журнал файлига қаранг.', 'Не удалось загрузить видеодвижок: см. файл журнала.'),
    'Grey menu commands say what they need when you point at them. Help > Feature status lists what works.': ('Кулранг меню буйруқлари устига сичқонча келтирсангиз, нима кераклигини айтади. Ёрдам > Функциялар ҳолати нима ишлашини кўрсатади.', 'Серые команды меню показывают, что им нужно, если навести на них мышь. Справка > Состояние функций показывает, что работает.'),
})

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
        if lang == "en":
            tr.text = src
            tr.attrib.pop("type", None)
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
    for lang in ("uz_Cyrl", "uz_Latn", "ru", "en"):
        missing += fill(i18n / f"bukhara_{lang}.ts", lang)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
