# Security Policy

## Overview

OMEGA (Oceanic Measurement & Environmental Geospatial Array) is an experimental open-source platform for underwater sensing, environmental data collection, logging, mapping, and geospatial visualization.

Because OMEGA involves hardware, sensors, embedded systems, and data handling, security and responsible disclosure matter. This policy explains which versions are supported and how to report security issues.

## Supported Versions

OMEGA is currently in an early development / pre-release stage.

At this time, security updates are provided on a best-effort basis for:

| Version | Supported          |
| ------- | ------------------ |
| Latest pre-release / main branch | Yes |
| Older pre-release builds | No |
| Archived or experimental branches | No |

## Reporting a Vulnerability

Please do **not** open a public GitHub issue for security vulnerabilities.

Instead, report vulnerabilities privately by contacting:

**Jonathan Capone**  
Project maintainer  
Contact: use the contact information listed on the project website or GitHub profile

When reporting a vulnerability, please include:

- A clear description of the issue
- Affected component(s)
- Steps to reproduce
- Proof of concept, logs, or screenshots if available
- Potential impact
- Suggested mitigation, if known

You will receive a response on a best-effort basis. I will try to acknowledge reports promptly and evaluate severity before any public disclosure.

## Preferred Disclosure Process

Please follow responsible disclosure:

1. Report the issue privately.
2. Allow time for review and mitigation.
3. Avoid public disclosure until the issue has been assessed and, when possible, addressed.

## Scope

This policy applies to security issues involving the OMEGA codebase and related project materials, including where applicable:

- Embedded/firmware code
- Data logging workflows
- Geospatial visualization tools
- Configuration files and scripts
- Documentation that could expose sensitive operational details

Examples of relevant issues include:

- Hardcoded credentials, tokens, or secrets
- Unsafe network exposure
- Remote code execution vulnerabilities
- Injection vulnerabilities
- Insecure file handling
- Privilege escalation
- Sensitive data exposure
- Authentication or authorization weaknesses in future connected components

## Out of Scope

The following are generally out of scope unless they create a clear and demonstrable security risk:

- Feature requests
- General coding bugs without security impact
- UI issues
- Theoretical concerns without a reproducible exploit path
- Vulnerabilities in third-party dependencies that are not used in a security-relevant way by OMEGA
- Issues requiring physical access to personally modified hardware, unless they expose a broader design flaw

## Hardware and Field Use Notice

OMEGA is an experimental research and development project. It is **not** certified for safety-critical, mission-critical, navigational, industrial, or life-support use.

Users are responsible for safe deployment in marine, outdoor, electrical, and field environments. Always:

- Protect devices from water intrusion and corrosion
- Use appropriate power regulation and circuit protection
- Validate sensor output before relying on it
- Avoid exposing credentials or wireless interfaces in the field
- Review local laws and regulations before deploying radio, GPS, or wireless systems

## Data and Privacy

OMEGA may collect environmental, geographic, or sensor data depending on configuration. Users and contributors should avoid:

- Committing API keys, tokens, passwords, or private certificates
- Publishing sensitive coordinates for protected or private locations without permission
- Exposing personally identifiable information in logs, datasets, screenshots, or example files

If you discover sensitive information committed to the repository, please report it privately as a security issue.

## Security Best Practices for Contributors

Contributors should, whenever possible:

- Keep secrets out of source control
- Use environment variables or ignored local config files for credentials
- Review dependencies before adding them
- Minimize exposed network services and debug interfaces
- Validate and sanitize external inputs
- Fail safely when sensors or communications behave unexpectedly
- Document any security-relevant configuration clearly

## No Warranty

OMEGA is provided as-is, without warranty of any kind. Security review is ongoing and best-effort, especially while the project remains in alpha development.

## Thank You

Responsible reporting helps make OMEGA safer and more reliable for everyone using or studying the project.
