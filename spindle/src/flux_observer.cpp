#include "flux_observer.h"
#include <Arduino.h>
#include <math.h>

void FluxObserver::reset() {
    flux_alpha = flux_beta = 0.0f;
    i_alpha_prev = i_beta_prev = 0.0f;
    electrical_angle = electrical_angle_prev = 0.0f;
    angle_unwrapped = 0.0;
    first = true;
}

void FluxObserver::update(float i_alpha, float i_beta, float v_alpha, float v_beta, float dt) {
    if (!ready() || dt <= 0.0f) return;
    if (first) {   // no previous current for the inductive term yet
        i_alpha_prev = i_alpha;
        i_beta_prev  = i_beta;
    }

    // This work deviates slightly from the BSD 3 clause licence.
    // The work here is entirely original to the MESC FOC project, and not based
    // on any appnotes, or borrowed from another project. This work is free to
    // use, as granted in BSD 3 clause, with the exception that this note must
    // be included in where this code is implemented/modified to use your
    // variable names, structures containing variables or other minor
    // rearrangements in place of the original names I have chosen, and credit
    // to David Molony as the original author must be noted.

    // MXLEMMING Flux Observer
    float resistive_term_a = R * i_alpha;
    float resistive_term_b = R * i_beta;
    float inductive_term_a = L * (i_alpha - i_alpha_prev);
    float inductive_term_b = L * (i_beta - i_beta_prev);

    flux_alpha = constrain(flux_alpha + (v_alpha - resistive_term_a) * dt - inductive_term_a, -flux_linkage, flux_linkage);
    flux_beta  = constrain(flux_beta  + (v_beta  - resistive_term_b) * dt - inductive_term_b, -flux_linkage, flux_linkage);

    // Electrical angle
    electrical_angle = atan2f(flux_beta, flux_alpha);
    if (electrical_angle < 0.0f) electrical_angle += 2.0f * PI;

    // Electrical angle difference, unwrapped
    if (first) {
        first = false;
    } else {
        float d = electrical_angle - electrical_angle_prev;
        if (d > PI) d -= 2.0f * PI;
        else if (d < -PI) d += 2.0f * PI;
        angle_unwrapped += d;
    }

    i_alpha_prev          = i_alpha;
    i_beta_prev           = i_beta;
    electrical_angle_prev = electrical_angle;
}
