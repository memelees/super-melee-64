#!/usr/bin/env python3
"""Audit Git-selected source files/history without reading ignored game folders.

Findings identify a rule and location, never the matched credential or private
term. Optional private terms are supplied from a local file outside an export;
they are not embedded in public source, receipts or CI configuration.
"""
from pathlib import Path, PurePosixPath
import argparse
import base64
import collections
import hashlib
import io
import json
import re
import struct
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MANIFEST = 'PUBLIC-SOURCE-MANIFEST.json'
BOOTSTRAP = 'outputs/sm64-melee-lab/release/SmashRoyale-bootstrap.zip'
PNG = 'outputs/sm64-melee-lab/branding/Super Melee 64 Rendered.png'
MAX_BYTES = 16 * 1024 * 1024
TEXT_SUFFIXES = {'.md', '.txt', '.py', '.c', '.cpp', '.h', '.inc', '.m', '.patch',
                 '.json', '.lua', '.html', '.svg', '.swift', '.ps1', '.cmd',
                 '.command', '.vbs', '.yml', '.yaml'}
FORBIDDEN = {'.iso', '.gcm', '.z64', '.n64', '.v64', '.rvz', '.nkit', '.dat',
             '.usd', '.dol', '.smm', '.dylib', '.dll', '.exe', '.o', '.obj',
             '.a', '.lib', '.mp4', '.mov', '.sav', '.eep', '.bin', '.hps',
             '.bti', '.tpl', '.wav', '.mp3', '.ogg', '.flac'}
PRIVATE_PATH = re.compile(r'/(?:Users|home)/[A-Za-z0-9_. -]+/')
SECRET_RULES = {
    'github_token': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{30,})\b'),
    'aws_access_key': re.compile(r'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    'slack_token': re.compile(r'\bxox[baprs]-[A-Za-z0-9-]{20,}\b'),
    'private_key': re.compile(r'-----BEGIN (?:RSA |EC |DSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----'),
    'url_credentials': re.compile(r'https?://[^\s/:@]+:[^\s/@]{12,}@'),
}
PNG_METADATA = {b'tEXt', b'zTXt', b'iTXt', b'eXIf', b'tIME'}


def finding(kind, path, rule, line=None):
    item = {'kind': kind, 'path': path, 'rule': rule}
    if line is not None:
        item['line'] = line
    return item


def load_terms(path):
    if not path:
        return []
    data = json.loads(Path(path).read_text())
    terms = data.get('deny_terms', []) if isinstance(data, dict) else data
    if not isinstance(terms, list) or any(not isinstance(t, str) or not t for t in terms):
        raise ValueError('Private audit terms must be a list of nonempty strings')
    return terms


def redact_location(path, terms):
    for term in sorted(terms, key=len, reverse=True):
        path = re.sub(re.escape(term), '<private>', path, flags=re.IGNORECASE)
    return PRIVATE_PATH.sub('/<local-user>/', path)


def text_findings(data, path, terms):
    path = redact_location(path, terms)
    text = data.decode('utf-8', errors='replace')
    found = []
    for rule, pattern in SECRET_RULES.items():
        for match in pattern.finditer(text):
            found.append(finding('credential', path, rule, text.count('\n', 0, match.start()) + 1))
    for match in PRIVATE_PATH.finditer(text):
        found.append(finding('privacy', path, 'personal_absolute_path', text.count('\n', 0, match.start()) + 1))
    for index, term in enumerate(terms):
        for match in re.finditer(re.escape(term), text, re.IGNORECASE):
            found.append(finding('privacy', path, 'private_term_' + str(index + 1), text.count('\n', 0, match.start()) + 1))
    return found


def png_chunks(data):
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('Invalid original logo PNG')
    offset = 8
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError('Truncated PNG chunk')
        size = struct.unpack_from('>I', data, offset)[0]
        end = offset + size + 12
        if end > len(data):
            raise ValueError('Truncated PNG payload')
        yield data[offset + 4:offset + 8], data[offset + 8:offset + 8 + size], data[offset:end]
        offset = end


