#pragma once

#include "Ui.h"

// Tab 0..4: Jog, Job, Setup, Log, More.
Ui::View* makeTab(int index);

// Full-screen pairing view (shown at boot when not paired, and from More).
void showPairing();

// Per-loop checks that are not tied to a view.
void viewsLoop();
