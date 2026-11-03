Stand-ins for the console SDK headers that src/gui/GuiText.cpp and
GuiElement.cpp pull in, so they build on the host (test_guitext.cpp). Each
file declares only the names those two units use; they are searched before
src/, so they replace the real headers of the same path.
