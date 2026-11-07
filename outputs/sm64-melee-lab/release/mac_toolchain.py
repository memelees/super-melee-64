"""Select and test the macOS SDK used by the installer, without game data."""
from pathlib import Path
import json
import os
import shutil
import subprocess


RECOVERY = (
    'Update Apple Command Line Tools in System Settings > General > Software Update. '
    'If you use the full Xcode app, open it and finish its setup prompts. '
    'Then choose Prepare Build Tools and Build / Resume in the same build folder. '
    'Your game files and verified downloads are kept.'
)


class MacToolchainError(RuntimeError):
    def __init__(self, code, title, message, recovery=RECOVERY, detail=''):
        self.diagnostic = dict(code=code, title=title, message=message,
                               recovery=recovery, detail=detail[-6000:])
        super().__init__(title + ': ' + message + '\n' + recovery +
                         ('\nCompiler details:\n' + detail[-6000:] if detail else ''))


def compiler_problem(output, compiler='Apple'):
    lower = output.lower()
    if 'license' in lower and any(word in lower for word in ('agree', 'accept', 'review')):
        return MacToolchainError('xcode-license', 'Finish Xcode setup',
            'Apple is blocking the build tools until the Xcode licence has been reviewed.',
            'Open Xcode, review its licence and complete the setup prompts yourself. '
            'Then choose Prepare Build Tools and Build / Resume. Existing downloads are kept.', output)
    return MacToolchainError('mac-compiler', 'Mac build tools need attention',
        compiler + ' could not compile a small system-header check with the selected macOS SDK. '
        'Setup stopped before downloading game dependencies.', detail=output)


def mac_gcc(environment=None):
    environment = os.environ if environment is None else environment
    path = environment.get('PATH', os.defpath)
    explicit = environment.get('GCC_BIN')
    if explicit:
        found = shutil.which(explicit, path=path)
        return str(Path(found).absolute()) if found else None
    # Match the pinned Melee compiler wrapper; do not silently switch to Clang.
    for version in range(20, 12, -1):
        found = shutil.which('gcc-' + str(version), path=path)
        if found:
            return str(Path(found).absolute())
    return None


def _probe(command, environment, source=None):
    try:
        result = subprocess.run(command, input=source, env=environment,
                                text=True, capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise compiler_problem(str(error)) from error
    if result.returncode:
        raise compiler_problem(result.stdout + result.stderr, Path(command[0]).name)
    return result.stdout.strip()


def mac_build_environment(environment=None):
    """Return a copy with a verified SDK/GCC; never change the parent environment.

    Both compiler probes read source from stdin and use -fsyntax-only. They do
    not download anything, link/run a program, or change the build cache.
    """
    environment = dict(os.environ if environment is None else environment)
    selection = dict(environment)
    selection.pop('SDKROOT', None)  # A shell or old install may contain a stale SDK.
    developer = _probe(['/usr/bin/xcode-select', '-p'], selection)
    sdk = _probe(['/usr/bin/xcrun', '--sdk', 'macosx', '--show-sdk-path'], selection)
    if (not Path(sdk).is_absolute() or '\n' in sdk or '\r' in sdk or
            not (Path(sdk) / 'usr/include/sys/types.h').is_file()):
        raise MacToolchainError('mac-sdk', 'macOS SDK is missing or incomplete',
            'Apple\'s selected developer tools do not provide the system headers needed to build the game.',
            detail='Selected developer tools: ' + developer + '\nSelected SDK: ' + sdk)
    gcc = mac_gcc(environment)
    if not gcc:
        raise MacToolchainError('mac-gcc', 'Install the Melee C compiler',
            'A supported Homebrew GCC compiler was not found. Apple Clang cannot compile the Melee engine.',
            'Choose Prepare Build Tools to install GCC, then Build / Resume. '
            'If you set GCC_BIN yourself, point it at a working Homebrew gcc-N compiler.')
    # Compile against the selected SDK while retaining the supported runtime
    # floor. Otherwise Clang inherits the builder's macOS version on upgrades.
    environment.update(SDKROOT=sdk, GCC_BIN=gcc, MACOSX_DEPLOYMENT_TARGET='15.0')
    headers = '#include <sys/types.h>\n#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n'
    c = headers + '''#ifdef __clang__
#error Melee requires GCC scalar_storage_order support, not Clang
#endif
struct __attribute__((scalar_storage_order("big-endian"))) MeleeValue { unsigned value; };
int main(void) { struct MeleeValue value = { 1 }; return value.value != 1; }
'''
    flags = ['-arch', 'arm64', '-mmacosx-version-min=15.0', '-isysroot', sdk, '-fsyntax-only', '-']
    _probe([gcc, '-x', 'c', '-Werror=attributes', *flags], environment, c)
    _probe(['/usr/bin/clang++', '-x', 'c++', '-std=c++20', *flags], environment,
           headers + '#include <vector>\nint main() { std::vector<int> values{1}; return values[0] != 1; }\n')
    return environment, dict(ok=True, sdk=sdk, gcc=gcc, developer=developer,minimum_macos='15.0',
                             c_headers_checked=True, cxx_headers_checked=True)


def mac_toolchain_report():
    try:
        _, report = mac_build_environment()
        return report
    except MacToolchainError as error:
        return dict(ok=False, **error.diagnostic)


if __name__ == '__main__':
    report = mac_toolchain_report()
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['ok'] else 2)
