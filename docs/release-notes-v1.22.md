# Release Notes - v1.22

These notes summarize changes introduced after [`v1.21`](https://github.com/MaslowCNC/Maslow_4/releases/tag/v1.21) through `v1.22.0`.

## Highlights

- **Calibration reliability and performance updates**
  - Added a Levenberg-Marquardt calibration path with analytical Jacobian and Schur-complement solve.
  - Moved Find Anchors LM computation to firmware and added watchdog servicing/fallback handling to prevent resets during heavy calibration runs.
  - Improved calibration diagnostics, measurement logging, and retry behavior when convergence fails.
  - Simplified/relaxed several calibration guard rails based on field feedback.

- **Apply Tension safety and usability improvements**
  - Added configurable belt retraction limits with improved validation.
  - Added a safety pause with continue/cancel prompt during apply tension.
  - Updated defaults and UI guidance/tooltips for safer first-time behavior.

- **Z-home and startup safety fixes**
  - Improved startup validation of persisted Z offsets and warnings for out-of-range conditions.
  - Added/reset handling and clearer warning flows in the UI.
  - Updated Z Home workflows and button labeling for clearer operation.

- **UI and workflow quality improvements**
  - Added update-check dialog improvements (including firmware/UI version checks and stream handling).
  - Added auto-load behavior for newly uploaded G-code files.
  - Improved Find Anchors visualization and trace persistence in the UI.

- **Release engineering updates**
  - Added and hardened automated release workflow tooling, including packaging and generated release-note support.

## Full changelog

- [Compare `v1.21` to `v1.22.0`](https://github.com/MaslowCNC/Maslow_4/compare/v1.21...v1.22.0)
