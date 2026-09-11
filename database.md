This document describes how /calc/db will look

TABLES:

Packages
Hash (Key) | Name (Optional String) | Path (String) | Local (Bool) |
-----------+------------------------+---------------+--------------+

Build Dependencies

Package (Hash) | Dependency (Hash) |
---------------+-------------------+

Runtime Dependencies

Package (Hash) | Dependency (Hash) |
---------------+-------------------+

To find runtime dependencies, this simple command can be used and then filtered out

<!-- find <path> -type f -exec strings -f {} + | grep -o '/calc/str/[a-zA-Z0-9]{64}(-[^/]+)?' -->

find %PATH SUBSTITUDED IN% -type f -exec strings -f {} + | grep -Eo '/calc/str/[a-zA-Z0-9]{64}(-[a-z0-9A-Z_-]+)?' | sort | uniq

We can run this via popen,