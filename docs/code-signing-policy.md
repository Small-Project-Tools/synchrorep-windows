# Code signing policy

**Status:** Application to SignPath Foundation is planned. No certificate has been issued to this project and unsigned builds must not be presented as signed.

Free code signing provided by SignPath.io, certificate by SignPath Foundation.

The policy follows the [SignPath Foundation open-source conditions](https://signpath.org/terms.html). Signing is available only after SignPath Foundation accepts the project.

## Team roles

- **Author / committer:** Sébastien Kus — maintains the project and its source code.
- **Reviewer:** Sébastien Kus — reviews external contributions before merge.
- **Release and signing approver:** Sébastien Kus — approves each signing request.
- Access control and mandatory multi-factor authentication on GitLab and SignPath must be verified before activation. This document does not imply they have already been verified.

## Privacy policy

Synchrorep does not transfer information to networked systems unless the user explicitly requests or configures synchronization to those endpoints. Configured remote targets (including FTP and SFTP) receive files, metadata and connection traffic required for synchronization. FTP is unencrypted and is not recommended for sensitive data. Connection secrets are kept in Windows Credential Manager. Logs, profile settings and recovery copies may contain file paths and metadata; users should protect their local machine and backups.

User-selected remote systems and third-party servers can have separate privacy policies. Synchrorep does not control these services. No telemetry or analytics collection is described by the project.

## Release and signing procedure

1. Changes are committed to the public [source repository](https://git.small-project.dev/pub/desktop/synchrorep-windows). Contributions from external authors require review by a trusted maintainer.
2. The CI test job validates the engine and integration tests. The Windows build generates the full MSI from source. Its dependent OSS DLLs are staged and included with license notices.
3. The MSI and portable archive are published as CI build artifacts. Only a designated release build that passed mandatory checks may be submitted for signing; unsigned upstream dependencies may be packaged but must not be separately signed as first-party binaries.
4. The release approver must verify the source revision, provenance and artifact hashes before authorizing signing. Signed binaries must be published without rebuilding or modifying the MSI.
5. Until SignPath approves the application and signing integration is configured, releases remain **unsigned**. The presence of this policy does not indicate certification.

## Open compliance items

- Confirm all maintainers use MFA on both GitLab and SignPath.
- Confirm source and build pipeline permissions enforce required reviews and release approval.
- Register the project and configure SignPath artifact configuration, trusted build source and signing policy.
- Confirm the release distribution is publicly accessible and traceable to the specific source revision.
- Verify licensing of every bundled runtime DLL and full compliance with the GPL-3.0 source distribution obligations.

Project website and download page: [Synchrorep at Small Project](https://small-project.dev/products/synchrorep#code-signing-policy).
