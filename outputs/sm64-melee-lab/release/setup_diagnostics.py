"""Actionable setup failures shared by the CLI and desktop installers."""
import errno
import re

RESUME = 'Then choose Build / Resume in the same build folder. Verified downloads and completed build outputs are kept.'


def diagnose(error, *, stage=None, version='development'):
    detail = '\n'.join(str(value) for value in (error, getattr(error, 'output', ''), getattr(error, 'stderr', '')) if value)[-16000:]
    lower = detail.lower()
    def result(code, title, message, recovery):
        if stage:
            message += '\nStopped at step {number}/{total}: {label}.'.format(**stage)
        return dict(code=code, title=title, message=message, recovery=recovery,
                    detail=detail, version=version, stage=stage)
    if hasattr(error, 'diagnostic'):
        value=dict(error.diagnostic, version=version, stage=stage)
        if stage:value['message'] += '\nStopped at step {number}/{total}: {label}.'.format(**stage)
        return value
    space = re.search(r'at least ([\d.]+) gib.*?([\d.]+) gib is available', lower)
    if space:
        required, available = map(float, space.groups())
        return result('disk-space', 'Not enough free disk space',
            f'The build drive has {available:.1f} GiB free; setup needs at least {required:.1f} GiB.',
            f'Free at least {max(0, required-available):.1f} GiB more, or use Build Folder to select a drive with enough space. '
            '20 GiB free is recommended. ' + RESUME)
    if getattr(error, 'errno', None)==errno.ENOSPC or any(s in lower for s in ('no space left on device', 'not enough space on the disk', 'disk full')):
        return result('disk-full', 'The build drive ran out of space',
            'Setup could not finish writing a download or build file.',
            'Free space on the build drive; aim for at least 20 GiB free. ' + RESUME)
    if ('level_rules.mk' in lower and ('no such file' in lower or 'cannot open' in lower)) or 'gnu make is required' in lower or ('gmake' in lower and 'prerequisites' in lower):
        return result('gnu-make', 'Install GNU Make',
            'The SM64 build needs GNU Make. Apple\'s bundled Make 3.81 skips required preparation steps.',
            'Choose Prepare Build Tools in the latest Mac installer. Manual repair: run brew install make in Terminal. ' + RESUME)
    if 'sm64: use the original' in lower:
        return result('sm64-image', 'Choose the supported Super Mario 64 ROM',
            'The selected file did not match the original US big-endian .z64 ROM (8 MiB).',
            'Choose your own unmodified US SM64 .z64 file. Other regions, revisions, byte orders and patched ROMs are unsupported. The original file is unchanged.')
    if 'melee: use an unmodified' in lower or 'melee disc header' in lower:
        return result('melee-image', 'Choose the supported Melee disc image',
            'The selected file did not match Super Smash Bros. Melee USA Rev 2 (GALE01, revision 2).',
            'Choose your own unmodified USA Rev 2 ISO or GCM. Extract ZIP/7z files first; RVZ, NKit and patched images are unsupported. The original file is unchanged.')
    if 'without spaces' in lower:
        return result('build-path', 'Choose a build folder without spaces',
            'The source build tools cannot use this build-folder path.',
            'Use Build Folder to choose a writable path such as ~/SuperMelee64 on Mac or C:\\SuperMelee64 on Windows. Your game filenames may contain spaces.')
    if 'apple silicon' in lower and 'macos 15' in lower:
        return result('mac-platform', 'This Mac is not supported by this build',
            'The current Mac installer requires an Apple Silicon Mac running macOS 15 or newer.',
            'Update macOS if your Mac supports it. Intel Macs require a separate port; installing more build tools will not fix that limitation.')
    if 'already running' in lower or 'another setup process' in lower:
        return result('setup-running', 'Another setup is already running',
            'This build folder is being used by another installer process.',
            'Return to that installer and let it finish, or use its Cancel button and wait for it to stop. Then retry. Do not delete the active lock file.')
    if getattr(error, 'errno', None) in (errno.EACCES, errno.EPERM) or 'not writable' in lower or 'permission denied' in lower:
        return result('folder-access', 'Setup cannot write to the build folder',
            'The current account cannot create or update a required local file.',
            'Choose a build folder owned by your account. Check the drive is writable and has space. Do not run setup as administrator or root. Your original game files are unchanged.')
    if any(s in lower for s in ('certificate_verify_failed', 'certificate verify failed', 'ssl certificate problem')):
        return result('download-certificate', 'A download failed its certificate check',
            'Setup could not verify the download server\'s HTTPS certificate.',
            'Check the computer\'s date and time and install OS updates. If your network uses a proxy, check it with its administrator. Keep certificate verification enabled. ' + RESUME)
    if any(s in lower for s in ('connection timed out', 'connection reset', 'could not resolve host', 'temporary failure in name resolution', 'urlopen error', 'http error 429', 'http error 5', 'failed to connect', 'curl: (')):
        return result('download-network', 'A required download could not finish',
            'The download server or network connection was unavailable.',
            'Check your connection and retry. If the server is temporarily unavailable, wait a few minutes. ' + RESUME)
    if 'preserving' in lower or 'different revision' in lower or 'unknown local' in lower:
        return result('existing-files', 'Existing files need attention',
            'Setup found files that differ from its expected inputs or pinned source and preserved them.',
            'Keep the existing folder if it contains your changes. Choose a new empty build folder for a clean install, or review the conflict in the setup log before retrying.')
    if 'checksum' in lower or 'hash mismatch' in lower or ('bootstrap' in lower and 'mismatch' in lower):
        return result('download-integrity', 'A downloaded file failed verification',
            'A source package or dependency did not match its expected checksum. Setup did not use it.',
            'Download and extract the latest complete source kit from the project release page. If it happens again, send the error code and setup log; do not disable the checksum check.')
    if 'prerequisites' in lower or isinstance(error, FileNotFoundError):
        return result('missing-tools', 'Required build tools are missing',
            str(error)[:1400], 'Choose Prepare Build Tools in the latest installer, finish any system setup or restart prompts, then retry Build / Resume.')
    if 'source kit' in lower or 'source package' in lower:
        return result('incomplete-kit', 'The extracted installer kit is incomplete',
            'Setup could not find a required project source file.',
            'Extract the entire latest release ZIP, then open the installer from that extracted folder. Keep the existing build folder so the new helper can resume it.')
    lines=[line.strip() for line in detail.splitlines() if re.search(r'(?i)(fatal error:|error:|undefined symbols|undefined reference|ninja:|make.*\*\*\*)', line)]
    summary=next((line for line in lines if 'fatal error:' in line.lower() or 'error:' in line.lower()), lines[-1] if lines else str(error))[:1200]
    return result('build-failed' if stage else 'setup-failed', 'The game build stopped' if stage else 'Setup could not continue',
        summary or 'The setup process exited without an error message.',
        'Open Setup Log for the complete output. Share the installer version, error code and log with the project maintainer. ' + RESUME)


def format_diagnostic(value):
    return '\n'.join((value['title'], value['message'], '', value['recovery'], '',
                      'Error code: '+value['code'], 'Installer version: '+value.get('version','development')))
