# Security

This is an experimental native game mod. Network compatibility is restricted to
matching Super Melee 64 native protocol builds; this is not a promise of secure
operation with arbitrary clients. Use trusted players while networking receives
further review and testing.

The maintained version is the latest source commit for the current native
protocol. Older experimental builds may lack fixes and are not maintained as
separate security branches.

## Reporting a vulnerability

Use the repository's **Security → Advisories → Report a vulnerability** option
when it is available. Describe the affected source commit/native version,
platform, host/client role, impact and a minimal reproduction. Do not publish
an exploit or private credentials in an ordinary issue. If private reporting is
unavailable, open a minimal issue asking a maintainer to enable a private report;
leave exploit details and sensitive data out of that issue.

Do not attach game images, extracted assets, private saves, full build folders,
credentials or another person's network details. A source-only reproduction or
small sanitized log is preferable. There is no guaranteed response time.

## Downloads and local builds

Use source kits from the repository's release page and compare their published
SHA-256 checksums. Setup verifies the supported original game inputs locally;
game files are not uploaded. Do not redistribute the generated game app or its
asset/build folders. See [LICENSING.md](LICENSING.md) for distribution scope.

Pull-request CI runs with read-only repository permissions and has no game
images or private build credentials. Never add those inputs to CI secrets,
caches, logs or uploaded artifacts.
