#!/usr/bin/env python3
"""Point the README buttons at the newest complete public installer release.

GitHub's /releases/latest excludes prereleases. Select published releases from
the release list instead, and use the actual uploaded asset URLs unchanged.
Only README.md and its existing source-manifest digest may be updated.
"""
import argparse
import datetime
import hashlib
import html
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import tempfile
import urllib.parse
import urllib.request


REPOSITORY = 'memelees/super-melee-64'
BASE = 'https://github.com/' + REPOSITORY
API = 'https://api.github.com/repos/' + REPOSITORY + '/releases'
START = '<!-- DOWNLOAD_BUTTONS_START -->'
END = '<!-- DOWNLOAD_BUTTONS_END -->'
MANIFEST = 'PUBLIC-SOURCE-MANIFEST.json'
MAX_JSON_BYTES = 4 * 1024 * 1024
MAX_PAGES = 10
TAG = re.compile(r'v([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?)\Z')
MAC_BADGE = 'https://img.shields.io/badge/Download_for_Mac-2563EB?style=for-the-badge'
WINDOWS_BADGE = 'https://img.shields.io/badge/Download_for_Windows-198754?style=for-the-badge'


def read_json(path):
    with Path(path).open('rb') as source:
        payload = source.read(MAX_JSON_BYTES + 1)
    if len(payload) > MAX_JSON_BYTES:
        raise ValueError('Release metadata exceeds the size limit')
    return json.loads(payload)


