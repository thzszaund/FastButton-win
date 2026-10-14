# FastButton-win (CornerBtn)

**RU** · [English below](#english)

Маленькая полупрозрачная кнопка в углу экрана для Windows 10, поверх всех окон.
Нужна, чтобы не целиться в крошечную «Свернуть все окна» в углу панели задач, особенно когда мониторов несколько.
Один файл `CornerBtn.exe`, без установки, без .NET. Настройки хранятся в `CornerBtn.ini` рядом с программой.

## Возможности

- **Своя кнопка на каждом мониторе**, угол и отступ настраиваются отдельно для каждого. Если монитор отключён, его настройки хранятся и вернутся при подключении.
- **Левый клик, двойной левый клик, средняя кнопка** с разными действиями. Правый клик открывает меню.
- **Свернуть окна только на том мониторе, где нажата кнопка**, и вернуть их вторым нажатием. Окна на других виртуальных рабочих столах не трогаются; окна «поверх всех» сворачиваются (можно отключить в меню).
- **Закрепить активное окно** (пункт меню или действие `@pin`): «поверх всех окон» и/или «на всех виртуальных рабочих столах», каждый режим включается отдельно (Настройки → Закрепление окна). Закреплённые окна после смены виртуального стола поднимаются наверх (опция).
- **Исключения**: список программ, при активном окне которых кнопка на этом мониторе скрывается (игры, плееры).
- **Работа с RDP/VNC**: действия `@mondesk`, `@desktop`, `@minall` работают через API и не зависят от клавиатуры. Для клавиатурных действий (Win+Tab), если в фокусе mstsc/vncviewer, фокус сначала переходит на локальный компьютер. Настраивается.
- **Меню пользователя** с вложенными папками, разделителями и сортировкой, редактор встроен.
- Размер, цвет, прозрачность (и при наведении), форма (квадрат/круг), иконка (`.ico`, `.exe`, `.dll`).
- Shift + перетаскивание перемещает кнопку внутри её монитора.
- Автозапуск с Windows (галочка в меню).

## Использование

Скачайте `CornerBtn.exe` из [Releases](../../releases) и запустите. Кнопка 8×8 пикселей появится в углу каждого монитора (на основном слева сверху, на остальных справа сверху).

| Действие | По умолчанию |
|---|---|
| Левый клик | свернуть / вернуть окна на этом мониторе |
| Двойной клик | обзор задач (Win+Tab) |
| Средняя кнопка | нет действия |
| Правый клик | меню |

Встроенные действия: `@mondesk`, `@desktop`, `@minall`, `@taskview`, `@pin`, `@taskmgr`, `@lock`, `@none`. Любое другое значение считается путём к файлу, папке или ссылке.

## Известные ограничения

- Над приложениями в «эксклюзивном» полноэкранном режиме (часть игр) Windows не показывает чужие окна. Кнопка там не видна.
- Закрепление на всех рабочих столах использует недокументированный интерфейс Windows. На другой сборке Windows оно может не сработать; закрепление «поверх всех» при этом работает.
- Программа не подписана, Windows Defender/SmartScreen может предупредить.
- Если что-то работает не так: Настройки → «Журнал диагностики», повторите действие, затем «Скопировать журнал в буфер обмена».

## Сборка

В репозитории настроена сборка в GitHub Actions (mingw-w64, кросс-компиляция на Linux). Локально:

```sh
cd src
x86_64-w64-mingw32-windres app.rc -O coff -o app.res.o
x86_64-w64-mingw32-gcc -municode -mwindows -O2 -o CornerBtn.exe CornerBtn.c app.res.o -static \
  -lcomctl32 -lcomdlg32 -lshell32 -lole32 -loleaut32 -ladvapi32 -luser32 -lgdi32 -ldwmapi -luuid -s
```

Релиз создаётся автоматически при push тега `v*` (например `v2.0.0`).

---

## English

A tiny semi-transparent always-on-top button in a screen corner for Windows 10, a big easy target instead of the thin "Show desktop" sliver of the taskbar. Single `CornerBtn.exe`, no installer, settings in `CornerBtn.ini` next to the exe.

- One button **per monitor**, with its own corner and offset; settings are remembered for monitors that are disconnected.
- Left click / double click / middle click actions, right click opens a user-editable menu (nested folders, separators, ordering).
- **Minimize windows only on the monitor where the button was clicked** (second click restores). Windows on other virtual desktops and pinned windows are left alone.
- **Pin active window**: always on top and shown on all virtual desktops (uses undocumented Windows COM interfaces, written for Windows 10 and may not work on other builds).
- **Exclusion list**: hide the button on a monitor while a listed program is active there.
- **RDP/VNC friendly**: window actions go through APIs, and keyboard-based actions first move focus to the local machine when a remote client is active.
- Configurable size, color, opacity (and hover opacity), shape and icon. Shift+drag moves the button within its monitor.

Build: GitHub Actions cross-compiles with mingw-w64; see `.github/workflows/build.yml`. Pushing a `v*` tag publishes a release.

License: MIT
