#!/usr/bin/env python3
"""Verify the pinned source adapter and report its distribution boundary."""
from pathlib import Path
import argparse
import json
from build_bootstrap import ARCHIVE, verify
from verify_repository import bootstrap_findings, load_terms


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=ARCHIVE)
    parser.add_argument('--privacy-terms', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    # The builder checks its exact supported revision, members and hashes.
    verify(args.archive)
    findings, facts = bootstrap_findings(args.archive.read_bytes(), 'source-bootstrap.zip', load_terms(args.privacy_terms))
    result = {'passed': not findings, 'pinned_builder_contract_verified': True,
              **facts, 'findings': findings, 'legal_clearance_claimed': False}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
