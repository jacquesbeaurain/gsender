#pragma once

// Plugin-supplied firmware response parsers (upstream's server/lib/plugin-parsers:
// specSchema.js, regexSafety.js and PluginParserChain.js).
//
// Every raw line from the board runs through each registered parser. A "line"
// parser matches one line; a "block" parser gathers the lines between a begin
// and an end (or ok/error) pattern. Only matches leave the chain, delivered to
// the plugin that owns the parser, so a plugin cannot turn the serial stream
// into a firehose: a parser is rate limited, and one whose matching is slow is
// quarantined. The chain only observes; it never consumes or alters a line.
//
// It also runs machine:query captures: the lines answering one command a
// plugin sends, until ok/error (or a pattern), a line count or a timeout.

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace gs::app {

/** Upstream's heuristic screen for catastrophic backtracking. Empty when acceptable, else why not. */
QString assessParserRegexRisk(const QString& source, const QString& flags = {});

/** A plugin's regex (a string, or { source, flags } with JavaScript flags), screened as above.
 *  Nothing, with the reason in `error`, when it is malformed or risky. */
std::optional<QRegularExpression> compilePluginRegex(const QJsonValue& value, QString* error = nullptr);

struct PluginParserProblem {
    QString id;
    QString message;
};

class PluginParserChain : public QObject {
    Q_OBJECT

public:
    /** Delivers a result to one plugin: topic "parser" (matches and parser errors) or "query". */
    using Deliver = std::function<void(const QString& pluginId, const QString& topic, const QJsonObject& data)>;

    static constexpr int MaxParsersPerPlugin = 16;
    static constexpr int MaxEmitsPerSecond = 20;

    PluginParserChain(Deliver deliver, std::function<bool()> machineIdle, QObject* parent = nullptr);
    ~PluginParserChain() override;

    /** Replaces a plugin's manifest-declared parsers (none removes them). Returns the rejected specs. */
    std::vector<PluginParserProblem> setManifestParsers(const QString& pluginId, const QJsonArray& specs);

    /** Adds parsers at run time; re-registering an id replaces it. */
    QJsonObject registerRuntime(const QString& pluginId, const QJsonArray& specs);
    /** Removes one runtime parser, or all of the plugin's when parserId is empty. */
    void unregisterRuntime(const QString& pluginId, const QString& parserId = {});
    /** Drops everything a plugin registered (disabled or unloaded). */
    void removePlugin(const QString& pluginId);

    /** One raw firmware line. Never throws. */
    void feed(const QString& line);
    /** The connection closed: flush open blocks and captures, drop runtime parsers. */
    void reset(const QString& reason = QStringLiteral("close"));

    struct QueryOptions {
        QString untilKind = QStringLiteral("ok-or-error");  // "ok", "error", "ok-or-error" or "" (regex)
        std::optional<QRegularExpression> untilPattern;
        int maxLines = 200;
        int timeoutMs = 5000;
        bool includeStatusReports = false;
    };
    using QueryDone = std::function<void(const QJsonObject& result)>;
    /** Opens the capture for a query's reply; false while another is in flight. */
    bool beginCapture(const QueryOptions& options, QueryDone done);
    bool captureActive() const noexcept { return capture_ != nullptr; }

    int parserCount(const QString& pluginId) const;

private:
    struct Parser;
    struct Capture;

    std::vector<PluginParserProblem> compile(const QString& pluginId, const QJsonArray& specs, bool manifest,
                                             std::vector<std::unique_ptr<Parser>>& out, QStringList* warnings);
    void feedLine(Parser& parser, const QString& line);
    void feedBlock(Parser& parser, const QString& line);
    bool isIgnored(const Parser& parser, const QString& line) const;
    bool isTerminator(const Parser& parser, const QString& line) const;
    void collectEntry(Parser& parser, const QString& line);
    void flushBlock(Parser& parser, bool complete, const QString& reason);
    void emitMatch(Parser& parser, QJsonObject payload);
    void chargeTime(Parser& parser, double elapsedMs);
    void quarantine(Parser& parser, const QString& message);
    void reportProblem(const QString& pluginId, const QString& parserId, const QString& reason, const QString& message);
    void finishCapture(bool complete, const QString& reason);
    void sweep();
    void updateSweepTimer();

    Deliver deliver_;
    std::function<bool()> machineIdle_;
    std::vector<std::unique_ptr<Parser>> parsers_;
    std::unique_ptr<Capture> capture_;
    QTimer sweepTimer_;
};

}  // namespace gs::app
