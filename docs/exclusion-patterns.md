# Pattern exclusions

In Settings > Behavior & integration, enter one glob per line. Exclusions are
relative to either synchronization root and apply to previews, copies and
deletions, including in mirror mode.

- \`*.tmp\` excludes that basename anywhere in the tree.
- \`cache/**\` excludes a directory and its descendants.
- \`**/*.log\` excludes log files in any directory.
- \`build/output?.bin\` matches one character except a path separator.

Patterns are case sensitive (including on WSL). No leading slash, backslash
or parent traversal (\`..\`) is accepted. Existing excluded files remain
untouched on both sides. Removing a rule reintroduces the affected paths
into synchronization. Recovery archives and journals are never filtered.
