# Native sharing prototype

`sharedfolders.h` is a local read-only directory-catalog foundation for the planned peer file browser. It has no listener, no automatic roots, and no actual file-content transfer/read API.

Callers explicitly add selected roots and may remove them at runtime. Public root listings contain an ID/label, not a native path. Directory results have a bounded page size; links and Windows reparse entries are not followed. Directory work must run outside the UI thread.

Standalone regression tests are in `server/tests/sharedfolders_test.cpp`. The metadata checks are not sufficient to authorize future file reads/writes: transport integration still needs authenticated peer grants and handle-based containment/identity checks at the moment of opening a file.

Next: define the paired control protocol, implement native TLS/session/grant handling, add an Android Storage Access Framework provider, and connect both providers to actual Files screens. Keep bulk bytes away from the mouse-input socket/event loop.