def public_releases():
    releases = []
    for page in range(1, MAX_PAGES + 1):
        url = API + '?per_page=100&page=' + str(page)
        request = urllib.request.Request(url, headers={
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'SuperMelee64-download-links',
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            if response.geturl() != url:
                raise ValueError('Unexpected release API redirect')
            payload = response.read(MAX_JSON_BYTES + 1)
        if len(payload) > MAX_JSON_BYTES:
            raise ValueError('Release API response exceeds the size limit')
        batch = json.loads(payload)
        if not isinstance(batch, list) or len(batch) > 100:
            raise ValueError('Unexpected release API response')
        releases.extend(batch)
        if len(batch) < 100:
            return releases
    raise ValueError('Release pagination limit reached; no links changed')


def positive_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def exact_url(value, expected):
    if not isinstance(value, str) or value != expected:
        raise ValueError('Unexpected public release or installer asset URL')
    parsed = urllib.parse.urlsplit(value)
    if (parsed.scheme, parsed.netloc) != ('https', 'github.com') or parsed.query or parsed.fragment:
        raise ValueError('Installer links must be exact public GitHub URLs')
    return value


def choose_release(releases):
    if not isinstance(releases, list) or len(releases) > 100 * MAX_PAGES:
        raise ValueError('Expected a bounded release list')
    candidates = []
    for release in releases:
        if not isinstance(release, dict):
            raise ValueError('Unexpected release record')
        if release.get('draft') is not False or not release.get('published_at'):
            continue
        if not isinstance(release.get('tag_name'), str):
            raise ValueError('Invalid release tag')
        match = TAG.fullmatch(release['tag_name'])
        if not match:
            continue  # Private source-import drafts and unrelated tags are not installers.
        version = match.group(1)
        tag = release['tag_name']
        if not isinstance(release.get('prerelease'), bool) or not positive_integer(release.get('id')):
            raise ValueError('Invalid published release identity')
        timestamp = release['published_at']
        if not isinstance(timestamp, str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z', timestamp):
            raise ValueError('Invalid release publication time')
        published = datetime.datetime.fromisoformat(timestamp.replace('Z', '+00:00'))
        exact_url(release.get('html_url'), BASE + '/releases/tag/' + tag)
        names = {
            'SuperMelee64-' + version + '-mac-source-kit.zip': 'mac',
            'SuperMelee64-' + version + '-windows-experimental-source-kit.zip': 'windows',
            'SuperMelee64-' + version + '-windows-source-kit.zip': 'windows',
        }
        assets = release.get('assets')
        if not isinstance(assets, list) or len(assets) > 100:
            raise ValueError('Unexpected release asset list')
        installers = {}
        for asset in assets:
            if not isinstance(asset, dict):
                raise ValueError('Unexpected release asset record')
            name = asset.get('name')
            if not isinstance(name, str):
                raise ValueError('Invalid release asset name')
            if name not in names or asset.get('state') != 'uploaded':
                continue
            platform = names[name]
            if platform in installers or not positive_integer(asset.get('size')):
                raise ValueError('Ambiguous or empty installer asset')
            url = exact_url(asset.get('browser_download_url'), BASE + '/releases/download/' + tag + '/' + name)
            installers[platform] = {'url': url, 'experimental': '-experimental-' in name}
        if set(installers) == {'mac', 'windows'}:
            candidates.append((published, release['id'], {
                'version': version, 'tag': tag, 'prerelease': release['prerelease'],
                'notes': release['html_url'], **installers,
            }))
    if not candidates:
        raise ValueError('No published release has both complete installer assets; no links changed')
    return max(candidates, key=lambda candidate: candidate[:2])[2]


def render_block(release):
    mac, windows, notes = (html.escape(release[key]['url'] if key != 'notes' else release[key], quote=True)
                           for key in ('mac', 'windows', 'notes'))
    version = html.escape(release['version'])
    status = 'beta' if release['prerelease'] else 'release'
    heading = 'Beta installers' if release['prerelease'] else 'Installers'
    windows_status = 'experimental beta' if release['windows']['experimental'] and release['prerelease'] else (
        'experimental' if release['windows']['experimental'] else status)
    return '\n'.join([
        START,
        '<p align="center">',
        '  <a href="' + mac + '"><img src="' + html.escape(MAC_BADGE, quote=True) + '" alt="Download for Mac" height="44"></a>',
        '  <a href="' + windows + '"><img src="' + html.escape(WINDOWS_BADGE, quote=True) + '" alt="Download for Windows" height="44"></a>',
        '</p>',
        '<p align="center">' + heading + ' · Mac: Apple Silicon, macOS 15+ · Windows x64: ' + windows_status + ' · Linux: not available</p>',
        '<p align="center"><a href="' + mac + '">Mac ZIP</a> · <a href="' + windows + '">Windows ZIP</a> · <a href="' + notes + '">Release notes — ' + version + ' (' + status + ')</a></p>',
        END,
    ])


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def checked_manifest(root, old_readme):
    path = root / MANIFEST
    if not path.exists():
        if path.is_symlink():
            raise ValueError('Source manifest must not be a symlink')
        return None
    if path.is_symlink() or not path.is_file():
        raise ValueError('Source manifest must be a regular file')
    manifest = read_json(path)
    files = manifest.get('files') if isinstance(manifest, dict) else None
    if not isinstance(files, dict) or not 1 <= len(files) <= 5000 or 'README.md' not in files:
        raise ValueError('Invalid source manifest file map')
    for name, expected in files.items():
        if not isinstance(name, str):
            raise ValueError('Invalid source manifest path')
        relative = PurePosixPath(name)
        if (relative.is_absolute() or relative.as_posix() != name
                or '..' in relative.parts or '\\' in name or name == MANIFEST
                or not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected)):
            raise ValueError('Invalid source manifest path or digest')
        target = root / name
        if any(part.is_symlink() for part in [target, *target.parents] if part != root and root in part.parents):
            raise ValueError('Source manifest entries must not traverse symlinks')
        if not target.is_file() or digest(target) != expected:
            raise ValueError('Source manifest is stale; verify and regenerate it before updating links')
    if files['README.md'] != hashlib.sha256(old_readme).hexdigest():
        raise ValueError('README does not match its source manifest')
    return manifest


def atomic_write(path, payload):
    descriptor, temporary = tempfile.mkstemp(prefix='.download-links-', dir=path.parent)
    try:
        os.fchmod(descriptor, stat.S_IMODE(path.stat().st_mode))
        with os.fdopen(descriptor, 'wb') as output:
            output.write(payload)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def update_readme(path, block, write=False):
    if path.name != 'README.md' or path.is_symlink() or not path.is_file():
        raise ValueError('Choose a regular README.md file')
    path = path.resolve()
    old = path.read_bytes()
    text = old.decode('utf-8')
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) >= text.index(END):
        raise ValueError('README must contain exactly one ordered download-button marker pair')
    updated = (text[:text.index(START)] + block + text[text.index(END) + len(END):]).encode('utf-8')
    manifest = checked_manifest(path.parent, old)
    if updated == old or not write:
        return updated != old
    manifest_path = path.parent / MANIFEST
    old_manifest = manifest_path.read_bytes() if manifest is not None else None
    if manifest is not None:
        manifest['files']['README.md'] = hashlib.sha256(updated).hexdigest()
    try:
        atomic_write(path, updated)
        if manifest is not None:
            atomic_write(manifest_path, (json.dumps(manifest, indent=2) + '\n').encode('utf-8'))
    except OSError:
        atomic_write(path, old)
        if old_manifest is not None:
            atomic_write(manifest_path, old_manifest)
        raise
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--readme', type=Path, default=Path('README.md'))
    parser.add_argument('--releases-json', type=Path, help='Use an offline public-release metadata fixture')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--dry-run', action='store_true', help='Print the current download block without changing files (default)')
    mode.add_argument('--write', action='store_true', help='Update only README.md and its existing source-manifest digest')
    args = parser.parse_args()
    try:
        release = choose_release(read_json(args.releases_json) if args.releases_json else public_releases())
        block = render_block(release)
        changed = update_readme(args.readme, block, args.write)
        if args.write:
            print(json.dumps({'release': release['tag'], 'changed': changed, 'manifest_updated': changed and (args.readme.parent / MANIFEST).is_file()}))
        else:
            print(block)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(1, 'Download links unchanged: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
