<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Sign and notarize the macOS packages

For jrp when setting up or renewing Apple's signing credentials for REL-1. The
[release guide](../../develop/releasing.md#sign-a-macos-candidate) explains how
to request a signed candidate and check the downloads.

**In short:** the workflow uses two Developer ID identities and an App Store
Connect team API key in the protected `macos-signing` environment. The first
approved run still needs to qualify both Mac architectures.

Contents: [what users get](#what-users-get), [what is signed](#what-is-signed),
[account and cost](#account-and-cost), [account setup](#steps-only-jrp-can-take),
[environment secrets](#pipeline-changes).

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

For an unsigned Mac download, follow the [install guide](../../use/install.md#unsigned-previews).
The release notes identify the signing status of each download.

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
| The product `.pkg` | Developer ID Installer (`productsign`) | Timestamp | Yes | Yes |
| The ZIP | Not signable | | Its signed contents are, in a separate ZIP made only for the submission | Not possible: staple the bundles inside, then zip again |

How the workflow signs the packages:

- None of the programs needs an entitlement. The notary service refuses
  `com.apple.security.get-task-allow`, which is only added by debug signing.
- Sign the inner executable, then the bundle, without `--deep`. The VST3 SDK writes `moduleinfo.json`
  into the bundle after building; it must exist before the final signature
  (the 18 October [preview record](../../archive/releases/2026-10-18-developer-preview.md)
  found that ordering defect once).
- Re-signing with `codesign --force` replaces the ad-hoc signature that
  `cmake/ConvolverDebugSymbols.cmake` and the SDK apply at build time. Signing
  does not change a binary's UUID, so the kept `.dSYM` files still match
  (the verification job compares their UUIDs).
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
  permanent, user-facing identity.
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
  cost. The 1.0 [release policy](../../develop/releasing.md#production-gates)
  permits unsigned Windows packages.

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
   Access). If using Keychain Access, generate a separate certificate signing
   request for each certificate. Both identities must match the Team ID in
   `packaging/release-policy.json`.
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
6. In the repository, Settings > Environments, create `macos-signing` with
   jrp as a required reviewer and deployments limited to the `main` branch
   and `v*` tags, and add the secrets listed in
   [pipeline changes](#pipeline-changes). The reviewer matters because tags
   are not protected: anyone who can push could tag a commit carrying a
   modified workflow and start a run on it.
7. After the first signed candidate run passes, delete the loose copies of
   the `.p12` and `.p8` files (in Downloads and elsewhere), keeping the
   encrypted backup. If a certificate's private key is lost, create a new
   certificate; revoke it only if it may be compromised (Apple's Compromised
   Certificates steps). If the API key is lost or may be compromised, revoke
   it in App Store Connect and make a new one: Apple says "If the key becomes
   lost or compromised, remember to revoke it immediately."
8. Put the membership renewal and certificate expiry dates in a calendar.

## Pipeline changes

The [release guide](../../develop/releasing.md#sign-a-macos-candidate) describes
how to request signing and verify the resulting downloads. Ordinary candidates
use ad-hoc Mac signatures.

Configure these eight secrets in the protected `macos-signing` environment:

| Secret | Value |
| --- | --- |
| `DEVELOPER_ID_APPLICATION_P12` | Application certificate and private key exported as PKCS#12, Base64 encoded |
| `DEVELOPER_ID_APPLICATION_P12_PASSWORD` | Password used for that export |
| `DEVELOPER_ID_INSTALLER_P12` | Installer certificate and private key exported as PKCS#12, Base64 encoded |
| `DEVELOPER_ID_INSTALLER_P12_PASSWORD` | Password used for that export |
| `SIGNING_KEYCHAIN_PASSWORD` | Random password for the temporary runner keychain |
| `NOTARY_KEY_P8` | App Store Connect team private key, Base64 encoded |
| `NOTARY_KEY_ID` | Key ID from the Team Keys page |
| `NOTARY_ISSUER_ID` | Issuer ID from the same page |

To qualify the setup, follow [Sign a macOS candidate](../../develop/releasing.md#sign-a-macos-candidate)
on `main`. Keep the run link, both notarization submission IDs per architecture,
and the SHA-256 values of the final ZIPs and installers.