def bootstrap_findings(data, path, terms):
    found, facts = [], {}
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise ValueError('Duplicate bootstrap members')
            manifest = json.loads(archive.read('smash-royale-bootstrap.json'))
            files = manifest['files']
            if manifest.get('source_only') is not True or manifest.get('game_assets_included') is not False:
                raise ValueError('Bootstrap is not declared source-only')
            if set(names) != {'smash-royale-bootstrap.json', *files}:
                raise ValueError('Bootstrap manifest/member mismatch')
            patches, game_targets, context_lines = 0, set(), 0
            for name, digest in files.items():
                member = PurePosixPath(name)
                allowed = ((len(member.parts) == 3 and member.parts[:2] == ('tools', 'melee3d') and member.suffix == '.py') or
                           (len(member.parts) >= 3 and member.parts[:2] == ('native', 'mac') and member.suffix in {'.c', '.cpp', '.h', '.inc', '.json', '.patch', '.manifest', '.md'}) or
                           name in {'BOOTSTRAP-NOTICE.txt', 'COPYING-port.txt', 'UPSTREAM-LICENSING.txt', 'config/GALE01/symbols.txt'})
                entry = archive.getinfo(name)
                if not allowed or member.is_absolute() or '..' in member.parts or '\\' in name or (entry.external_attr >> 16) & 0o170000 == 0o120000 or entry.file_size > 8 * 1024 * 1024:
                    raise ValueError('Invalid source bootstrap member')
                payload = archive.read(name)
                if b'\x00' in payload or hashlib.sha256(payload).hexdigest() != digest:
                    raise ValueError('Changed/binary bootstrap source')
                payload.decode('utf-8')
                found.extend(text_findings(name.encode(), path + '!member_name', terms))
                found.extend(text_findings(payload, path + '!' + name, terms))
                if member.suffix == '.patch':
                    patches += 1
                    for line in payload.decode().splitlines():
                        if line.startswith('diff --git a/'):
                            target = line[len('diff --git a/'):].split(' b/', 1)[0]
                            if target.startswith(('src/melee/', 'src/sysdolphin/')):
                                game_targets.add(target)
                        if line.startswith(' '):
                            context_lines += 1
            found.extend(text_findings(archive.read('smash-royale-bootstrap.json'), path + '!manifest', terms))
            facts = {'files': len(files), 'all_payload_hashes_verified': True,
                     'all_payloads_utf8_text': True, 'patches': patches,
                     'upstream_game_files_touched_by_patches': len(game_targets),
                     'patch_context_lines': context_lines,
                     'distribution_permissions_not_certified': True}
    except (ValueError, KeyError, UnicodeError, zipfile.BadZipFile, json.JSONDecodeError):
        found.append(finding('asset', path, 'invalid_source_bootstrap'))
    return found, facts


