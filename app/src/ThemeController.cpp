#include "ThemeController.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

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

// The colour a translucent mark actually paints over a square.
QColor composite(const QColor &mark, double opacity, const QColor &base)
{
    auto mix = [&](int markChannel, int baseChannel) {
        return static_cast<int>(std::round(markChannel * opacity + baseChannel * (1.0 - opacity)));
    };
    return QColor(mix(mark.red(), base.red()), mix(mark.green(), base.green()),
                  mix(mark.blue(), base.blue()));
}

// Search a colour's own lightness for the first shade that scores well enough,
// keeping hue and saturation so the palette's character survives the
// correction. `score` reads 1.0 when a shade is good enough, so every
// correction below is one scoring rule plus this one search.
QColor correctLightness(const QColor &color, const std::function<double(const QColor &)> &score)
{
    if (!color.isValid() || score(color) >= 1.0)
        return color;

    float hue = 0;
    float saturation = 0;
    float lightness = 0;
    color.getHslF(&hue, &saturation, &lightness);
    const float safeHue = hue < 0 ? 0 : hue;

    QColor best = color;
    double bestScore = score(color);
    for (int step = 1; step <= 100; ++step) {
        for (const float shade : {std::max(0.0f, lightness - step / 100.0f),
                                  std::min(1.0f, lightness + step / 100.0f)}) {
            const QColor candidate = QColor::fromHslF(safeHue, saturation, shade);
            const double candidateScore = score(candidate);
            if (candidateScore > bestScore) {
                bestScore = candidateScore;
                best = candidate;
            }
            if (bestScore >= 1.0)
                return best;
        }
    }
    return best;
}

// Text, or any colour that has to be read against one surface.
QColor legibleAgainst(const QColor &color, const QColor &against, double minimum)
{
    if (!against.isValid())
        return color;
    return correctLightness(color, [&](const QColor &candidate) {
        return contrastRatio(candidate, against) / minimum;
    });
}

// A translucent board mark has to change the square it lands on, on both
// square colours.
QColor visibleMark(const QColor &mark, double opacity, const QColor &light, const QColor &dark,
                   double minimum)
{
    return correctLightness(mark, [&](const QColor &candidate) {
        return std::min(contrastRatio(composite(candidate, opacity, light), light),
                        contrastRatio(composite(candidate, opacity, dark), dark))
            / minimum;
    });
}

// A panel is a surface with two jobs: it carries body text, and it reads as a
// distinct surface against the window behind it. Neither correction is allowed
// to undo the other, so one search answers both.
QColor surfaceFor(const QColor &panel, const QColor &text, const QColor &background,
                  double textMinimum, double surfaceMinimum)
{
    return correctLightness(panel, [&](const QColor &candidate) {
        return std::min(contrastRatio(candidate, text) / textMinimum,
                        contrastRatio(candidate, background) / surfaceMinimum);
    });
}

// The opacities Square.qml paints its marks with, so the contrast the report
// states is the contrast a player sees.
constexpr double lastMoveOpacity = 0.42;
constexpr double selectedOpacity = 0.45;
constexpr double moveTargetOpacity = 0.32;

// The bar v0.1 claims: body text, secondary text and status colour, board
// squares, and a mark that is visible on either square.
constexpr double textMinimum = 4.5;
constexpr double secondaryMinimum = 3.0;
constexpr double squareMinimum = 2.0;
constexpr double markMinimum = 1.2;
constexpr double surfaceMinimum = 1.1;

} // namespace

void ThemeController::enforceContrast(Roles &roles)
{
    roles.foreground = legibleAgainst(roles.foreground, roles.background, textMinimum);
    // Panels carry the same body text, so the panel moves rather than the text:
    // a palette's foreground stays the colour the desktop asked for.
    roles.panel = surfaceFor(roles.panel, roles.foreground, roles.background, textMinimum,
                             surfaceMinimum);
    roles.selection = legibleAgainst(roles.selection, roles.foreground, textMinimum);
    roles.muted = legibleAgainst(roles.muted, roles.panel, secondaryMinimum);
    roles.accent = legibleAgainst(roles.accent, roles.panel, secondaryMinimum);
    roles.accent = legibleAgainst(roles.accent, roles.background, secondaryMinimum);
    roles.danger = legibleAgainst(roles.danger, roles.panel, secondaryMinimum);
    roles.warning = legibleAgainst(roles.warning, roles.panel, secondaryMinimum);
    roles.success = legibleAgainst(roles.success, roles.panel, secondaryMinimum);

    roles.darkSquare = legibleAgainst(roles.darkSquare, roles.lightSquare, squareMinimum);
    roles.lastMove = visibleMark(roles.lastMove, lastMoveOpacity, roles.lightSquare,
                                 roles.darkSquare, markMinimum);
    roles.selectedSquare = visibleMark(roles.selectedSquare, selectedOpacity, roles.lightSquare,
                                       roles.darkSquare, markMinimum);
    roles.moveTarget = visibleMark(roles.moveTarget, moveTargetOpacity, roles.lightSquare,
                                   roles.darkSquare, markMinimum);
}

QVariantMap ThemeController::contrastReport() const
{
    auto markRatio = [](const QColor &mark, double opacity, const QColor &base) {
        return contrastRatio(composite(mark, opacity, base), base);
    };
    auto weakestMark = [&](const QColor &mark, double opacity) {
        return std::min(markRatio(mark, opacity, m_lightSquare),
                        markRatio(mark, opacity, m_darkSquare));
    };

    return QVariantMap{
        {QStringLiteral("board_squares"), contrastRatio(m_lightSquare, m_darkSquare)},
        {QStringLiteral("chrome_text"), contrastRatio(m_foreground, m_background)},
        {QStringLiteral("panel_text"), contrastRatio(m_foreground, m_panel)},
        {QStringLiteral("selection_text"), contrastRatio(m_foreground, m_selection)},
        {QStringLiteral("muted_text"), contrastRatio(m_muted, m_panel)},
        {QStringLiteral("panel_surface"), contrastRatio(m_panel, m_background)},
        {QStringLiteral("focus_ring"), std::min(contrastRatio(m_accent, m_background),
                                                contrastRatio(m_accent, m_panel))},
        {QStringLiteral("status_danger"), contrastRatio(m_danger, m_panel)},
        {QStringLiteral("status_warning"), contrastRatio(m_warning, m_panel)},
        {QStringLiteral("status_success"), contrastRatio(m_success, m_panel)},
        {QStringLiteral("last_move_mark"), weakestMark(m_lastMove, lastMoveOpacity)},
        {QStringLiteral("selected_mark"), weakestMark(m_selectedSquare, selectedOpacity)},
        {QStringLiteral("move_target_mark"), weakestMark(m_moveTarget, moveTargetOpacity)},
    };
}

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
    enforceContrast(painted);

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
