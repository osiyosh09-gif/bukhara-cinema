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