def file_findings(data, path, terms):
    suffix = PurePosixPath(path).suffix.lower()
    if suffix in FORBIDDEN or '/.env' in '/' + path or PurePosixPath(path).name.startswith('.env'):
        return [finding('asset', path, 'forbidden_game_binary_or_environment_file')], {}
    if len(data) > MAX_BYTES:
        return [finding('asset', path, 'oversized_source_file')], {}
    if path == BOOTSTRAP:
        return bootstrap_findings(data, path, terms)
    if path == PNG:
        try:
            chunks = list(png_chunks(data))
        except ValueError:
            return [finding('asset', path, 'invalid_original_logo')], {}
        found = [finding('privacy', path, 'image_metadata_' + kind.decode()) for kind, _, _ in chunks if kind in PNG_METADATA]
        # Other ancillary metadata may contain text; image pixels are excluded.
        for kind, payload, _ in chunks:
            if kind not in {b'IHDR', b'IDAT', b'IEND'}:
                found.extend(text_findings(payload, path + '!' + kind.decode(errors='replace'), terms))
                if b'\x00' in payload:
                    found.extend(text_findings(payload.replace(b'\x00', b''), path + '!metadata_text', terms))
        return found, {}
    if suffix not in TEXT_SUFFIXES and path not in {'.gitignore', 'LICENSE'}:
        return [finding('asset', path, 'unexpected_non_source_file')], {}
    if b'\x00' in data or data.startswith((b'MZ', b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\x80\x37\x12\x40')):
        return [finding('asset', path, 'binary_disguised_as_source')], {}
    found = text_findings(data, path, terms)
    if suffix == '.svg' and re.search(rb'<(?:[A-Za-z0-9_]+:)?metadata\b', data):
        found.append(finding('privacy', path, 'svg_metadata'))
    if suffix == '.svg':
        for number, match in enumerate(re.finditer(rb'data:image/png;base64,([A-Za-z0-9+/=\r\n]+)', data)):
            try:
                image = base64.b64decode(match[1])
                issues, _ = file_findings(image, PNG, terms)
                for issue in issues:
                    issue['path'] = path + '!embedded_image_' + str(number + 1)
                found.extend(issues)
            except (ValueError, base64.binascii.Error):
                found.append(finding('asset', path, 'invalid_embedded_logo_image'))
    return found, {}


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def audit(root, terms=(), history=True):
    root = Path(root).resolve()
    is_git = (root / '.git').exists()
    if is_git:
        tracked = git(root, 'ls-files', '-z').decode().split('\0')[:-1]
        pending = git(root, 'ls-files', '--others', '--exclude-standard', '-z').decode().split('\0')[:-1]
        paths = sorted(set(tracked + pending))
    elif (root / MANIFEST).is_file():
        manifest = json.loads((root / MANIFEST).read_text())
        paths = sorted({MANIFEST, *manifest['files']})
        tracked, pending = [], paths
        for path, digest in manifest['files'].items():
            target = root / path
            if PurePosixPath(path).is_absolute() or '..' in PurePosixPath(path).parts or target.is_symlink() or hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                raise ValueError('Export file differs from its source manifest')
        actual = {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}
        if actual != set(paths):
            raise ValueError('Unexpected files in the source export')
    else:
        raise ValueError('Audit an explicit Git repository or manifested source export')
    found, bootstrap = [], {}
    for path in paths:
        found.extend(text_findings(path.encode(), path, terms))
        target = root / path
        if target.is_symlink():
            found.append(finding('asset', path, 'symlink_not_audited'));continue
        if not target.is_file():
            found.append(finding('asset', path, 'tracked_file_missing'));continue
        issues, facts = file_findings(target.read_bytes(), path, terms)
        found.extend(issues)
        if facts:
            bootstrap = facts
    blobs_checked, commits = 0, 0
    if history and is_git:
        commits = int(git(root, 'rev-list', '--count', '--all'))
        actors = git(root, 'log', '--all', '--format=%an%n%ae%n%cn%n%ce%n%B%x00').split(b'\x00')
        for index, metadata in enumerate(actors):
            found.extend(text_findings(metadata, 'history:commit-metadata-' + str(index + 1), terms))
        found.extend(text_findings(git(root, 'config', '--local', '--list'), 'history:local-repository-metadata', terms))
        objects = git(root, 'rev-list', '--objects', '--all').decode().splitlines()
        # Scan every reachable blob, including deleted source and old archives.
        for row in objects:
            oid, _, path = row.partition(' ')
            if not path or git(root, 'cat-file', '-t', oid).strip() != b'blob':
                continue
            size = int(git(root, 'cat-file', '-s', oid))
            found.extend(text_findings(path.encode(), 'history:' + path, terms))
            if size > MAX_BYTES:
                found.append(finding('asset', 'history:' + path, 'oversized_historical_blob'));continue
            issues, _ = file_findings(git(root, 'cat-file', 'blob', oid), path, terms)
            for issue in issues:
                issue['path'] = 'history:' + issue['path']
            found.extend(issues);blobs_checked += 1
    for item in found:
        item['path'] = redact_location(item['path'], terms)
    counts = dict(collections.Counter(item['kind'] for item in found))
    return {'passed': not found, 'current_files_checked': len(paths),
            'tracked_files': len(tracked), 'proposed_nonignored_files': len(pending),
            'history_checked': bool(history and is_git), 'history_commits': commits,
            'commit_actor_and_local_git_metadata_checked': bool(history and is_git),
            'historical_blobs_checked': blobs_checked,
            'private_terms_configured': len(terms), 'finding_counts': counts,
            'findings': found, 'bootstrap': bootstrap,
            'scope': 'Git-selected source candidates and reachable history, or an exact source-export manifest. Ignored game/build folders are never scanned. Findings omit matched values and private commit metadata.',
            'legal_clearance_claimed': False}


def self_test():
    global git
    token = 'gh' + 'p_' + 'A' * 36
    report = text_findings(('token=' + token).encode(), 'sample.py', [])
    assert report and report[0]['rule'] == 'github_token' and token not in json.dumps(report)
    samples = [('github_token', 'github_' + 'pat_' + 'A' * 40),
               ('aws_access_key', 'AK' + 'IA' + 'A' * 16),
               ('slack_token', 'xox' + 'b-' + 'A' * 25),
               ('private_key', '-' * 5 + 'BEGIN ' + 'PRIVATE KEY' + '-' * 5),
               ('url_credentials', 'https://' + 'account:' + 'A' * 20 + '@example.invalid')]
    for expected, value in samples:
        hits = text_findings(value.encode(), 'sample.txt', [])
        assert any(hit['rule'] == expected for hit in hits)
        assert value not in json.dumps(hits)
    assert text_findings(b'identity: Example Private Alias', 'sample.txt', ['example private alias'])
    assert text_findings(('/' + 'Users' + '/someone/private/').encode(), 'sample.txt', [])
    assert file_findings(b'source', 'accidental.iso', [])[0]
    assert file_findings(b'\x7fELF\x00', 'pretend.c', [])[0]
    assert not file_findings(b'int function(void){return 1;}\n', 'example.c', [])[0]
    assert not text_findings(b'checksum=9bef1128717f958171a4afac3ed78ee2bb4e86ce', 'source.txt', [])
    assert bootstrap_findings(b'not a ZIP', BOOTSTRAP, [])[0]
    def bundle(name, payload, correct_hash=True):
        data = io.BytesIO()
        manifest = {'source_only':True, 'game_assets_included':False,
                    'files':{name:hashlib.sha256(payload).hexdigest() if correct_hash else '0' * 64}}
        with zipfile.ZipFile(data, 'w') as archive:
            archive.writestr(name, payload)
            archive.writestr('smash-royale-bootstrap.json', json.dumps(manifest))
        return data.getvalue()
    assert not bootstrap_findings(bundle('tools/melee3d/example.py', b'print(1)'), BOOTSTRAP, [])[0]
    assert bootstrap_findings(bundle('tools/melee3d/example.py', token.encode()), BOOTSTRAP, [])[0][0]['kind'] == 'credential'
    assert bootstrap_findings(bundle('tools/melee3d/example.py', b'print(1)', False), BOOTSTRAP, [])[0]
    assert bootstrap_findings(bundle('../outside.py', b'print(1)'), BOOTSTRAP, [])[0]
    assert bootstrap_findings(bundle('native/mac/game.dat', b'extracted'), BOOTSTRAP, [])[0]
    # Drive the actual history traversal with a synthetic deleted blob and
    # actor record. No Git repository is initialized or committed for this test.
    original_git = git
    deleted = ('key=' + token).encode()
    def history_fixture(root, *args):
        if args == ('ls-files', '-z'):return b'README.md\0'
        if args == ('ls-files', '--others', '--exclude-standard', '-z'):return b''
        if args == ('rev-list', '--count', '--all'):return b'2\n'
        if args == ('log', '--all', '--format=%an%n%ae%n%cn%n%ce%n%B%x00'):return b'Example Private Alias\0'
        if args == ('config', '--local', '--list'):return b'remote.origin.url=https://example.invalid/source\n'
        if args == ('rev-list', '--objects', '--all'):return b'object deleted-source.py\n'
        if args == ('cat-file', '-t', 'object'):return b'blob\n'
        if args == ('cat-file', '-s', 'object'):return str(len(deleted)).encode()
        if args == ('cat-file', 'blob', 'object'):return deleted
        raise AssertionError('Unexpected history fixture operation')
    with tempfile.TemporaryDirectory(prefix='source-audit-test-') as temporary:
        root = Path(temporary);(root / '.git').mkdir();(root / 'README.md').write_text('ordinary source')
        git = history_fixture
        try:
            result = audit(root, ['Example Private Alias'])
        finally:
            git = original_git
        assert not result['passed'] and result['historical_blobs_checked'] == 1
        assert result['finding_counts'] == {'privacy':1, 'credential':1}
        assert token not in json.dumps(result) and 'Example Private Alias' not in json.dumps(result)
    print('PASS: credential/alias redaction, personal paths, forbidden assets, disguised binaries, ordinary hashes, nested source ZIP hashes/path/asset checks, deleted historical secrets and private commit actors')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--privacy-terms', type=Path)
    parser.add_argument('--no-history', action='store_true', help='For a new uninitialized source export; normal CI always checks reachable history')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test();return 0
    result = audit(args.root, load_terms(args.privacy_terms), not args.no_history)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
