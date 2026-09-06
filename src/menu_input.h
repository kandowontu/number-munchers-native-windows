#pragma once

namespace munchers {

// Win32 virtual-key values are repeated here so this small shared helper does
// not depend on a particular Windows-header include order.
inline bool applyFixedMenuNavigation(const unsigned int virtualKey,
                                     const int itemCount,
                                     int& zeroBasedSelection) {
    if (itemCount <= 0) return false;

    switch (virtualKey) {
    case 0x25: // VK_LEFT
    case 0x26: // VK_UP
        zeroBasedSelection = (zeroBasedSelection + itemCount - 1) % itemCount;
        return true;
    case 0x27: // VK_RIGHT
    case 0x28: // VK_DOWN
        zeroBasedSelection = (zeroBasedSelection + 1) % itemCount;
        return true;
    case 0x24: // VK_HOME
        zeroBasedSelection = 0;
        return true;
    case 0x23: // VK_END
        zeroBasedSelection = itemCount - 1;
        return true;
    default:
        return false;
    }
}

// The Yes/No wrapper intercepts the arrow and Y/N keys. Up/Down deliberately
// remain no-ops, while unlisted Home/End fall through to the common two-row
// widget and select its first/last item.
inline bool applyYesNoMenuSelection(const unsigned int virtualKey,
                                    int& zeroBasedSelection) {
    switch (virtualKey) {
    case 0x24: // VK_HOME
    case 0x25: // VK_LEFT
    case 'Y':
        zeroBasedSelection = 0;
        return true;
    case 0x23: // VK_END
    case 0x27: // VK_RIGHT
    case 'N':
        zeroBasedSelection = 1;
        return true;
    default:
        return false;
    }
}

// With flag 0x80 clear, the common DOS list widget treats the translated
// keypad-style characters 4/8 as previous and 2/6 as next. As with arrows,
// these characters only change selection and never activate an item.
inline bool applyUnnumberedMenuCharacterNavigation(const wchar_t character,
                                                    const int itemCount,
                                                    int& zeroBasedSelection) {
    switch (character) {
    case L'4':
    case L'8':
        return applyFixedMenuNavigation(0x25, itemCount, zeroBasedSelection);
    case L'2':
    case L'6':
        return applyFixedMenuNavigation(0x27, itemCount, zeroBasedSelection);
    default:
        return false;
    }
}

// The common DOS numbered-list widget enables this path with flag 0x80.
// Selection is one-based internally. When row 1 is already selected, another
// digit can extend it to rows 10-19; the largest original list has 11 rows.
// A digit changes the selection only. Enter remains the activation event.
inline bool applyNumberedMenuShortcut(const wchar_t character,
                                      const int itemCount,
                                      int& zeroBasedSelection) {
    if (character < L'0' || character > L'9') return false;

    int candidate = static_cast<int>(character - L'0');
    if (zeroBasedSelection == 0 && itemCount >= candidate + 10) {
        candidate += 10;
    }
    if (candidate >= 1 && candidate <= itemCount) {
        zeroBasedSelection = candidate - 1;
    }
    return true;
}

} // namespace munchers
