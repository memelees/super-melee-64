# Licensing and distribution scope

Copyright (C) 2026 Super Melee 64 contributors.

The original Super Melee 64 integration code, build and installation tools,
tests, documentation and original branding authored for this project are
licensed under **GPL-3.0-or-later**, unless a file carries a different notice.
The full GNU General Public License version 3 is in [LICENSE](LICENSE).
You may use version 3 or, at your option, any later version.

## Your freedom to change and reuse the project

You are welcome to fork this project, edit its original code, add characters or
features, experiment privately, and adapt the integration to other games.
You do not need separate permission from this project, and you do not have to
send your changes back here. Commercial use of the covered original work is
also permitted under the GPL's terms.

If you distribute covered code or a modified version, preserve the applicable
copyright and licence notices, identify your changes, and meet the GPL's
corresponding-source and licensing requirements. Recipients must retain the
freedom to study, modify and redistribute the covered work. Private changes do
not have to be published. This is free software with copyleft terms; it is not
a waiver of all conditions. The full [LICENSE](LICENSE) is authoritative; see
the GNU project's [GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html) for
explanations of private changes and distributing modified versions.

Start with [how the integration works](docs/INTEGRATION.md) and the
[guide to adapting it to another game](docs/PORTING.md). Reusing the technique
does not grant rights over a game's code or assets: the scope below still
applies to each dependency and to any new host or donor game.

## What the licence covers

This grant covers only the contributors' original work. It does not relicense
third-party code, patch context, game assets, character designs, trademarks or
other material belonging to their respective owners. Where a file combines
original changes with third-party material, this grant covers the original
changes; the original material retains its existing notices and permissions.

| Material | Applicable scope |
| --- | --- |
| Original Super Melee 64 integration and release tools | GPL-3.0-or-later, subject to individual source notices |
| Bundled Smash Royale adapter source and changes | Preserve individual notices, bootstrap manifest and included GPL/upstream notices |
| Upstream native port code | Its upstream licenses; see the preserved notices below |
| Upstream decompiled SM64/Melee game code and patch context | No license is granted here; review the upstream notices and provenance |
| User-supplied ROMs/discs and extracted models, textures, audio or fonts | Not included or licensed by this project |
| Third-party libraries and official mod patches fetched during setup | Their own upstream notices and permissions |

The native Melee upstream explicitly distinguishes its GPL port code from
decompiled game code for which it offers no redistribution permission. Read
[UPSTREAM-LICENSING.txt](outputs/sm64-melee-lab/release/UPSTREAM-LICENSING.txt)
and [THIRD-PARTY.txt](outputs/sm64-melee-lab/release/THIRD-PARTY.txt).
The bundled source adapter also contains patches against that upstream code.
Calling a package "source-only" does not resolve those underlying permissions.

Distributable project artifacts are the reviewed source repository and source
build kits. Do not upload locally built game apps, engine binaries, game images,
extracted art/audio, saved games, recordings or local dependency/build trees as
project release assets. The small installer helper is built from its published
source and contains only the reviewed source kit and original branding.

The automated content checks detect accidental assets, binaries and common
credential formats. They do not establish copyright ownership, grant missing
permissions or constitute legal clearance. The bootstrap composition and its
remaining review boundary are documented in
[Bootstrap provenance](docs/BOOTSTRAP-PROVENANCE.md).
