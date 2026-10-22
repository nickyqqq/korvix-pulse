"""Regenerate DigitalClock/ClockWebPage.h from web/page.html.

Run from the project folder:  python tools/make_web_page.py
"""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
html = (root / "web" / "page.html").read_text(encoding="utf-8")
if ')CLOCKPAGE"' in html:
    raise SystemExit("page.html must not contain the text )CLOCKPAGE\"")
out = ("#pragma once\n"
       "// Generated from web/page.html by tools/make_web_page.py. Edit the HTML, then regenerate.\n"
       "#include <pgmspace.h>\n"
       'static const char CLOCK_WEB_PAGE[] PROGMEM = R"CLOCKPAGE(' + html + ')CLOCKPAGE";\n')
(root / "DigitalClock" / "ClockWebPage.h").write_text(out, encoding="utf-8")
print(f"Wrote DigitalClock/ClockWebPage.h ({len(html.encode('utf-8'))} bytes of HTML)")
