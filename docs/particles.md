# Particles

`Curve` evaluates cubic Hermite segments, with independent incoming and outgoing derivatives.
`Curve::Linear(start, end)` makes a linear ramp over 0–1. `Gradient` interpolates linear HDR RGBA
colors, including alpha. Both clamp outside the keys and use identity values (1 or white) when empty.

Use `SetKeys()` to author keys. Times are sorted; the last duplicate wins and nonfinite keys are
removed. Evaluation allocates nothing and locates its segment with binary search. Keys can use any
time domain; particles use normalized age.

Archive fields save these types as nested `keys` arrays. The inspector can add/remove keys and edit
times, values, tangents and HDR colors with the normal undo and prefab override support.
