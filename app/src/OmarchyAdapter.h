#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <optional>

// The single place Omachess knows about Omarchy paths and Quattro Palette
// tokens. Everything else consumes the semantic result, never the files.
//
// Capability-first and version-gated: an unrecognised Omarchy version leaves
// the adapter inert so the Built-in Palette is used. A recognised version
// reads and watches the active theme state. Theme switching replaces the
// theme directory atomically, so the watcher sits on the containing `current`
// directory rather than on colors.toml alone.
class OmarchyAdapter : public QObject
{
    Q_OBJECT

public:
    // The tokens Omachess consumes from a Quattro Palette. Required keys must
    // be present and valid hex colours (mode is a string) for the palette to
    // count as structurally compatible.
    struct Palette {
        QString mode;
        QColor background;
        QColor foreground;
        QColor accent;
        QColor selection;
        QColor muted;
        QColor red;
        QColor yellow;
        QColor green;
        QColor orange;
        QColor lighterBackground;
        QColor darkBackground;
        QColor darkerBackground;
        QString themeName;
    };

    explicit OmarchyAdapter(QObject *parent = nullptr);

    // Whether this Omarchy build is one the adapter will try to read.
    bool recognised() const { return m_recognised; }
    QString detectedVersion() const { return m_version; }

    // The most recently accepted Quattro Palette, if any has loaded.
    std::optional<Palette> lastValid() const { return m_lastValid; }

    // Re-read version and palette state. Safe to call at any time; never
    // throws and never blocks startup on bad data.
    void reload();

signals:
    // Fired when a structurally valid Quattro Palette is accepted.
    void paletteReady(const OmarchyAdapter::Palette &palette);

    // Fired when the adapter cannot produce a palette (unrecognised version,
    // missing files, or malformed data) so the caller can fall back.
    void paletteUnavailable();

private:
    QString versionFilePath() const;
    QString stateRoot() const;
    QString currentDir() const;
    QString colorsFilePath() const;
    QString themeNameFilePath() const;

    bool detectVersion();
    std::optional<Palette> readPalette() const;
    void watchCurrentDir();

    void onStateChanged(const QString &path);
    void tryReadAfterChange(int attempt);

    QFileSystemWatcher m_watcher;
    bool m_recognised = false;
    QString m_version;
    std::optional<Palette> m_lastValid;
};
