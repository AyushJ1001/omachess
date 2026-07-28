#pragma once

#include "OmarchyAdapter.h"

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Omachess-owned visual roles derived from the Quattro Palette (or the
// Built-in Palette), plus Board Theme pinning and Piece Set selection.
//
// QML binds to these roles; it never reads Omarchy paths or colors.toml.
class ThemeController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    QML_NAMED_ELEMENT(Theme)

    // Chrome
    Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor selection READ selection NOTIFY themeChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY themeChanged)
    Q_PROPERTY(QColor panel READ panel NOTIFY themeChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)

    // Board Theme square and overlay colours
    Q_PROPERTY(QColor lightSquare READ lightSquare NOTIFY themeChanged)
    Q_PROPERTY(QColor darkSquare READ darkSquare NOTIFY themeChanged)
    Q_PROPERTY(QColor lastMove READ lastMove NOTIFY themeChanged)
    Q_PROPERTY(QColor selectedSquare READ selectedSquare NOTIFY themeChanged)
    Q_PROPERTY(QColor moveTarget READ moveTarget NOTIFY themeChanged)

    // The contrast the workspace is actually painting, as WCAG ratios by the
    // pair a player has to read: board squares, board marks, chrome text,
    // focus, selection, and status colours. Asserted across palettes.
    Q_PROPERTY(QVariantMap contrastReport READ contrastReport NOTIFY themeChanged)

    // Where the active colours came from: "quattro", "last_valid", or "builtin".
    Q_PROPERTY(QString paletteSource READ paletteSource NOTIFY themeChanged)
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)
    Q_PROPERTY(QString omarchyVersion READ omarchyVersion NOTIFY themeChanged)
    Q_PROPERTY(bool omarchyRecognised READ omarchyRecognised NOTIFY themeChanged)

    // Board Theme: "follow" tracks the Quattro Palette; any other id is a
    // pinned Omachess-owned set that ignores desktop theme changes.
    Q_PROPERTY(QString boardThemeId READ boardThemeId NOTIFY themeChanged)
    Q_PROPERTY(bool boardThemePinned READ boardThemePinned NOTIFY themeChanged)
    Q_PROPERTY(QStringList boardThemeIds READ boardThemeIds CONSTANT)

    // Piece Set selection is independent of any palette.
    Q_PROPERTY(QString pieceSetId READ pieceSetId NOTIFY themeChanged)
    Q_PROPERTY(QString pieceSetPath READ pieceSetPath NOTIFY themeChanged)
    Q_PROPERTY(QStringList pieceSetIds READ pieceSetIds CONSTANT)

public:
    explicit ThemeController(QObject *parent = nullptr);

    static ThemeController *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static ThemeController *instance();

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor accent() const { return m_accent; }
    QColor selection() const { return m_selection; }
    QColor muted() const { return m_muted; }
    QColor panel() const { return m_panel; }
    QColor danger() const { return m_danger; }
    QColor warning() const { return m_warning; }
    QColor success() const { return m_success; }

    QColor lightSquare() const { return m_lightSquare; }
    QColor darkSquare() const { return m_darkSquare; }
    QColor lastMove() const { return m_lastMove; }
    QColor selectedSquare() const { return m_selectedSquare; }
    QColor moveTarget() const { return m_moveTarget; }

    QVariantMap contrastReport() const;

    QString paletteSource() const { return m_paletteSource; }
    QString themeName() const { return m_themeName; }
    QString omarchyVersion() const { return m_adapter.detectedVersion(); }
    bool omarchyRecognised() const { return m_adapter.recognised(); }

    QString boardThemeId() const { return m_boardThemeId; }
    bool boardThemePinned() const { return m_boardThemeId != QLatin1String("follow"); }
    QStringList boardThemeIds() const;

    QString pieceSetId() const { return m_pieceSetId; }
    QString pieceSetPath() const;
    QStringList pieceSetIds() const;

    // Pin the Board Theme to an Omachess-owned set, or return to following
    // the desktop palette with id "follow".
    Q_INVOKABLE void setBoardTheme(const QString &id);

    // Choose the Piece Set. Independent of palette and Board Theme.
    Q_INVOKABLE void setPieceSet(const QString &id);

signals:
    void themeChanged();

private:
    struct Roles {
        QColor background;
        QColor foreground;
        QColor accent;
        QColor selection;
        QColor muted;
        QColor panel;
        QColor danger;
        QColor warning;
        QColor success;
        QColor lightSquare;
        QColor darkSquare;
        QColor lastMove;
        QColor selectedSquare;
        QColor moveTarget;
        QString themeName;
    };

    static Roles builtInRoles();
    static Roles rolesFromPalette(const OmarchyAdapter::Palette &palette);
    static Roles pinnedBoardTheme(const QString &id, const Roles &chrome);

    // Raise painted roles to the legibility bar v0.1 claims. A palette decides
    // the look; this decides that the look stays readable.
    static void enforceContrast(Roles &roles);

    void applyRoles(const Roles &roles, const QString &source);
    void refreshBoardFromPin();
    void onPaletteReady(const OmarchyAdapter::Palette &palette);
    void onPaletteUnavailable();

    OmarchyAdapter m_adapter;
    Roles m_active;
    Roles m_lastValidRoles;
    bool m_haveLastValid = false;

    QColor m_background;
    QColor m_foreground;
    QColor m_accent;
    QColor m_selection;
    QColor m_muted;
    QColor m_panel;
    QColor m_danger;
    QColor m_warning;
    QColor m_success;
    QColor m_lightSquare;
    QColor m_darkSquare;
    QColor m_lastMove;
    QColor m_selectedSquare;
    QColor m_moveTarget;

    QString m_paletteSource = QStringLiteral("builtin");
    QString m_themeName;
    QString m_boardThemeId = QStringLiteral("follow");
    QString m_pieceSetId = QStringLiteral("cburnett");
};
