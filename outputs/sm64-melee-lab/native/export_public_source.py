#!/usr/bin/env python3
"""Create an audited source-only directory with no Git history or account actor.

This does not initialize Git, commit, publish, transfer or contact GitHub. The
optional local policy contains private aliases/replacements and is never copied
into the export or included in its manifest or audit findings.
"""
from pathlib import Path
import argparse
import base64
import contextlib
import hashlib
import io
import json
import re
import struct
import tempfile
import xml.etree.ElementTree as ET
import zlib
from verify_repository import ROOT, MANIFEST, PNG, audit, git, load_terms, png_chunks

LEGACY = {'verify_independence.py', 'export_link.py', 'build_worker.py',
          'play_adventure.py', 'play_castle.py'}
# Keep pixel bytes and standard color/alpha information; discard author,
# editor, timestamp and other auxiliary records from the original logo.
PNG_KEEP = {b'IHDR', b'PLTE', b'IDAT', b'IEND', b'tRNS', b'cHRM', b'gAMA',
            b'iCCP', b'sBIT', b'sRGB', b'pHYs', b'bKGD'}


def clean_png(data):
    chunks = list(png_chunks(data))
    cleaned = b'\x89PNG\r\n\x1a\n' + b''.join(raw for kind, _, raw in chunks if kind in PNG_KEEP)
    # Metadata removal must never rewrite or transcode a pixel payload.
    assert [payload for kind, payload, _ in png_chunks(cleaned) if kind == b'IDAT'] == [payload for kind, payload, _ in chunks if kind == b'IDAT']
    return cleaned


def clean_svg(data):
    tree = ET.fromstring(data)
    for parent in list(tree.iter()):
        for child in list(parent):
            if child.tag.rsplit('}', 1)[-1] in {'metadata', 'namedview'}:
                parent.remove(child)
        for name in list(parent.attrib):
            if 'inkscape' in name or 'sodipodi' in name or name.rsplit('}', 1)[-1] in {'docname', 'absref', 'export-filename'}:
                del parent.attrib[name]
    result = ET.tostring(tree, encoding='utf-8', xml_declaration=True)
    def embedded(match):
        return b'data:image/png;base64,' + base64.b64encode(clean_png(base64.b64decode(match[1])))
    return re.sub(rb'data:image/png;base64,([A-Za-z0-9+/=\r\n]+)', embedded, result)


def source_paths(root):
    tracked = git(root, 'ls-files', '-z').decode().split('\0')[:-1]
    proposed = git(root, 'ls-files', '--others', '--exclude-standard', '-z').decode().split('\0')[:-1]
    return sorted(set(tracked + proposed))


def manifest_for(root, changes, evidence):
    return {'format': 1, 'source_only': True, 'git_history_included': False,
            'private_source_commit_metadata_included': False,
            'historical_evidence_omitted_by_default': True,
            'selected_historical_evidence_files': len(evidence),
            'files_with_sanitization_changes': changes,
            'files': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in sorted(root.rglob('*')) if path.is_file() and path.name != MANIFEST}}


