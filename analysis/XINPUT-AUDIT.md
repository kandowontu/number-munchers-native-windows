# XInput support audit

Version 1.1 adds native support for Xbox-compatible controllers while retaining
the existing WinMM joystick path.

## Runtime boundary

`src/xinput_controller.cpp` loads `XInputGetState` at runtime, trying the
Windows-provided `xinput1_4.dll`, `xinput1_3.dll`, and `xinput9_1_0.dll` variants
in that order. No XInput DLL is bundled, and the executable does not acquire a
new static import. The production artifact test continues to enforce the
Windows-system-DLL-only boundary.

The first connected XInput controller is used. When no XInput controller is
connected, polling falls back to the pre-existing WinMM joystick path.

## Mapping

| Controller input | Native action |
|---|---|
| D-pad or left stick | Arrow-key movement |
| A or X | Space / munch |
| View/Back (Select) or B | Escape / back |
| Menu (Start) or Y | Enter / pause / confirm |

The D-pad takes priority over the stick. Stick input inside the standard XInput
left-thumb dead zone is ignored, and diagonal stick input resolves to the axis
with the larger magnitude. Direction changes fire immediately, then repeat
after 280 ms at 100 ms intervals. Action buttons are rising-edge triggered.

## Automated evidence

`xinput_controller_test` verifies:

- dead-zone suppression;
- D-pad priority and held-direction repeat timing;
- cardinal selection from the dominant stick axis;
- the primary A, Back, and Start mappings;
- the secondary X, B, and Y mappings;
- action-button edge suppression; and
- clean mapper reset across disconnect/reconnect.

The complete seven-test release suite passes, including the isolated production
artifact resource and import audit.
