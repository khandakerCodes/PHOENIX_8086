# Security

Phoenix-8086 is a teaching kernel for real-mode x86. Real mode has no memory protection or privilege separation, so the kernel is not a security boundary and should not be used as one.

Reports are still welcome for problems in the host-side tools, where they matter: the telemetry bridge (`bridge/`), the dashboard (`dashboard/`), and the build scripts (`tools/`). Please report these privately through GitHub's "Report a vulnerability" option on the repository's Security tab, not in a public issue.
