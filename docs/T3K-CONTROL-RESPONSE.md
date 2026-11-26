# Unified input selection and response controls

The overlay Controls section now has one Controller selector: Any gamepad
(last used), Mouse / trackball, and the connected gamepads supplied by SDL.
Saved disconnected gamepads remain listed. Mouse stays at a stable position in
the list and is never labelled as a disconnected controller. Other games keep
their shared gamepad-selector behavior.

Selecting Mouse / trackball changes the native controller identity and enables
relative mouse capture in gameplay. Selecting a gamepad returns to the native
analog path. Existing mode-change input suppression, spinner history reseeding,
overlay capture release and controller button bindings are retained. Mouse
buttons remain left fire, right jump and middle zapper; keyboard menu navigation
and gamepad overlay navigation remain available.

The separate Input device option is removed. On settings load, an old
`control_device=mouse` selection migrates to `controller=mouse`; the retired key
is reset so it cannot override later selections. New files omit that key.

**Stick response curve** (`t3k_stick_curve`, 1.0–5.0, default 2.5) is shown for
gamepads. Higher values give more stick travel to lower and middle movement
speeds. The power curve acts between the native decoder's deadzone and maximum
speed knees, rather than expanding the deadzone. Both stick axes are shaped;
buttons, D-pad and keyboard directions are unchanged.

**Sensitivity** (`t3k_input_gain`, 0.25–5.0, default 2.5) applies to the selected
input path. For analog input it scales active response after the curve; for
mouse input it scales relative X displacement before the spinner accumulates
fractions and applies its native wrap/safety limit. The curve does not affect
mouse input. These settings adjust input, not simulation time or game speed.

Defaults preserve every original signed stick input byte exactly. Nondefault
settings retain native quantization, deadzone, speed clamp and rotary limits;
they do not add unbounded native controller speed or change collision rules.

Validation: all four CTest targets pass. The control contract checks every
65,536 signed stick input values for exact default preservation and monotonic
curved response; tests also cover useful-range midpoint, full native speed,
gain saturation, invalid values, mouse displacement gain, controller list
stability, existing button mapping and distinct native device identities.
Level 9, analog level 15 and mouse scene-trial routes pass. An isolated old
mouse-settings file migrates correctly and completes a mouse route with saved
nondefault gain/curve values retained.

This is research-branch work; the previously frozen John handoff is unchanged.
