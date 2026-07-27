#include "ThemeController.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

ThemeController *g_theme = nullptr;

double relativeLuminance(const QColor &color)
{
    auto channel = [](double c) {
        c /= 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green())
        + 0.0722 * channel(color.blue());
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double l1 = relativeLuminance(a);
    const double l2 = relativeLuminance(b);
    const double lighter = std::max(l1, l2);
    const double darker = std::min(l1, l2);
    return (lighter + 0.05) / (darker + 0.05);
}

// Pick a contrasting square pair from palette entries without mutating them.
// Prefer familiar board-like pairs when several clear the ≥2:1 bar.
void selectBoardPair(const OmarchyAdapter::Palette &palette, QColor &light, QColor &dark)
{
    struct Candidate {
        QColor color;
        const char *role;
    };
    const Candidate candidates[] = {
        {palette.lighterBackground, "lighter_background"},
        {palette.selection, "selection"},
        {palette.muted, "muted"},
        {palette.background, "background"},
        {palette.darkBackground, "dark_background"},
        {palette.darkerBackground, "darker_background"},
        {palette.green, "green"},
        {palette.orange, "orange"},
        {palette.accent, "accent"},
    };

    // Preferred role pairings first — selected, not invented.
    const std::pair<const char *, const char *> preferred[] = {
        {"lighter_background", "background"},
        {"selection", "background"},
        {"lighter_background", "dark_background"},
        {"muted", "background"},
        {"selection", "dark_background"},
        {"green", "background"},
        {"orange", "background"},
    };

    auto findRole = [&](const char *role) -> QColor {
        for (const Candidate &candidate : candidates) {
            if (std::strcmp(candidate.role, role) == 0)
                return candidate.color;
        }
        return {};
    };

    for (const auto &pair : preferred) {
        const QColor a = findRole(pair.first);
        const QColor b = findRole(pair.second);
        if (!a.isValid() || !b.isValid() || a == b)
            continue;
        if (contrastRatio(a, b) >= 2.0) {
            if (relativeLuminance(a) >= relativeLuminance(b)) {
                light = a;
                dark = b;
            } else {
                light = b;
                dark = a;
            }
            return;
        }
    }

    // Fall through: best contrast among all distinct pairs.
    double best = 0.0;
    QColor bestLight = palette.lighterBackground;
    QColor bestDark = palette.background;
    for (const Candidate &left : candidates) {
        for (const Candidate &right : candidates) {
            if (left.color == right.color)
                continue;
            const double ratio = contrastRatio(left.color, right.color);
            if (ratio > best) {
                best = ratio;
                if (relativeLuminance(left.color) >= relativeLuminance(right.color)) {
                    bestLight = left.color;
                    bestDark = right.color;
                } else {
                    bestLight = right.color;
                    bestDark = left.color;
                }
            }
        }
    }
    light = bestLight;
    dark = bestDark;
}

} // namespace

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
    g_theme = this;
    connect(&m_adapter, &OmarchyAdapter::paletteReady, this, &ThemeController::onPaletteReady);
    connect(&m_adapter, &OmarchyAdapter::paletteUnavailable, this,
            &ThemeController::onPaletteUnavailable);

    // Start from the Built-in Palette so the first frame is never blank, then
    // let the adapter replace it when a Quattro Palette is ready.
    applyRoles(builtInRoles(), QStringLiteral("builtin"));
    m_adapter.reload();
}

ThemeController *ThemeController::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(scriptEngine);
    // One Theme for the process; QML singleton ownership stays with C++.
    auto *theme = new ThemeController(engine);
    QQmlEngine::setObjectOwnership(theme, QQmlEngine::CppOwnership);
    return theme;
}

ThemeController *ThemeController::instance()
{
    return g_theme;
}

QStringList ThemeController::boardThemeIds() const
{
    return {QStringLiteral("follow"), QStringLiteral("classic"), QStringLiteral("walnut"),
            QStringLiteral("slate")};
}

QStringList ThemeController::pieceSetIds() const
{
    return {QStringLiteral("cburnett")};
}

QString ThemeController::pieceSetPath() const
{
    return QStringLiteral("pieces/%1/").arg(m_pieceSetId);
}

void ThemeController::setBoardTheme(const QString &id)
{
    if (!boardThemeIds().contains(id) || id == m_boardThemeId)
        return;
    m_boardThemeId = id;
    refreshBoardFromPin();
    emit themeChanged();
}

void ThemeController::setPieceSet(const QString &id)
{
    if (!pieceSetIds().contains(id) || id == m_pieceSetId)
        return;
    m_pieceSetId = id;
    emit themeChanged();
}

