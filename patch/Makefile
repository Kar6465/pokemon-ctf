CXX      = g++
CXXFLAGS = -std=c++14 -O2 -Wall
# -lGLU is required: Graphics.cpp uses gluOrtho2D(). On macOS, GL/GLU ship as
# frameworks instead of -lGL/-lGLU. Homebrew's sdl2-config does not put its
# include prefix on the default search path, so add it explicitly there so the
# "<SDL2/SDL.h>" style includes resolve.
ifeq ($(shell uname),Darwin)
CXXFLAGS += -I$(shell sdl2-config --prefix)/include
LIBS     = $(shell sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf -lSDL2_mixer -framework OpenGL
else
LIBS     = $(shell sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lGL -lGLU
endif

SRCS = main.cpp Basics.cpp Character.cpp Database.cpp Dialogue.cpp Event.cpp \
       GUI.cpp Graphics.cpp Inputs.cpp Item.cpp Map.cpp Pokemon.cpp \
       Sound.cpp Tile.cpp

TARGET = pokemon_grey

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET)
