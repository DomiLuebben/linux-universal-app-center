#pragma once

namespace lut {

/// Ergebnis eines Paketvorgangs, wie Flatpak, Snap und AUR es an das
/// Fortschrittsfenster melden. Bewusst schlichte Ganzzahlen, damit die
/// Modelle nicht vom Fortschrittsfenster abhängen.
namespace OperationResult {
enum : int {
    Running = 0,
    Succeeded = 1,
    Failed = 2,
    Cancelled = 3,
};
}

} // namespace lut
