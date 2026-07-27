#include "OmarchyAdapter.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcOmarchy, "omachess.omarchy")

namespace {

// Omarchy 4 / Quattro is the only family whose theme state layout we know.
bool isRecognisedOmarchyVersion(const QString &version)
{
    const QString trimmed = version.trimmed();
    return trimmed.startsWith(QLatin1String("4."));
}

std::optional<QColor> parseHexColor(const QString &raw)
{
    const QString value = raw.trimmed();
    if (!value.startsWith(QLatin1Char('#')) || (value.size() != 7 && value.size() != 9))
        return std::nullopt;
    const QColor color(value);
    if (!color.isValid())
        return std::nullopt;
    return color;
}

QHash<QString, QString> parseSimpleToml(const QString &text)
{
    QHash<QString, QString> values;
    static const QRegularExpression linePattern(
        QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*\"([^\"]*)\"\\s*(?:#.*)?"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto match = linePattern.match(line);
        if (match.hasMatch())
            values.insert(match.captured(1), match.captured(2));
    }
    return values;
}

QColor requiredColor(const QHash<QString, QString> &values, const QString &key)
{
    const auto color = parseHexColor(values.value(key));
    return color.value_or(QColor());
}

QColor optionalColor(const QHash<QString, QString> &values, const QString &key,
                     const QColor &fallback)
{
    const auto color = parseHexColor(values.value(key));
    return color.value_or(fallback);
}

} // namespace

OmarchyAdapter::OmarchyAdapter(QObject *parent)
    : QObject(parent)
{
    // ThemeController calls reload() after connecting to the signals; the
    // constructor only arms the watcher so the first read is not lost.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &OmarchyAdapter::onStateChanged);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &OmarchyAdapter::onStateChanged);
}

QString OmarchyAdapter::versionFilePath() const
{
    // Tests point OMACHESS_OMARCHY_PREFIX at a fixture tree; production reads
    // the packaged Omarchy version file.
    const QString prefix = qEnvironmentVariable("OMACHESS_OMARCHY_PREFIX");
    if (!prefix.isEmpty())
        return QDir(prefix).filePath(QStringLiteral("version"));
    return QStringLiteral("/usr/share/omarchy/version");
}

QString OmarchyAdapter::stateRoot() const
{
    // Honour XDG_STATE_HOME so journey tests can inject theme state without
    // touching the developer's real Omarchy session. Production defaults to
    // ~/.local/state, which is where Quattro currently writes.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation))
        .filePath(QStringLiteral("omarchy"));
}

QString OmarchyAdapter::currentDir() const
{
    return QDir(stateRoot()).filePath(QStringLiteral("current"));
}

QString OmarchyAdapter::colorsFilePath() const
{
    return QDir(currentDir()).filePath(QStringLiteral("theme/colors.toml"));
}

QString OmarchyAdapter::themeNameFilePath() const
{
    return QDir(currentDir()).filePath(QStringLiteral("theme.name"));
}

bool OmarchyAdapter::detectVersion()
{
    QFile file(versionFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCInfo(lcOmarchy) << "no Omarchy version file at" << versionFilePath()
                          << "; using Built-in Palette";
        m_version.clear();
        m_recognised = false;
        return false;
    }

    m_version = QString::fromUtf8(file.readAll()).trimmed();
    m_recognised = isRecognisedOmarchyVersion(m_version);
    if (!m_recognised) {
        qCWarning(lcOmarchy) << "unrecognised Omarchy version" << m_version
                             << "; using Built-in Palette";
    }
    return m_recognised;
}

std::optional<OmarchyAdapter::Palette> OmarchyAdapter::readPalette() const
{
    QFile colors(colorsFilePath());
    if (!colors.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    const QHash<QString, QString> values = parseSimpleToml(QString::fromUtf8(colors.readAll()));

    const QString mode = values.value(QStringLiteral("mode")).trimmed();
    if (mode != QLatin1String("dark") && mode != QLatin1String("light"))
        return std::nullopt;

    const QColor background = requiredColor(values, QStringLiteral("background"));
    const QColor foreground = requiredColor(values, QStringLiteral("foreground"));
    const QColor accent = requiredColor(values, QStringLiteral("accent"));
    const QColor selection = requiredColor(values, QStringLiteral("selection"));
    const QColor muted = requiredColor(values, QStringLiteral("muted"));
    if (!background.isValid() || !foreground.isValid() || !accent.isValid()
        || !selection.isValid() || !muted.isValid()) {
        return std::nullopt;
    }

    Palette palette;
    palette.mode = mode;
    palette.background = background;
    palette.foreground = foreground;
    palette.accent = accent;
    palette.selection = selection;
    palette.muted = muted;
    palette.red = optionalColor(values, QStringLiteral("red"), accent);
    palette.yellow = optionalColor(values, QStringLiteral("yellow"), accent);
    palette.green = optionalColor(values, QStringLiteral("green"), accent);
    palette.orange = optionalColor(values, QStringLiteral("orange"), accent);
    palette.lighterBackground =
        optionalColor(values, QStringLiteral("lighter_background"), selection);
    palette.darkBackground =
        optionalColor(values, QStringLiteral("dark_background"), background);
    palette.darkerBackground =
        optionalColor(values, QStringLiteral("darker_background"), background);

    QFile nameFile(themeNameFilePath());
    if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
        palette.themeName = QString::fromUtf8(nameFile.readAll()).trimmed();

    return palette;
}

void OmarchyAdapter::watchCurrentDir()
{
    const QStringList watched = m_watcher.directories() + m_watcher.files();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    const QString current = currentDir();
    QDir().mkpath(current);
    if (!m_watcher.addPath(current))
        qCWarning(lcOmarchy) << "cannot watch" << current;

    // theme.name is rewritten on every swap; watching it covers replacements
    // that do not always emit a directory event immediately.
    const QString namePath = themeNameFilePath();
    if (QFile::exists(namePath))
        m_watcher.addPath(namePath);
}

void OmarchyAdapter::reload()
{
    if (!detectVersion()) {
        emit paletteUnavailable();
        return;
    }

    watchCurrentDir();

    if (const auto palette = readPalette()) {
        m_lastValid = palette;
        emit paletteReady(*palette);
        return;
    }

    emit paletteUnavailable();
}

void OmarchyAdapter::onStateChanged(const QString &path)
{
    Q_UNUSED(path);
    // Theme swaps replace the directory; re-arm the watch and retry briefly so
    // a mid-replace read does not spuriously fall back.
    watchCurrentDir();
    tryReadAfterChange(0);
}

void OmarchyAdapter::tryReadAfterChange(int attempt)
{
    if (!m_recognised)
        return;
    if (const auto palette = readPalette()) {
        m_lastValid = palette;
        emit paletteReady(*palette);
        return;
    }
    if (attempt < 4) {
        QTimer::singleShot(50, this, [this, attempt] { tryReadAfterChange(attempt + 1); });
        return;
    }
    // Still unreadable after retries: keep Last Valid at the controller, or
    // Built-in when none exists.
    emit paletteUnavailable();
}
