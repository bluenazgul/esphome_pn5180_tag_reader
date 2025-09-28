# fix_pn5180_spi_esp8266.py
# Patches tueddy/PN5180-Library for ESP8266: use SPI.begin() without pin args
from pathlib import Path
import io

Import("env")

def apply_patch():
    envname = env["PIOENV"]
    libdir = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / envname / "PN5180-Library"
    cpp = libdir / "PN5180.cpp"
    if not cpp.exists():
        print("[fix_pn5180_spi_esp8266] PN5180.cpp not found yet – will try again later.")
        return False

    s = cpp.read_text(encoding="utf-8", errors="ignore")
    if "ARDUINO_ARCH_ESP8266" in s and "PN5180_SPI.begin()" in s:
        print("[fix_pn5180_spi_esp8266] Patch already present.")
        return True

    OLD = "PN5180_SPI.begin(PN5180_SCK, PN5180_MISO, PN5180_MOSI, PN5180_NSS);"
    NEW = (
        "#if defined(ARDUINO_ARCH_ESP8266)\n"
        "  PN5180_SPI.begin();\n"
        "#else\n"
        "  PN5180_SPI.begin(PN5180_SCK, PN5180_MISO, PN5180_MOSI, PN5180_NSS);\n"
        "#endif"
    )

    if OLD not in s:
        print("[fix_pn5180_spi_esp8266] Expected line not found; no changes made.")
        return True  # don't fail build; maybe already compatible

    s = s.replace(OLD, NEW)
    cpp.write_text(s, encoding="utf-8")
    print("[fix_pn5180_spi_esp8266] Patch applied to", cpp)
    return True

# Try now; if lib not downloaded yet, patch just before compiling main.cpp
if not apply_patch():
    env.AddPreAction("$BUILD_DIR/src/main.cpp.o", lambda *a, **k: apply_patch())
