# ─────────────────────────────────────────────────────
#  NetCommand Client — Makefile
#  Targets: Linux, macOS, Windows (MinGW/MSVC)
#
#  Usage:
#    make              # auto-detect platform
#    make PLATFORM=windows
#    make clean
# ─────────────────────────────────────────────────────

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I../common -I.

# ── Platform detection ────────────────────────────────
UNAME := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(UNAME), Linux)
  PLATFORM  := linux
  TARGET    := netcommand-client
  CXXFLAGS  += -DLINUX
  LDFLAGS   := -lpthread -lX11 -lXtst
  # stb_image_write is header-only; libjpeg optional (using stb)
  SRCS      := main.cpp screencapture.cpp inputinjector.cpp
endif

ifeq ($(UNAME), Darwin)
  PLATFORM  := macos
  TARGET    := netcommand-client
  CXXFLAGS  += -DMACOS
  LDFLAGS   := -framework CoreGraphics -framework ApplicationServices \
               -framework CoreFoundation -lpthread
  SRCS      := main.cpp screencapture.cpp inputinjector.cpp
endif

ifeq ($(PLATFORM), windows)
  CXX       := x86_64-w64-mingw32-g++
  TARGET    := netcommand-client.exe
  CXXFLAGS  += -DWINDOWS -mwindows
  LDFLAGS   := -lws2_32 -lgdi32 -lpthread
  SRCS      := main.cpp screencapture.cpp inputinjector.cpp
endif

OBJS := $(SRCS:.cpp=.o)

# ── Rules ─────────────────────────────────────────────
.PHONY: all clean

all: check_stb $(TARGET)

# Download stb_image_write.h if not present
check_stb:
	@if [ ! -f stb_image_write.h ]; then \
	  echo "Downloading stb_image_write.h..."; \
	  curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h \
	       -o stb_image_write.h || \
	  wget -q https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h; \
	fi

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) netcommand-client netcommand-client.exe
