#include "EmilyBrain.h"

// Maak één globaal 'emily' object aan. De constructor in EmilyBrain.cpp wordt nu aangeroepen.
EmilyBrain emily;

void setup() {
    // Roep de setup-functie van ons emily-object aan.
    emily.setup();
}

void loop() {
    // Roep de loop-functie van ons emily-object aan.
    emily.loop();
}