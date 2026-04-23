// Structura — C++ desktop viewer entry point (sketch)
//
// Build (example, Linux/macOS):
//   g++ -std=c++17 -O2 main.cpp var_store.cpp expr_eval.cpp document.cpp
//       renderer.cpp -o structura-view
//
// Usage:
//   structura-view ocean_pressure.syn
//   structura-view game_store.syn

#include <iostream>
#include <stdexcept>
#include "document.hpp"
#include "renderer.hpp"

// ─── Event loop (skeleton) ────────────────────────────────────────────────────
//
// In a real GUI build this would be an SDL2 / Dear ImGui / Qt event loop.
// For the terminal backend we do a single-shot render then exit.
//
static void runEventLoop(Document& doc, Renderer& renderer) {
    renderer.render(doc);

    // GUI loop would look like:
    //
    //   while (!quit) {
    //       Event e = pollEvent();
    //       if (e.type == SLIDER_CHANGED) {
    //           doc.varStore().set(e.varName, e.newValue);
    //           renderer.renderDirty(doc);   // only changed regions
    //           swapBuffers();
    //       }
    //   }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: structura-view <file.syn>\n";
        return 1;
    }

    try {
        auto doc = Document::load(argv[1]);
        TerminalTarget target;
        Renderer renderer(target);
        runEventLoop(*doc, renderer);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
