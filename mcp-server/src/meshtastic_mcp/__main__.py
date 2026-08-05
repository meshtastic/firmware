"""Entry point for `python -m meshtastic_mcp`."""

from meshtastic_mcp.server import app


def main() -> None:
    app.run()


if __name__ == "__main__":
    main()
