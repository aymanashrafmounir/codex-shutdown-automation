---
name: Codex Shutdown Automation
description: A native Windows utility with clear one-time shutdown permission.
colors:
  primary: "#206246"
  primary-hover: "#164c35"
  primary-soft: "#e6efe8"
  attention-bg: "#f5e9d6"
  attention-text: "#713e0a"
  attention-border: "#ddcbaa"
  error: "#a4362f"
  paper: "#f5f4ed"
  surface: "#fffef9"
  ink: "#182b27"
  muted: "#52645d"
  alternate-row: "#edf0e8"
  line: "#d8dfd6"
  disabled-text: "#748277"
  focus: "#39735a"
typography:
  state:
    fontFamily: Segoe UI
    fontSize: 32px
    fontWeight: 600
  title:
    fontFamily: Segoe UI
    fontSize: 20px
    fontWeight: 600
  description:
    fontFamily: Segoe UI
    fontSize: 14px
  body:
    fontFamily: Segoe UI
    fontSize: 10pt
rounded:
  control: 6px
  state: 10px
spacing:
  compact: 12px
  content: 16px
  section: 20px
  window: 24px
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.surface}"
    rounded: "{rounded.control}"
    padding: 5px 12px
  button-primary-hover:
    backgroundColor: "{colors.primary-hover}"
  button-primary-disabled:
    backgroundColor: "{colors.line}"
    textColor: "{colors.muted}"
  state-panel:
    backgroundColor: "{colors.surface}"
    rounded: "{rounded.state}"
    padding: 16px 20px
  state-panel-enabled:
    backgroundColor: "{colors.primary-soft}"
  state-panel-countdown:
    backgroundColor: "{colors.attention-bg}"
  simulation-banner:
    backgroundColor: "{colors.attention-bg}"
    textColor: "{colors.attention-text}"
    rounded: "{rounded.control}"
    padding: 10px
---

# Design System: Codex Shutdown Automation

## Overview

The approved office proof-sheet world carries into a native Windows Qt Widgets application: warm paper, ink, forest-green decisions and amber attention. Aligned data and restrained typography keep the state legible during everyday work. Standard Windows controls carry navigation, editing and window behavior.

The principal state remains unmistakable. Color supports the written OFF, waiting, idle-confirmation and countdown labels; it never replaces them. The application uses native widgets throughout, including tables, tabs, spin boxes, a language selector and a system-tray menu.

## Colors

Forest green marks the deliberate Enable action, selection and enabled waiting states. Amber identifies countdown, simulation and shutdown blockers. Error red marks a reported failure. The paper and surface neutrals distinguish the window and work area, while muted ink and the line color organize supporting information.

### Primary

The primary, primary-hover and primary-soft tokens provide action, hover and enabled-state treatments.

### Secondary

The attention tokens provide readable warning surfaces and borders. Error is reserved for reported failures; focus marks keyboard interaction.

### Neutral

Paper is the window background; surface is the data and disarmed-state background. Ink carries primary text. Muted carries supporting text, alternate-row separates table rows and headers, line defines boundaries, and disabled-text supports inactive native controls.

## Typography

Segoe UI is the Windows system typeface requested by the implementation. Qt supplies installed fallback glyphs for Arabic. The native window font uses points, while the explicit state, title and description styles use Qt stylesheet pixel sizes; keep those units as recorded in the frontmatter.

The state has the strongest emphasis, followed by the product title and then descriptive text. Labels and table content inherit the window font. Arabic changes the window to right-to-left layout without introducing a separate display face.

## Layout

The initial window is 1100 × 740 in Qt logical coordinates, with an 800 × 600 minimum. The outer content margins are 24 left/right, 20 top and 16 bottom. The main layout separates groups by 12. The state area uses 20 horizontal and 16 vertical padding.

Overview places the chats table beside a timing panel with a fixed width of 242. Tables scroll when the available width or height is insufficient; this is a desktop window with no web breakpoints or mobile stacking. The blocker list is bounded to a maximum height of 100 and remains scrollable. Table rows start at a height of 34. History uses a separate native tab. Right-to-left layout mirrors the containing widgets.

## Elevation & Depth

The application adds no decorative shadows. Thin borders and tonal surfaces separate controls, the state area, tables and the settings group. Windows and Qt retain their native window and control rendering outside these application-owned styles.

## Shapes

Gently rounded controls and notices use the control radius; the larger state area uses the state radius. Native spin boxes, selectors, tabs, scrollbars and menus retain their standard interaction geometry. The application icon is an authored power symbol drawn with QPainter, rather than an emoji or icon font.

## Components

### State and actions

The large text label states whether shutdown is OFF, waiting, confirming idle, counting down or requesting shutdown. Enable uses the primary button treatment. Cancel is a standard native button whose availability follows the current permission. Waiting and confirming-idle surfaces use primary-soft; countdown uses attention-bg. State and timer updates are immediate, with no authored animation.

### Inputs and keyboard focus

Timing uses native integer spin boxes, each constrained to 60–3600 seconds. Saving timing or language cancels current permission and displays localized saved feedback. Buttons, spin boxes and the language selector receive a two-pixel focus border. Tables and tabs retain Qt keyboard navigation and selection behavior.

### Tables, errors and blockers

Chats show title, state, local activity time and details. Pending work appears before completed work. History uses the store's newest-first order and shows local time, decision and details. Table items and text labels display data as plain text. Monitor and policy blockers remain visible even when the monitor is healthy; reported errors appear separately. Chat-cell tooltips expose text clipped by table columns.

### Tray and window lifecycle

The tray menu offers Show window, Cancel shutdown and Quit monitor. Closing the window hides it only when a system tray is available; otherwise the window remains reachable and shows recovery guidance. Quit cancels permission. The simulation notice explicitly says that the instance cannot shut down the computer.

## Do's and Don'ts

- **Do** use written states together with the established palette.
- **Do** preserve native keyboard behavior, scrolling, focus and Arabic layout direction.
- **Do** show monitoring errors and blockers where the user can inspect them.
- **Do** keep the simulation notice visible in a simulated instance.
- **Don't** restore the browser interface, web fonts or web breakpoints in the native application.
- **Don't** add decorative shadows, marketing panels or emoji controls.
- **Don't** hide the sole cancellation interface when no system tray is available.
