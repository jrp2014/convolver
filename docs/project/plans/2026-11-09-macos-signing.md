<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Sign and notarize the macOS packages

For jrp, who has to set up the Apple account, and for whoever then implements
the macOS part of REL-1: what Developer ID signing and notarization need, what
it costs, which steps only jrp can take, and what changes in the release
pipeline.

**In short:** signing needs a paid Apple Developer Program membership (99 USD a
year), two Developer ID certificates that only the account holder can create,
and an App Store Connect API key for Apple's notary service. jrp makes those on
his Mac and stores them as secrets in a GitHub environment that needs his
approval. A separate packaging step in the macOS candidate job, after the build
and tests, then signs every program with the hardened runtime and a secure
timestamp, has the bundles and the installer notarized, staples the tickets,
and seals the manifest and hashes afterwards, so the existing checks run on the
signed bytes. Users would then install the `.pkg` with no Gatekeeper warning,
and plug-ins copied from the ZIP would load without the `xattr` step. This page
changes no workflow, script or decision. Contents: [what users
get](#what-users-get), [what is signed](#what-is-signed), [account and
cost](#account-and-cost), [steps only jrp can take](#steps-only-jrp-can-take),
[pipeline changes](#pipeline-changes), [decisions for jrp](#decisions-for-jrp).

Sources, read on 9 November 2026: Apple's
[notarizing macOS software](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[customizing the notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow),
[creating Developer ID certificates](https://developer.apple.com/help/account/certificates/create-developer-id-certificates/),
[program enrollment](https://developer.apple.com/help/account/membership/program-enrollment/),
[fee waivers](https://developer.apple.com/help/account/membership/fee-waivers/),
[App Store Connect API keys](https://developer.apple.com/documentation/appstoreconnectapi/creating-api-keys-for-app-store-connect-api),
Apple DTS's forum post
[The Care and Feeding of Developer ID](https://developer.apple.com/forums/thread/732320),
and GitHub's
[installing an Apple certificate on macOS runners](https://docs.github.com/en/actions/use-cases-and-examples/deploying/installing-an-apple-certificate-on-macos-runners-for-xcode-development).

## What users get

Today the previews carry only an ad-hoc signature (Apple silicon will not load
unsigned code at all), so the [install guide](../../use/install.md#unsigned-previews)
has users approve the `.pkg` under System Settings > Privacy & Security >
Open Anyway (since macOS 15 the Control-click shortcut no longer bypasses
Gatekeeper), and run `xattr -dr com.apple.quarantine` on bundles copied from
the ZIP. Apple's notarization page says that from macOS 10.15 "apps can load
quarantined plug-ins ... only if the plug-in is notarized".

After signing and notarization:

- The `.pkg` opens with the normal installer, no Privacy & Security detour.
- Bundles copied from the ZIP load in a host without clearing the quarantine,
  because each bundle carries its stapled ticket.
- The command-line tools from the ZIP run without a warning when the Mac is
  online (a standalone program cannot carry a stapled ticket, so Gatekeeper
  looks the ticket up). Tools installed by the `.pkg` are not quarantined.
- **Unchanged:** a host built with the hardened runtime still refuses
  plug-ins from another developer unless the host has the
  `com.apple.security.cs.disable-library-validation` entitlement. Signing
  with Convolver's own Team ID does not change that, so the REAPER note in
  [host testing](../../develop/host-testing.md) still applies. Apple also says
  plug-ins "inherit the entitlements of the host process"; Convolver needs none.

## What is signed

Each macOS candidate (arm64 and x86_64 are separate packages) holds:

| Item | Signed with | Hardened runtime and timestamp | Notarized | Stapled |
| --- | --- | --- | --- | --- |
| `VST3/Convolver.vst3` | Developer ID Application | Yes | Yes | Yes |
| `Components/Convolver.component` | Developer ID Application | Yes | Yes | Yes |
| `bin/convolver`, `bin/convolver-preset`, `bin/convolver-equalizer-apo` | Developer ID Application | Yes | Yes (inside the submissions) | Not possible for a standalone program |
| The three component packages | Not signed separately | | Covered by the product package | |
| The product `.pkg` | Developer ID Installer (`productbuild --sign`) | Timestamp | Yes | Yes |
| The ZIP | Not signable | | Its signed contents are, in a separate ZIP made only for the submission | Not possible: staple the bundles inside, then zip again |

Notes for the implementation:

- None of the programs needs an entitlement. The notary service refuses
  `com.apple.security.get-task-allow`, which is only added by debug signing.
- Sign the inner executable, then the bundle, without `--deep`, as the
  [draft script](#starting-points) did. The VST3 SDK writes `moduleinfo.json`
  into the bundle after building; it must exist before the final signature
  (the 18 October [preview record](../../archive/releases/2026-10-18-developer-preview.md)
  found that ordering defect once).
- Re-signing with `codesign --force` replaces the ad-hoc signature that
  `cmake/ConvolverDebugSymbols.cmake` and the SDK apply at build time. Signing
  does not change a binary's UUID, so the kept `.dSYM` files still match
  (check this with `dwarfdump --uuid` in the first signed run).
- Stapling adds a ticket file to each bundle, so the artifact manifest and the
  SHA-256 sums must be computed after stapling. The release guide already says
  "Signing comes before the final hashes".
- Each release makes four notary submissions (ZIP contents and `.pkg`, for two
  architectures). Apple says most finish within 5 minutes and 98% within 15,
  and asks for at most 75 a day.
- Signed files are not byte-reproducible (the secure timestamp differs), but
  the corresponding source and the build are unchanged. A person building the
  source gets ad-hoc-signed bundles, which run on their own Mac without any
  Apple key, so signing does not stop users running modified versions. The
  production gate's licence review should still confirm this for the Audio
  Unit's GPL-3.0-or-later terms.

## Account and cost

- **Membership:** the Apple Developer Program costs "99 USD ... in local
  currency where available", renewed yearly. Fee waivers are only for
  nonprofits, accredited schools and government bodies, and exclude
  "an individual, sole proprietor, or single-person business".
- **Individual or organization:** for an individual membership Apple says
  "your personal legal name will be listed as the seller on the App Store".
  In practice the Developer ID certificates carry the same name
  (`Developer ID Application: <name> (<Team ID>)`), which users can see in
  the installer and with `codesign -dv`. An organization needs a legal entity
  (no trade names) and a D-U-N-S number, "free in most jurisdictions". This is a
  permanent, user-facing identity, so it is jrp's choice
  ([decisions](#decisions-for-jrp)).
- **Certificates:** only the Account Holder can create Developer ID
  certificates, at most five of each kind. Apple's page does not give their
  lifetime here (believed to be five years; check when created). Timestamped
  code keeps working after the certificate expires, but Apple says
  "Installer packages signed with a Developer ID Installer certificate that
  has expired must be re-signed". Published files are never replaced
  ([releasing](../../develop/releasing.md)), so from then on an old release's
  `.pkg` stops opening and its users need a newer release or that release's
  ZIP, which is unaffected.
- **Notary credentials:** `notarytool` accepts an App Store Connect **team**
  API key (individual keys cannot use it) or an Apple Account with an
  app-specific password. Use the team key: it can be revoked on its own and
  is not tied to jrp's password. Creating one needs the Admin role, which the
  Account Holder has, and the first time the Account Holder must request
  access to the App Store Connect API. A key with the Developer role is reported to work for
  notarization (Apple forum, not documented); use App Manager if it does not.
- **Not covered here:** Windows Authenticode is a separate certificate and
  cost, tracked under the same production gate.

## Steps only jrp can take

Agents cannot enrol, create certificates or change repository settings. In
order:

1. Choose an individual or organization membership, and enrol at
   [developer.apple.com/programs/enroll](https://developer.apple.com/programs/enroll/)
   with an Apple Account that has two-factor authentication. Apple verifies
   the identity before approving.
2. On the Mac, create a **Developer ID Application** and a **Developer ID
   Installer** certificate (Xcode > Settings > Accounts > Manage
   Certificates, or the Certificates page with a request from Keychain
   Access). Note the Team ID.
3. Export each certificate with its private key as a `.p12` file with a
   strong password.
4. In [App Store Connect](https://appstoreconnect.apple.com/), open Users
   and Access > Integrations > App Store Connect API (the first time, request
   access to the API), then Team Keys. Generate a key (role Developer),
   download the `.p8` file (Apple offers it once only and keeps no copy) and
   note the Key ID and the Issuer ID.
5. Back up both `.p12` files with their passwords, and the `.p8` file, in an
   encrypted store that is not only on this Mac (a password manager or an
   encrypted disk image). DTS advises saving the exported identity "in a
   secure location".
6. Optional but recommended: the [local trial](#local-trial) below, to prove
   the account works before any pipeline change.
7. In the repository, Settings > Environments, create `macos-signing` with
   jrp as a required reviewer and deployments limited to the `main` branch
   and `v*` tags, and add the secrets listed in
   [pipeline changes](#pipeline-changes). The reviewer matters because tags
   are not protected: anyone who can push could tag a commit carrying a
   modified workflow and start a run on it.
8. After the first signed candidate run passes, delete the loose copies of
   the `.p12` and `.p8` files (in Downloads and elsewhere), keeping the
   encrypted backup. If a certificate's private key is lost, create a new
   certificate; revoke it only if it may be compromised (Apple's Compromised
   Certificates steps). If the API key is lost or may be compromised, revoke
   it in App Store Connect and make a new one: Apple says "If the key becomes
   lost or compromised, remember to revoke it immediately."
9. Put the membership renewal and certificate expiry dates in a calendar.

### Local trial

With a candidate ZIP downloaded (for example from the 0.13.2 release), on
jrp's Mac, using the names from steps 2 and 4, in the folder holding both
downloads. Store the notary key once,
from wherever the `.p8` file was saved, so that no later command names it and
it is never inside a folder that is uploaded:

```sh
xcrun notarytool store-credentials convolver-notary --key "$HOME/Downloads/AuthKey_KEYID.p8" --key-id KEYID --issuer ISSUER
```

Check the Installer identity on the published package (its contents keep
their ad-hoc signatures, so this package is not for notarizing):

```sh
productsign --sign "Developer ID Installer: NAME (TEAMID)" Convolver-0.13.2-preview-macos-arm64-unsigned.pkg signed-trial.pkg
pkgutil --check-signature signed-trial.pkg
```

Then sign the bundles and tools, check them, copy only them into a
submission folder, and look at the ZIP's contents (`zipinfo` should list
only `VST3/`, `Components/` and `bin/`) before submitting it and stapling:

```sh
ditto -x -k Convolver-0.13.2-preview-macos-arm64.zip trial
cd trial
id="Developer ID Application: NAME (TEAMID)"
for f in VST3/Convolver.vst3/Contents/MacOS/Convolver Components/Convolver.component/Contents/MacOS/Convolver bin/convolver bin/convolver-preset bin/convolver-equalizer-apo; do
  codesign --force --options runtime --timestamp --sign "$id" "$f"
done
codesign --force --options runtime --timestamp --sign "$id" VST3/Convolver.vst3
codesign --force --options runtime --timestamp --sign "$id" Components/Convolver.component
codesign --verify --strict VST3/Convolver.vst3 Components/Convolver.component bin/*
rm -rf ../submit ../submit.zip
mkdir ../submit
ditto VST3 ../submit/VST3
ditto Components ../submit/Components
ditto bin ../submit/bin
ditto -c -k ../submit ../submit.zip
zipinfo -1 ../submit.zip
xcrun notarytool submit ../submit.zip --keychain-profile convolver-notary --wait
for b in VST3/Convolver.vst3 Components/Convolver.component; do
  xcrun stapler staple "$b" && xcrun stapler validate "$b"
done
```

The trial is done when the submission reports `status: Accepted` and both
bundles validate. Read the log either way
(`xcrun notarytool log ID --keychain-profile convolver-notary`). The trial's
files are for checking only, not for publishing. Never put a `.p12` or `.p8`
file in a folder that is zipped, uploaded or committed. Keep the `.p8` file
in the step 5 backup: step 7 needs it, and Apple will not offer it again.

## Pipeline changes

These are for the implementation pull request, after jrp's setup. They keep
the unsigned path for pull requests, pushes to `main` and the weekly run, so
no secret is exposed to a pull request.

- **Secrets** in the `macos-signing` environment: `DEVELOPER_ID_APPLICATION_P12`
  and `DEVELOPER_ID_INSTALLER_P12` (base64), their passwords,
  `SIGNING_KEYCHAIN_PASSWORD` (any random string), `NOTARY_KEY_P8` (base64),
  `NOTARY_KEY_ID` and `NOTARY_ISSUER_ID`. The Team ID can be a plain variable.
- **Split the candidate step.** Today `candidate.py` is one workflow step
  that builds the dependencies, compiles, runs CTest and the VST3 validator,
  stages, seals, zips and makes the `.pkg`. Split it into a build phase
  (everything up to `keep_symbols`) and a package phase (sign when asked,
  then seal, zip, verify and make the `.pkg`), run as two steps. Unsigned
  runs run both phases as now. The build, the tests and any process they
  leave behind must never have the signing identity or the notary key.
- **`.github/workflows/release.yml`:** the macOS candidate job uses the
  environment only on a manual run on `main` or a `v*` tag (manual runs on
  tags are how candidates for a release are made today). Only the package
  step gets the secrets, in its own `env:`. That step creates the keychain
  itself: `security create-keychain`, `set-keychain-settings -lut` with a
  timeout that covers the notary waits, `unlock-keychain`, `import` with
  `-T /usr/bin/codesign -T /usr/bin/productbuild` (not `-A`, which lets any
  program use the key), `set-key-partition-list` and `list-keychain`; it
  writes the `.p8` key to `$RUNNER_TEMP`. A following `if: always()` step
  deletes the keychain and the key file. Keys and certificates stay in
  `$RUNNER_TEMP`, never under `build/`, whose files are uploaded.
  `candidate.py` receives identity names and the key's path, never a
  password on its command line, because `run()` prints the command when it
  fails. Both the step's 40-minute and the job's 60-minute limits grow, by
  30 to 60 minutes, for two notary waits in a row (Apple: typically under an
  hour). The `macos_installer.py` step's `*-unsigned.pkg` pattern changes
  with the file name.
- **`scripts/release/candidate.py`, package phase on Darwin:** sign the
  programs and bundles, verify with `codesign --verify --strict`, make a ZIP
  of the signed items only and submit it, check the result is `Accepted`,
  staple and validate both bundles; then seal, zip and verify as now. After
  `pkgbuild`, build the product package with `productbuild --sign` and
  `--timestamp`, submit it, staple it, and check it with `stapler validate`,
  `pkgutil --check-signature` and `spctl --assess --type install`. Record
  `signed` and `notarized` as true, with the submission IDs, in the platform
  record and in `installer_evidence` for the `.pkg`, which
  `promotion_gate.py` checks separately. Notary logs and submission ZIPs go
  in the work directory or the diagnostics artifact, never in `build/assets`,
  which is published and checked against `SHA256SUMS`.
- **Checks:** `tests/release/macos_installer.py` and `package_smoke.py`
  check the signing authority and Team ID when signed, and the ad-hoc seal
  otherwise, and both must pass on the signed files (the hardened runtime
  changes how the tools run). `verify_artifact.py` keeps checking the
  manifest, which now includes the stapled tickets. `promotion_gate.py`
  already requires `signed` and `notarized` for production.
- **Publishing and documents:** `prepare_release.py`'s asset names and
  "unsigned" notes, `publish.yml`'s expectations, the install guide's macOS
  steps, the release guide's production gates, the decisions log, status and
  the changelog, in the same pull request. jrp's lasting steps (renewal,
  backup, revoking, rotating the secrets) move from this plan to the
  [release guide](../../develop/releasing.md) then.

### Starting points

The draft script at `f4519022c` (`git show f4519022c:scripts/release/sign-macos.sh`)
shows the order (sign, verify, `productbuild --sign`, `notarytool submit
--wait`, staple, `spctl`), but it predates the Audio Unit, the ZIP and the
current tools and installer identities, and reads its notary credentials
from a keychain profile on a Mac, where CI needs an API key file. Rebuild it inside `candidate.py` rather than restoring it.

## Decisions for jrp

1. **Individual or organization membership.** Recommended: individual,
   unless a legal entity already exists. It is quicker and cheaper; the cost
   is that jrp's legal name is shown as the developer.
2. **When to sign.** The 21 October 2026 decision keeps signing for the
   production gate. Recommended: sign the next preview once the account
   exists. It removes the hardest step for Mac users and changes no source
   code, so it fits the runtime freeze. The hardened runtime does change how
   the tools run (they ignore `DYLD_*` variables and refuse a debugger), which
   the package checks on the signed files cover.
3. **Unsigned files and names.** Recommended: once signing works, publish
   only the signed macOS files rather than both, and drop `-unsigned` from
   the `.pkg` name. That changes the published file names.
4. **Approval for each signing run.** Recommended: jrp as a required
   reviewer on the environment, approving on the run page before the macOS
   jobs start. Tags are not protected, so limiting the environment to `v*`
   tags alone does not limit who can start a signing run. The alternative is
   a tag ruleset letting only jrp and the version bot create `v*` tags,
   which needs checking that the bot can still tag.

Estimated effort once the account exists: about an hour of jrp's time for
steps 2 to 8, then one implementation pull request (REL-1, macOS part) with
two or three candidate runs to get the first signed, notarized packages.
