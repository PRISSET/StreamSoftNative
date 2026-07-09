#include "core_app.hpp"

// Standalone headless build — same logic as the merged gui+core exe
// (see gui/main.cpp), just without a GUI attached. Useful for running
// on a machine that only needs the background service, and for testing
// core in isolation without pulling in Qt.
int main() {
    streamsoft::run_core();
    return 0;
}