ThemeController::Roles ThemeController::builtInRoles()
{
    Roles roles;
    roles.background = QColor(QStringLiteral("#1a1b26"));
    roles.foreground = QColor(QStringLiteral("#a9b1d6"));
    roles.accent = QColor(QStringLiteral("#7aa2f7"));
    roles.selection = QColor(QStringLiteral("#292e42"));
    roles.muted = QColor(QStringLiteral("#414868"));
    roles.panel = QColor(QStringLiteral("#24283b"));
    roles.danger = QColor(QStringLiteral("#f7768e"));
    roles.warning = QColor(QStringLiteral("#e0af68"));
    roles.success = QColor(QStringLiteral("#9ece6a"));
    // The classic green Board Theme Omachess owns independently of Quattro.
    roles.lightSquare = QColor(QStringLiteral("#ebecd0"));
    roles.darkSquare = QColor(QStringLiteral("#739552"));
    roles.lastMove = QColor(QStringLiteral("#f6f669"));
    roles.selectedSquare = QColor(QStringLiteral("#2b9fd8"));
    roles.moveTarget = QColor(QStringLiteral("#1c2b1c"));
    roles.themeName = QStringLiteral("builtin");
    return roles;
}

ThemeController::Roles ThemeController::rolesFromPalette(const OmarchyAdapter::Palette &palette)
{
    Roles roles;
    roles.background = palette.background;
    roles.foreground = palette.foreground;
    roles.accent = palette.accent;
    roles.selection = palette.selection;
    roles.muted = palette.muted;
    roles.panel = palette.lighterBackground;
    roles.danger = palette.red;
    roles.warning = palette.yellow;
    roles.success = palette.green;
    selectBoardPair(palette, roles.lightSquare, roles.darkSquare);
    roles.lastMove = palette.yellow;
    roles.selectedSquare = palette.accent;
    roles.moveTarget = palette.muted;
    roles.themeName = palette.themeName.isEmpty() ? QStringLiteral("quattro") : palette.themeName;
    return roles;
}

ThemeController::Roles ThemeController::pinnedBoardTheme(const QString &id, const Roles &chrome)
{
    Roles roles = chrome;
    if (id == QLatin1String("classic")) {
        roles.lightSquare = QColor(QStringLiteral("#ebecd0"));
        roles.darkSquare = QColor(QStringLiteral("#739552"));
        roles.lastMove = QColor(QStringLiteral("#f6f669"));
        roles.selectedSquare = QColor(QStringLiteral("#2b9fd8"));
        roles.moveTarget = QColor(QStringLiteral("#1c2b1c"));
    } else if (id == QLatin1String("walnut")) {
        roles.lightSquare = QColor(QStringLiteral("#f0d9b5"));
        roles.darkSquare = QColor(QStringLiteral("#b58863"));
        roles.lastMove = QColor(QStringLiteral("#cdd26a"));
        roles.selectedSquare = QColor(QStringLiteral("#829769"));
        roles.moveTarget = QColor(QStringLiteral("#5c4033"));
    } else if (id == QLatin1String("slate")) {
        roles.lightSquare = QColor(QStringLiteral("#c0c4cc"));
        roles.darkSquare = QColor(QStringLiteral("#6b7280"));
        roles.lastMove = QColor(QStringLiteral("#eab308"));
        roles.selectedSquare = QColor(QStringLiteral("#38bdf8"));
        roles.moveTarget = QColor(QStringLiteral("#1f2937"));
    }
    return roles;
}

void ThemeController::applyRoles(const Roles &roles, const QString &source)
{
    m_active = roles;
    m_paletteSource = source;
    m_themeName = roles.themeName;

    Roles painted = roles;
    if (boardThemePinned())
        painted = pinnedBoardTheme(m_boardThemeId, roles);

    m_background = painted.background;
    m_foreground = painted.foreground;
    m_accent = painted.accent;
    m_selection = painted.selection;
    m_muted = painted.muted;
    m_panel = painted.panel;
    m_danger = painted.danger;
    m_warning = painted.warning;
    m_success = painted.success;
    m_lightSquare = painted.lightSquare;
    m_darkSquare = painted.darkSquare;
    m_lastMove = painted.lastMove;
    m_selectedSquare = painted.selectedSquare;
    m_moveTarget = painted.moveTarget;
}

void ThemeController::refreshBoardFromPin()
{
    applyRoles(m_active, m_paletteSource);
}

void ThemeController::onPaletteReady(const OmarchyAdapter::Palette &palette)
{
    const Roles roles = rolesFromPalette(palette);
    m_lastValidRoles = roles;
    m_haveLastValid = true;
    applyRoles(roles, QStringLiteral("quattro"));
    emit themeChanged();
}

void ThemeController::onPaletteUnavailable()
{
    if (m_haveLastValid) {
        applyRoles(m_lastValidRoles, QStringLiteral("last_valid"));
    } else {
        applyRoles(builtInRoles(), QStringLiteral("builtin"));
    }
    emit themeChanged();
}
