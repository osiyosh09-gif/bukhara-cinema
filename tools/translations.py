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
    "Ctrl+Z, Ctrl+Shift+Z, Ctrl+K split, Delete, Shift+Delete ripple delete, S snapping. Custom shortcuts are planned for phase 6.": ("Ctrl+Z, Ctrl+Shift+Z, Ctrl+K бўлиш, Delete, Shift+Delete бўшлиқсиз ўчириш, S ёпишиш. Тугмаларни ўзгартириш 6-босқичда.", "Ctrl+Z, Ctrl+Shift+Z, Ctrl+K разрезать, Delete, Shift+Delete удалить со сдвигом, S привязка. Настройка клавиш — на этапе 6."),
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

# Manual Latin overrides where transliteration is not ideal.
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
