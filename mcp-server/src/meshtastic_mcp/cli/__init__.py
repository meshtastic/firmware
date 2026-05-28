"""Command-line entry points that sit alongside the MCP server.

Modules here are loaded on-demand by `[project.scripts]` entries in
`pyproject.toml`. They are NOT imported by `meshtastic_mcp.server` or the
admin/info tool surface — the MCP server stays pure stdio JSON-RPC.
"""
