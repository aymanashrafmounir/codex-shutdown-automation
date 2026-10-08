# Third-party notices

The application source is MIT licensed. The Windows package dynamically links Qt 6.8.3 (Core, Gui, Widgets, Network and Sql), distributed under LGPL-3.0, and includes the QSQLite driver. Qt copyright belongs to The Qt Company Ltd. and other contributors.

Qt license texts are included in `licenses/`. Corresponding Qt source is available from [the exact Qt 6.8.3 source archive](https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz). Qt DLLs remain separate and replaceable. You may modify or reverse engineer the application as required to debug modifications to these libraries; the MIT license imposes no restriction on that.

The MinGW runtime libraries are distributed under their upstream licenses, including the GCC Runtime Library Exception. See `licenses/GPL-3.0.txt` and `licenses/GCC-exception-3.1.txt`. Reusable build scripts use an installed compatible Qt SDK and compiler. No compiler is bundled in the installed application.

SQLite is public domain; the Qt SQL plugin's Qt terms also apply. No telemetry or external network service is part of the application's runtime.