def export(source, destination, policy_path=None, audit_output=None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if destination == source or destination in source.parents or destination.exists():
        raise ValueError('Choose a new source-export directory; existing files are preserved')
    policy = json.loads(Path(policy_path).read_text()) if policy_path else {}
    terms = load_terms(policy_path) if policy_path else []
    replacements = policy.get('replacements', [])
    if not isinstance(replacements, list) or any(not isinstance(pair, list) or len(pair) != 2 or any(not isinstance(value, str) for value in pair) or not pair[0] for pair in replacements):
        raise ValueError('Policy replacements must be [original, replacement] string pairs')
    omit = set(policy.get('omit_paths', []))
    evidence = set(policy.get('include_evidence', []))
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.source-export-', dir=destination.parent) as temporary:
        staged = Path(temporary) / 'source';staged.mkdir()
        changed = 0;selected_evidence = set()
        for name in source_paths(source):
            relative = Path(name)
            if name in omit or name == MANIFEST or relative.name in LEGACY:
                continue
            if name.startswith('outputs/sm64-melee-lab/verification/') and name not in evidence:
                continue
            original = source / name
            if original.is_symlink() or not original.is_file():
                raise ValueError('Source export refuses missing files and symlinks')
            data = original.read_bytes()
            if name == PNG:
                prepared = clean_png(data)
            elif relative.suffix.lower() == '.svg':
                prepared = clean_svg(data)
            elif relative.suffix.lower() == '.zip':
                # The builder owns bootstrap provenance and rehashing. Never
                # blindly rewrite ZIP members or invalidate their source hashes.
                prepared = data
            else:
                text = data.decode('utf-8')
                for before, after in sorted(replacements, key=lambda pair: len(pair[0]), reverse=True):
                    text = text.replace(before, after)
                if name in evidence:
                    # Evidence is deliberately selected, and its sanitization
                    # is disclosed. Numeric outcomes are not changed here.
                    value = json.loads(text)
                    def sanitize(item):
                        if isinstance(item, dict):return {key:sanitize(part) for key,part in item.items()}
                        if isinstance(item, list):return [sanitize(part) for part in item]
                        if isinstance(item, str):return re.sub(r'/(?:Users|home)/[^\n\r\"\']+', '<local build path>', item)
                        return item
                    value = sanitize(value)
                    if isinstance(value, dict):value['public_evidence_note'] = ('Selected test evidence; personal identifiers and local paths are sanitized. See the recorded test scope and platform limitations.' if relative.name == 'beta-acceptance.json' else 'Selected historical evidence; personal identifiers and local paths are sanitized. This is not a fresh clean-machine build receipt.')
                    text = json.dumps(value, indent=2) + '\n'
                prepared = text.encode()
            target = staged / name;target.parent.mkdir(parents=True, exist_ok=True);target.write_bytes(prepared)
            if name in evidence:
                selected_evidence.add(name)
            if original.stat().st_mode & 0o111:
                target.chmod(0o755)
            changed += prepared != data
        result = None
        # The generated evidence contains no source root, old commit IDs,
        # private terms or matched values. Two passes account for the audit
        # report itself in the final exact export manifest.
        for _ in range(2):
            (staged / MANIFEST).write_text(json.dumps(manifest_for(staged, changed, selected_evidence), indent=2) + '\n')
            result = audit(staged, terms, history=False)
            if not result['passed']:
                if audit_output:
                    Path(audit_output).parent.mkdir(parents=True, exist_ok=True)
                    Path(audit_output).write_text(json.dumps(result, indent=2) + '\n')
                print(json.dumps(result, indent=2))
                raise ValueError('Source export blocked by content/privacy audit; matched values are redacted')
            evidence_path = staged / 'outputs/sm64-melee-lab/verification/public-source-audit.json'
            evidence_path.parent.mkdir(parents=True, exist_ok=True)
            evidence_path.write_text(json.dumps(result, indent=2) + '\n')
        (staged / MANIFEST).write_text(json.dumps(manifest_for(staged, changed, selected_evidence), indent=2) + '\n')
        result = audit(staged, terms, history=False)
        if not result['passed']:
            raise ValueError('Final source manifest failed validation')
        if audit_output:
            Path(audit_output).parent.mkdir(parents=True, exist_ok=True)
            Path(audit_output).write_text(json.dumps(result, indent=2) + '\n')
        staged.rename(destination)
    return {'passed': True, 'git_history_copied': False, 'repository_initialized': False,
            'published': False, 'files': result['current_files_checked'],
            'files_with_sanitization_changes': changed, 'audit': result}


def self_test():
    global source_paths
    original_paths = source_paths
    with tempfile.TemporaryDirectory(prefix='public-source-test-') as temporary:
        temporary = Path(temporary);source = temporary / 'input';source.mkdir()
        names = ['README.md', '.gitignore', PNG,
                 'outputs/sm64-melee-lab/branding/Super Melee 64.svg',
                 'outputs/sm64-melee-lab/verification/old.json']
        (source / 'README.md').write_text('Example Private Alias\n')
        (source / '.gitignore').write_text('/work/\n')
        def chunk(kind, payload):
            return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
        pixels = zlib.compress(b'\0\xff\0\0\xff')
        image = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 1, 1, 8, 6, 0, 0, 0)) + chunk(b'tEXt', b'Author\0Example Private Alias') + chunk(b'IDAT', pixels) + chunk(b'IEND', b'')
        logo = source / PNG;logo.parent.mkdir(parents=True);logo.write_bytes(image)
        svg = source / names[3]
        svg.write_text('<svg xmlns="http://www.w3.org/2000/svg"><metadata>Example Private Alias</metadata><image href="data:image/png;base64,' + base64.b64encode(image).decode() + '" /></svg>')
        evidence = source / names[4];evidence.parent.mkdir();evidence.write_text('{"author":"Example Private Alias"}')
        (source / '.git').mkdir();(source / '.git/config').write_text('private account metadata')
        policy = temporary / 'local-policy.json'
        policy.write_text(json.dumps({'deny_terms':['Example Private Alias'], 'replacements':[['Example Private Alias','Project contributors']]}))
        source_paths = lambda root: names
        try:
            destination = temporary / 'result';result = export(source, destination, policy)
            assert result['passed'] and not (destination / '.git').exists()
            assert not (destination / names[4]).exists()
            assert (destination / 'README.md').read_text() == 'Project contributors\n'
            cleaned = (destination / PNG).read_bytes()
            assert b'tEXt' not in cleaned and b'IDAT' in cleaned and pixels in cleaned
            assert audit(destination, ['Example Private Alias'], history=False)['passed']
            try:export(source, destination, policy)
            except ValueError:pass
            else:raise AssertionError('An existing destination was overwritten')
            # A sensitive payload stops the export before creating its target.
            (source / 'README.md').write_text('gh' + 'p_' + 'A' * 36)
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    export(source, temporary / 'blocked', policy)
            except ValueError:pass
            else:raise AssertionError('A credential-like payload was exported')
            assert not (temporary / 'blocked').exists()
        finally:
            source_paths = original_paths
    print('PASS: no Git metadata, literal alias replacement, PNG/SVG metadata removal with identical pixel payloads, historical-evidence omission, exact manifest audit, destination preservation and credential-blocked atomic export')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path, nargs='?')
    parser.add_argument('--source', type=Path, default=ROOT)
    parser.add_argument('--policy', type=Path, help='Ignored local JSON with deny_terms, replacements, omit_paths and include_evidence')
    parser.add_argument('--audit-output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test();return
    if args.destination is None:
        parser.error('Provide a new export destination or --self-test')
    result = export(args.source, args.destination, args.policy, args.audit_output)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
