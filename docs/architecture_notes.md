# Architecture notes

The current implementation uses a small in-memory `unordered_map` as the storage engine. This is intentionally simple and easy to inspect.

Future milestones should separate concerns into modules such as:

- storage layer
- buffer or page manager
- WAL / recovery
- index structures
- query parser and execution

The goal is to keep each milestone small enough to understand end-to-end.
