<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Sign and notarize the macOS packages

For jrp, who has to set up the Apple account, and for whoever then implements
the macOS part of REL-1: what Developer ID signing and notarization need, what
it costs, which steps only jrp can take, and what changes in the release
pipeline.

**In short:** signing needs a paid Apple Developer Program membership (99 USD a
year), two Developer ID certificates that only the account holder can create,
and an App Store Connect API key for Apple's notary service. jrp makes those
on his Mac and stores them as secrets in a protected GitHub environment. The
macOS candidate job then signs every program with the hardened runtime and a
secure timestamp, has the bundles and the installer notarized, staples the
tickets, and seals the manifest and hashes afterwards, so the existing checks
run on the signed bytes. Users would then install the `.pkg` with no Gatekeeper
warning, and plug-ins copied from the ZIP would load without the `xattr` step.
This page changes no workflow, script or decision. Contents: [what users
get](#what-users-get), [what is signed](#what-is-signed),
[account and cost](#account-and-cost), [steps only jrp can
take](#steps-only-jrp-can-take), [pipeline changes](#pipeline-changes),
[decisions for jrp](#decisions-for-jrp).

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
| The ZIP | Not signable | | Yes, as a submission | Not possible: staple the bundles inside, then zip again |

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
- **Individual or organization:** an individual membership lists "your
  personal legal name" as the developer, and that name appears in the
  certificate (`Developer ID Application: <name> (<Team ID>)`), which users
  can see in the installer and with `codesign -dv`. An organization needs a
  legal entity (no trade names) and a free D-U-N-S number. This is a
  permanent, user-facing identity, so it is jrp's choice
  ([decisions](#decisions-for-jrp)).
- **Certificates:** only the Account Holder can create Developer ID
  certificates, at most five of each kind. Apple's page does not give their
  lifetime here (believed to be five years; check when created). Timestamped
  code keeps working after the certificate expires, but Apple says
  "Installer packages signed with a Developer ID Installer certificate that
  has expired must be re-signed", so old `.pkg` files may need re-signing then;
  the ZIPs are unaffected.
- **Notary credentials:** `notarytool` accepts an App Store Connect **team**
  API key (individual keys cannot use it) or an Apple Account with an
  app-specific password. Use the team key: it can be revoked on its own and
  is not tied to jrp's password. Creating one needs the Admin role, which the
  Account Holder has. A key with the Developer role is reported to work for
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
4. In [App Store Connect](https://appstoreconnect.apple.com/), Users and
   Access > Integrations > Team Keys, generate a key (role Developer),
   download the `.p8` file (offered once only) and note the Key ID and the
   Issuer ID.
5. Optional but recommended: the [local trial](#local-trial) below, to prove
   the account works before any pipeline change.
6. In the repository, Settings > Environments, create `macos-signing`,
   limited to the `main` branch and `v*` tags, and add the secrets listed in
   [pipeline changes](#pipeline-changes).
7. After the first signed candidate run passes, delete the exported `.p12`
   and `.p8` copies. Keep the originals in the Mac's keychain (DTS suggests a
   separate keychain with its own password). A lost key is replaced by a new
   certificate or API key, not by revocation; revoke only a compromised one.
8. Put the membership renewal and certificate expiry dates in a calendar.

### Local trial

With a candidate ZIP downloaded (for example from the 0.13.2 release), on
jrp's Mac, using the names from steps 2 and 4:

```sh
ditto -x -k Convolver-0.13.2-preview-macos-arm64.zip trial
cd trial
id="Developer ID Application: NAME (TEAMID)"
for f in VST3/Convolver.vst3/Contents/MacOS/Convolver Components/Convolver.component/Contents/MacOS/Convolver bin/convolver bin/convolver-preset bin/convolver-equalizer-apo; do
  codesign --force --options runtime --timestamp --sign "$id" "$f"
done
codesign --force --options runtime --timestamp --sign "$id" VST3/Convolver.vst3
codesign --force --options runtime --timestamp --sign "$id" Components/Convolver.component
xcrun notarytool store-credentials convolver-notary --key AuthKey_KEYID.p8 --key-id KEYID --issuer ISSUER
ditto -c -k . ../submit.zip
xcrun notarytool submit ../submit.zip --keychain-profile convolver-notary --wait
for b in VST3/Convolver.vst3 Components/Convolver.component; do
  xcrun stapler staple "$b" && xcrun stapler validate "$b"
done
```

The trial is done when the submission reports `status: Accepted` and both
bundles validate. Read the log either way
(`xcrun notarytool log ID --keychain-profile convolver-notary`). The trial's
files are for checking only, not for publishing.

## Pipeline changes

These are for the implementation pull request, after jrp's setup. They keep
the unsigned path for pull requests, pushes to `main` and the weekly run, so
no secret is exposed to a pull request.

- **Secrets** in the `macos-signing` environment: `DEVELOPER_ID_APPLICATION_P12`
  and `DEVELOPER_ID_INSTALLER_P12` (base64), their passwords,
  `SIGNING_KEYCHAIN_PASSWORD` (any random string), `NOTARY_KEY_P8` (base64),
  `NOTARY_KEY_ID` and `NOTARY_ISSUER_ID`. The Team ID can be a plain variable.
- **`.github/workflows/release.yml`:** the macOS candidate job uses the
  environment only on a manual run on `main` or a `v*` tag (manual runs on
  tags are how candidates for a release are made today). A step makes a
  temporary keychain and imports both certificates as GitHub's page shows
  (`security create-keychain`, `import`, `set-key-partition-list`,
  `list-keychain`), and writes the `.p8` key to `$RUNNER_TEMP`. The job's
  time limit grows by about 30 minutes for the notary service.
- **`scripts/release/candidate.py`:** a signing step on Darwin, after
  `keep_symbols` and before the artifact manifest: sign the programs and
  bundles, verify with `codesign --verify --strict`, submit them in one ZIP,
  check the result is `Accepted` and keep the notary log, staple and validate
  both bundles; then seal, zip and verify as now. After `pkgbuild`, build the
  product package with `productbuild --sign` and `--timestamp`, submit it,
  staple it, and check it with `stapler validate`, `pkgutil --check-signature`
  and `spctl --assess --type install`. Record `signed` and `notarized` as
  true, with the submission IDs, in the candidate record. Signed packages
  drop `-unsigned` from their name.
- **Checks:** `tests/release/macos_installer.py` and `package_smoke.py`
  check the signing authority and Team ID when signed, and the ad-hoc seal
  otherwise; `verify_artifact.py` keeps checking the manifest, which now
  includes the stapled tickets. `promotion_gate.py` already requires
  `signed` and `notarized` for production.
- **Publishing and documents:** `prepare_release.py`'s asset names and
  "unsigned" notes, `publish.yml`'s expectations, the install guide's macOS
  steps, the release guide's production gates, the decisions log, status and
  the changelog, in the same pull request.

### Starting points

The draft script at `f4519022c` (`git show f4519022c:scripts/release/sign-macos.sh`)
shows the order (sign, verify, `productbuild --sign`, `notarytool submit
--wait`, staple, `spctl`), but it predates the Audio Unit, the ZIP and the
current tools and installer identities, and uses a keychain profile rather
than an API key. Rebuild it inside `candidate.py` rather than restoring it.

## Decisions for jrp

1. **Individual or organization membership.** Recommended: individual,
   unless a legal entity already exists. It is quicker and cheaper; the cost
   is that jrp's legal name is shown as the developer.
2. **When to sign.** The 21 October 2026 decision keeps signing for the
   production gate. Recommended: sign the next preview once the account
   exists. It removes the hardest step for Mac users and does not change the
   code, so it fits the runtime freeze.
3. **Unsigned files.** Recommended: once signing works, publish only the
   signed macOS files rather than both.
4. **Approval for each signing run.** Recommended: limit the environment to
   `main` and `v*` tags without a required reviewer, since jrp starts tag
   runs by hand anyway.

Estimated effort once the account exists: about an hour of jrp's time for
steps 2 to 7, then one implementation pull request (REL-1, macOS part) with
two or three candidate runs to get the first signed, notarized packages.
