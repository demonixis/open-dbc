# Security

## Running a program is running native code

Opening a `.dba` or `.dbpro` in OpenDBC and running it gives that program the
rights of your user account. A program can launch other programs (`EXECUTE FILE`),
load native plugins (`LOAD DLL`), open network connections and write files. Its
file paths resolve under the project folder, but that is not a sandbox. Run only
programs and plugins you trust, as you would any executable.

Exported games carry the same rights on the player's machine.

## Reporting a vulnerability

Report it privately through GitHub's
[private vulnerability reporting](https://github.com/demonixis/open-dbc/security/advisories/new),
not in a public issue. Include the OpenDBC version (`opendbc --version`), the
platform and, when possible, a program that reproduces it.

Only the latest release receives fixes.
