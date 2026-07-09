import sys

import webview

SETTINGS_URL = "http://127.0.0.1:8099/settings"


def main() -> None:
    webview.create_window(
        "Stream Panel — настройки",
        SETTINGS_URL,
        width=1180,
        height=780,
        min_size=(860, 600),
        background_color="#050506",
    )
    webview.start()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
