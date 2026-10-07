import QtQuick

// A bordered box: upstream's `rounded-lg border` panels, cards, popups and
// field backgrounds (the colours and radius are the common ones; set what
// differs). The border is one device pixel, as a 1 CSS px border is in
// Chromium (Theme.hairline).
Rectangle {
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.color: Theme.border
    border.width: Theme.hairline
}