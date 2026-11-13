#pragma once

namespace Haki
{

// Dark slate, translucent editor look. Pane backgrounds use `opacity` (0.2-1) so the game shows through.
// Cheap: call again whenever the opacity changes.
void ApplyEditorTheme(float opacity);

} // namespace Haki
