#include "windows_shutdown.h"
#include <QDir>
#include <QProcess>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace csa {
void WindowsShutdown::request() {
#ifdef Q_OS_WIN
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) throw std::runtime_error("Cannot locate the Windows shutdown command.");
    const QString executable = QString::fromWCharArray(directory) + "/shutdown.exe";
    QProcess process;
    process.setProgram(QDir::toNativeSeparators(executable));
    // A positive Windows timeout implicitly forces apps. Keep our countdown in policy.
    process.setArguments({"/s", "/t", "0", "/d", "p:0:0", "/c", "Codex work completed. One-time shutdown requested."});
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    process.start();
    if (!process.waitForStarted(3000)) throw std::runtime_error("Windows shutdown could not start.");
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error("Windows did not accept the shutdown request.");
#else
    throw std::runtime_error("Shutdown is supported on Windows only.");
#endif
}
} // namespace csa
