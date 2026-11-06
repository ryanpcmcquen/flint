# Repository Instructions

## Code style

- Do not use end-of-line comments (trailing `//` or `/* ... */`). Put comments on their own line above the code they describe.
- Exception: the polyglot `Makefile` uses `# \` comment-continuation lines to hide nmake directives from GNU make; those must stay inline.
- Flint is C99 + libc only. Avoid adding dependencies.
- Keep the README and TODO current when behavior changes.
