# ─────────────────────────────────────────────────────
#  NetCommand Admin — Makefile  (wraps qmake + make)
#
#  Requires: Qt 6 (or Qt 5) with Network module
#
#  Usage:
#    make              # build admin GUI
#    make run          # build + run
#    make clean
#
#  Set QTDIR if qmake is not in PATH:
#    make QTDIR=/usr/local/opt/qt6
# ─────────────────────────────────────────────────────

QTDIR    ?=
QMAKE    := $(if $(QTDIR),$(QTDIR)/bin/qmake,qmake)
TARGET   := netcommand-admin

.PHONY: all run clean distclean

all: Makefile.qmake
	$(MAKE) -f Makefile.qmake

Makefile.qmake: netcommand-admin.pro
	$(QMAKE) -o Makefile.qmake netcommand-admin.pro

run: all
	./$(TARGET)

clean:
	$(MAKE) -f Makefile.qmake clean 2>/dev/null || true
	rm -f Makefile.qmake moc_*.cpp moc_*.h *.o

distclean: clean
	rm -f $(TARGET) $(TARGET).exe
