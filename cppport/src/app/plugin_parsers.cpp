#include "plugin_parsers.hpp"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace gs::app {

namespace {

constexpr int kSweepIntervalMs = 250;
// A single match that slow on a short serial line is pathological.
constexpr double kParserKillMs = 50;
// Repeatedly slow-but-not-fatal matches also earn a quarantine.
constexpr double kParserSlowMs = 2;
constexpr int kParserSlowLimit = 20;
constexpr int kMaxRegexSource = 512;

const QRegularExpression& statusReportPattern() {
    static const QRegularExpression re(QStringLiteral("^<[^>]*>$"));
    return re;
}
const QRegularExpression& okPattern() {
    static const QRegularExpression re(QStringLiteral("^ok$"));
    return re;
}
const QRegularExpression& errorPattern() {
    static const QRegularExpression re(QStringLiteral("^error:\\s*\\d+"));
    return re;
}

bool matches(const QRegularExpression& re, const QString& line) {
    return re.match(line).hasMatch();
}

qint64 nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

// ---- regex screen (regexSafety.js) --------------------------------------------------

bool isUnbounded(const QString& source, qsizetype i) {
    if (i >= source.size()) return false;
    const QChar ch = source[i];
    if (ch == u'*' || ch == u'+') return true;
    if (ch == u'{') {
        const qsizetype close = source.indexOf(u'}', i);
        if (close < 0) return false;
        static const QRegularExpression openEnded(QStringLiteral("^\\d+,\\s*$"));
        return openEnded.match(source.mid(i + 1, close - i - 1)).hasMatch();
    }
    return false;
}

// One atom from i (an escape, a class, a group or a character); the index past it.
qsizetype skipAtom(const QString& source, qsizetype i) {
    if (i >= source.size()) return source.size();
    const QChar ch = source[i];
    if (ch == u'\\') return i + 2;
    if (ch == u'[') {
        qsizetype j = i + 1;
        while (j < source.size() && source[j] != u']') j += source[j] == u'\\' ? 2 : 1;
        return j + 1;
    }
    if (ch == u'(') {
        int depth = 0;
        for (qsizetype j = i; j < source.size();) {
            const QChar c = source[j];
            if (c == u'\\') {
                j += 2;
                continue;
            }
            if (c == u'(') {
                ++depth;
            } else if (c == u')' && --depth == 0) {
                return j + 1;
            }
            ++j;
        }
        return source.size();
    }
    return i + 1;
}

QString stripGroupPrefix(const QString& body) {
    static const QRegularExpression prefix(QStringLiteral("^\\?(?::|=|!|<=|<!|<[A-Za-z_$][\\w$]*>)"));
    QString out = body;
    return out.remove(prefix);
}

// Star height >= 2: (a+)+, ([a-z]+)*, (.*)*, (\s*\w+)+.
bool hasNestedUnboundedQuantifier(const QString& source) {
    for (qsizetype i = 0; i < source.size();) {
        const QChar ch = source[i];
        if (ch == u'\\') {
            i += 2;
            continue;
        }
        if (ch == u'[') {
            i = skipAtom(source, i);
            continue;
        }
        if (ch != u'(') {
            ++i;
            continue;
        }
        const qsizetype end = skipAtom(source, i);
        if (isUnbounded(source, end) && end - i >= 2) {
            const QString body = stripGroupPrefix(source.mid(i + 1, end - i - 2));
            if (!body.isEmpty() && isUnbounded(body, skipAtom(body, 0))) {
                return true;
            }
        }
        ++i;  // look inside the group too
    }
    return false;
}

// (a|a)*: branches that can start alike under an unbounded quantifier.
bool hasQuantifiedOverlappingAlternation(const QString& source) {
    for (qsizetype i = 0; i < source.size();) {
        if (source[i] == u'\\') {
            i += 2;
            continue;
        }
        if (source[i] != u'(') {
            ++i;
            continue;
        }
        int depth = 0;
        qsizetype j = i;
        for (; j < source.size(); ++j) {
            const QChar c = source[j];
            if (c == u'\\') {
                ++j;
                continue;
            }
            if (c == u'(') {
                ++depth;
            } else if (c == u')' && --depth == 0) {
                break;
            }
        }
        if (j < source.size() && isUnbounded(source, j + 1)) {
            static const QRegularExpression prefix(QStringLiteral("^\\?[:=!<][a-zA-Z]*>?"));
            QString body = source.mid(i + 1, j - i - 1);
            body.remove(prefix);
            if (body.contains(u'|')) {
                QSet<QString> seen;
                for (const QString& branch : body.split(u'|')) {
                    const QString first = branch.startsWith(u'\\') ? branch.left(2) : branch.left(1);
                    if (!first.isEmpty() && seen.contains(first)) return true;
                    seen.insert(first);
                }
            }
        }
        ++i;
    }
    return false;
}

bool hasHugeBoundedRepetition(const QString& source) {
    static const QRegularExpression counts(QStringLiteral("\\{(\\d+)(?:,(\\d+))?\\}"));
    for (auto it = counts.globalMatch(source); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString upper = m.captured(2).isEmpty() ? m.captured(1) : m.captured(2);
        if (upper.size() > 4 || upper.toInt() > 1000) return true;
    }
    return false;
}

QRegularExpression::PatternOptions optionsFor(const QString& flags) {
    QRegularExpression::PatternOptions options;
    if (flags.contains(u'i')) options |= QRegularExpression::CaseInsensitiveOption;
    if (flags.contains(u'm')) options |= QRegularExpression::MultilineOption;
    if (flags.contains(u's')) options |= QRegularExpression::DotMatchesEverythingOption;
    if (flags.contains(u'u')) options |= QRegularExpression::UseUnicodePropertiesOption;
    return options;
}

// ---- results ------------------------------------------------------------------------

QJsonObject namedGroups(const QRegularExpression& re, const QRegularExpressionMatch& m) {
    QJsonObject groups;
    const QStringList names = re.namedCaptureGroups();
    for (int i = 1; i < names.size(); ++i) {
        if (!names[i].isEmpty() && m.hasCaptured(i)) groups.insert(names[i], m.captured(i));
    }
    return groups;
}

QJsonArray numberedGroups(const QRegularExpressionMatch& m) {
    QJsonArray captures;
    for (int i = 1; i <= m.lastCapturedIndex(); ++i) {
        captures.append(m.hasCaptured(i) ? QJsonValue(m.captured(i)) : QJsonValue(QJsonValue::Null));
    }
    // Groups after the last one that took part did not match either.
    for (int i = m.lastCapturedIndex() + 1; i < m.regularExpression().captureCount() + 1; ++i) {
        captures.append(QJsonValue(QJsonValue::Null));
    }
    return captures;
}

}  // namespace

QString assessParserRegexRisk(const QString& source, const QString& flags) {
    if (source.isEmpty()) return QStringLiteral("pattern is empty");
    if (source.size() > kMaxRegexSource) {
        return QStringLiteral("pattern is longer than %1 characters").arg(kMaxRegexSource);
    }
    static const QRegularExpression allowedFlags(QStringLiteral("^[imsu]*$"));
    if (!allowedFlags.match(QString(flags).remove(u'g').remove(u'y')).hasMatch()) {
        return QStringLiteral("unsupported regex flags \"%1\"").arg(flags);
    }
    if (hasNestedUnboundedQuantifier(source)) {
        return QStringLiteral("nested unbounded quantifier (e.g. (a+)+) - this backtracks catastrophically");
    }
    // Adjacent unbounded quantifiers over overlapping classes (\s*\w+) are only
    // quadratic on a short line; grblHAL's own parsers use them.
    if (hasQuantifiedOverlappingAlternation(source)) {
        return QStringLiteral("quantified alternation with overlapping branches (e.g. (a|a)*)");
    }
    if (hasHugeBoundedRepetition(source)) return QStringLiteral("repetition count above 1000");
    const QRegularExpression re(source, optionsFor(flags));
    if (!re.isValid()) return QStringLiteral("invalid regex: %1").arg(re.errorString());
    return {};
}

std::optional<QRegularExpression> compilePluginRegex(const QJsonValue& value, QString* error) {
    QString source, flags;
    if (value.isString()) {
        source = value.toString();
    } else if (value.isObject() && value.toObject().value(QStringLiteral("source")).isString()) {
        source = value.toObject().value(QStringLiteral("source")).toString();
        flags = value.toObject().value(QStringLiteral("flags")).toString();
    } else {
        if (error) *error = QStringLiteral("must be a string or { source, flags }");
        return std::nullopt;
    }
    flags.remove(u'g').remove(u'y');
    const QString risk = assessParserRegexRisk(source, flags);
    if (!risk.isEmpty()) {
        if (error) *error = risk;
        return std::nullopt;
    }
    QRegularExpression re(source, optionsFor(flags));
    re.optimize();
    return re;
}

// ---- parsers ------------------------------------------------------------------------

struct PluginParserChain::Parser {
    QString pluginId;
    QString parserId;
    bool manifest = false;
    QString mode;  // "line" or "block"
    std::optional<QRegularExpression> match, begin, end, ignore;
    QString untilKind;  // "", "ok", "error", "ok-or-error"
    bool onlyWhenIdle = false;
    QString label;
    int maxLines = 64;
    int timeoutMs = 2000;
    bool strict = false;
    bool restartOnBegin = false;
    bool emitPartial = true;
    bool ignoreStatusReports = true;

    struct Block {
        QJsonArray lines;
        QJsonArray entries;
        QJsonObject groups;
        QJsonArray captures;
        qint64 startedAt = 0;
    };
    std::optional<Block> block;
    bool quarantined = false;
    int slowHits = 0;
    qint64 emitWindowStart = 0;
    int emitCount = 0;
    bool rateLimitReported = false;
    qint64 seq = 0;
};

struct PluginParserChain::Capture {
    QueryOptions options;
    QueryDone done;
    QJsonArray lines;
    qint64 startedAt = 0;
};

PluginParserChain::PluginParserChain(Deliver deliver, std::function<bool()> machineIdle, QObject* parent)
    : QObject(parent), deliver_(std::move(deliver)), machineIdle_(std::move(machineIdle)) {
    sweepTimer_.setInterval(kSweepIntervalMs);
    connect(&sweepTimer_, &QTimer::timeout, this, &PluginParserChain::sweep);
}

PluginParserChain::~PluginParserChain() = default;

std::vector<PluginParserProblem> PluginParserChain::compile(const QString& pluginId, const QJsonArray& specs, bool manifest,
                                                            std::vector<std::unique_ptr<Parser>>& out,
                                                            QStringList* warnings) {
    std::vector<PluginParserProblem> problems;
    static const QRegularExpression idPattern(QStringLiteral("^[a-zA-Z0-9._-]{1,64}$"));
    QSet<QString> seen;
    int index = 0;
    for (const QJsonValue& value : specs) {
        const QJsonObject raw = value.toObject();
        const QString label = raw.value(QStringLiteral("id")).isString() ? raw.value(QStringLiteral("id")).toString()
                                                                         : QStringLiteral("#%1").arg(index);
        ++index;
        if (static_cast<int>(out.size()) >= MaxParsersPerPlugin) {
            problems.push_back({label, QStringLiteral("exceeds the limit of %1 parsers per plugin").arg(MaxParsersPerPlugin)});
            continue;
        }
        if (!value.isObject()) {
            problems.push_back({label, QStringLiteral("parser spec must be an object")});
            continue;
        }
        const QString parserId = raw.value(QStringLiteral("id")).toString();
        if (!idPattern.match(parserId).hasMatch()) {
            problems.push_back({label, QStringLiteral("\"id\" must be 1-64 letters, digits, '.', '_' or '-'")});
            continue;
        }

        QStringList errors;
        auto parser = std::make_unique<Parser>();
        parser->pluginId = pluginId;
        parser->parserId = parserId;
        parser->manifest = manifest;
        parser->mode = raw.value(QStringLiteral("mode")).toString(QStringLiteral("line"));
        if (parser->mode != u"line" && parser->mode != u"block") {
            errors << QStringLiteral("\"mode\" must be one of: line, block");
        }
        const QString when = raw.value(QStringLiteral("whenWorkflow")).toString(QStringLiteral("any"));
        if (when != u"any" && when != u"idle") errors << QStringLiteral("\"whenWorkflow\" must be one of: any, idle");
        parser->onlyWhenIdle = when == u"idle";
        if (raw.contains(QStringLiteral("until"))) {
            parser->untilKind = raw.value(QStringLiteral("until")).toString();
            if (parser->untilKind != u"ok" && parser->untilKind != u"error" && parser->untilKind != u"ok-or-error") {
                errors << QStringLiteral("\"until\" must be one of: ok, error, ok-or-error");
                parser->untilKind.clear();
            }
        }

        const auto regexField = [&](const char* field, std::optional<QRegularExpression>& target) {
            const QJsonValue v = raw.value(QLatin1String(field));
            if (v.isUndefined() || v.isNull()) return;
            QString source, flags;
            if (v.isString()) {
                source = v.toString();
            } else if (v.isObject() && v.toObject().value(QStringLiteral("source")).isString()) {
                source = v.toObject().value(QStringLiteral("source")).toString();
                flags = v.toObject().value(QStringLiteral("flags")).toString();
            } else {
                errors << QStringLiteral("\"%1\" must be a string or { source, flags }").arg(QLatin1String(field));
                return;
            }
            // g and y carry state between lines in JavaScript; they mean nothing here.
            if (flags.contains(u'g') || flags.contains(u'y')) {
                if (warnings) *warnings << QStringLiteral("%1/%2: the g/y flags on \"%3\" were ignored").arg(pluginId, parserId, QLatin1String(field));
                flags.remove(u'g').remove(u'y');
            }
            const QString risk = assessParserRegexRisk(source, flags);
            if (!risk.isEmpty()) {
                errors << QStringLiteral("\"%1\" rejected: %2").arg(QLatin1String(field), risk);
                return;
            }
            target = QRegularExpression(source, optionsFor(flags));
            target->optimize();
        };
        regexField("match", parser->match);
        regexField("begin", parser->begin);
        regexField("end", parser->end);
        regexField("ignore", parser->ignore);

        const auto supplied = [&](const char* field) {
            const QJsonValue v = raw.value(QLatin1String(field));
            return !v.isUndefined() && !v.isNull();
        };
        if (parser->mode == u"line" && !parser->match && !supplied("match")) {
            errors << QStringLiteral("a \"line\" parser needs a \"match\" pattern");
        }
        if (parser->mode == u"block") {
            if (!parser->begin && !supplied("begin")) errors << QStringLiteral("a \"block\" parser needs a \"begin\" pattern");
            if (!parser->end && parser->untilKind.isEmpty() && !supplied("end")) {
                errors << QStringLiteral("a \"block\" parser needs either an \"end\" pattern or \"until\"");
            }
        }
        if (!parser->untilKind.isEmpty() && !parser->onlyWhenIdle && warnings) {
            *warnings << QStringLiteral("%1/%2: \"until: %3\" can end early while a job runs; prefer \"end\" or \"whenWorkflow\": \"idle\"")
                             .arg(pluginId, parserId, parser->untilKind);
        }
        if (seen.contains(parserId)) errors << QStringLiteral("duplicate parser id");

        if (!errors.isEmpty()) {
            for (const QString& e : errors) problems.push_back({label, e});
            continue;
        }

        const auto number = [&](const char* field, int fallback, int min, int max) {
            const QJsonValue v = raw.value(QLatin1String(field));
            if (!v.isDouble() || !std::isfinite(v.toDouble())) return fallback;
            return static_cast<int>(std::clamp(v.toDouble(), double(min), double(max)));
        };
        parser->label = raw.value(QStringLiteral("label")).toString(parserId);
        parser->maxLines = number("maxLines", 64, 1, 500);
        parser->timeoutMs = number("timeout", 2000, 100, 30000);
        parser->strict = raw.value(QStringLiteral("strict")).toBool(false);
        parser->restartOnBegin = raw.value(QStringLiteral("restartOnBegin")).toBool(false);
        parser->emitPartial = raw.value(QStringLiteral("emitPartial")).toBool(true);
        parser->ignoreStatusReports = raw.value(QStringLiteral("ignoreStatusReports")).toBool(true);
        seen.insert(parserId);
        out.push_back(std::move(parser));
    }
    return problems;
}

std::vector<PluginParserProblem> PluginParserChain::setManifestParsers(const QString& pluginId, const QJsonArray& specs) {
    for (auto& parser : parsers_) {
        if (parser->pluginId == pluginId && parser->manifest) flushBlock(*parser, false, QStringLiteral("reload"));
    }
    std::erase_if(parsers_, [&](const auto& p) { return p->pluginId == pluginId && p->manifest; });

    std::vector<std::unique_ptr<Parser>> compiled;
    QStringList warnings;
    const auto problems = compile(pluginId, specs, true, compiled, &warnings);
    for (const QString& w : warnings) qWarning().noquote() << "plugin parser" << w;
    for (const auto& problem : problems) {
        reportProblem(pluginId, problem.id, QStringLiteral("invalid-spec"), problem.message);
    }
    for (auto& parser : compiled) parsers_.push_back(std::move(parser));
    updateSweepTimer();
    return problems;
}

QJsonObject PluginParserChain::registerRuntime(const QString& pluginId, const QJsonArray& specs) {
    std::vector<std::unique_ptr<Parser>> compiled;
    QStringList warnings;
    const auto problems = compile(pluginId, specs, false, compiled, &warnings);

    // The plugin's manifest and runtime parsers together stay under the cap.
    QJsonArray registered;
    QJsonArray errors;
    for (auto& parser : compiled) {
        const QString id = parser->parserId;
        for (auto& existing : parsers_) {
            if (existing->pluginId == pluginId && !existing->manifest && existing->parserId == id) {
                flushBlock(*existing, false, QStringLiteral("reload"));
            }
        }
        std::erase_if(parsers_, [&](const auto& p) { return p->pluginId == pluginId && !p->manifest && p->parserId == id; });
        if (parserCount(pluginId) >= MaxParsersPerPlugin) {
            errors.append(QJsonObject{{"id", id}, {"error", QStringLiteral("exceeds the limit of %1 parsers per plugin").arg(MaxParsersPerPlugin)}});
            continue;
        }
        registered.append(id);
        parsers_.push_back(std::move(parser));
    }
    for (const auto& problem : problems) errors.append(QJsonObject{{"id", problem.id}, {"error", problem.message}});
    QJsonArray warningArray;
    for (const QString& w : warnings) warningArray.append(w);
    updateSweepTimer();
    return QJsonObject{{"registered", registered}, {"errors", errors}, {"warnings", warningArray}};
}

void PluginParserChain::unregisterRuntime(const QString& pluginId, const QString& parserId) {
    const auto selected = [&](const Parser& p) {
        return p.pluginId == pluginId && !p.manifest && (parserId.isEmpty() || p.parserId == parserId);
    };
    for (auto& parser : parsers_) {
        if (selected(*parser)) flushBlock(*parser, false, QStringLiteral("close"));
    }
    std::erase_if(parsers_, [&](const auto& p) { return selected(*p); });
    updateSweepTimer();
}

void PluginParserChain::removePlugin(const QString& pluginId) {
    std::erase_if(parsers_, [&](const auto& p) { return p->pluginId == pluginId; });
    updateSweepTimer();
}

int PluginParserChain::parserCount(const QString& pluginId) const {
    return static_cast<int>(std::count_if(parsers_.begin(), parsers_.end(), [&](const auto& p) { return p->pluginId == pluginId; }));
}

void PluginParserChain::feed(const QString& line) {
    try {
        sweep();
        for (auto& parser : parsers_) {
            if (parser->quarantined) continue;
            if (parser->onlyWhenIdle && machineIdle_ && !machineIdle_()) continue;
            QElapsedTimer timer;
            timer.start();
            if (parser->mode == u"line") {
                feedLine(*parser, line);
            } else {
                feedBlock(*parser, line);
            }
            chargeTime(*parser, static_cast<double>(timer.nsecsElapsed()) / 1e6);
        }
        if (capture_) {
            Capture& c = *capture_;
            if (!c.options.includeStatusReports && matches(statusReportPattern(), line)) return;
            c.lines.append(line);
            bool until = false;
            if (c.options.untilPattern) {
                until = matches(*c.options.untilPattern, line);
            } else if (c.options.untilKind == u"ok") {
                until = matches(okPattern(), line);
            } else if (c.options.untilKind == u"error") {
                until = matches(errorPattern(), line);
            } else {
                until = matches(okPattern(), line) || matches(errorPattern(), line);
            }
            if (until) {
                finishCapture(true, QStringLiteral("until"));
            } else if (c.lines.size() >= c.options.maxLines) {
                finishCapture(false, QStringLiteral("maxLines"));
            }
        }
    } catch (const std::exception& ex) {
        qWarning().noquote() << "plugin parser chain failed on" << line << ex.what();
    }
}

void PluginParserChain::chargeTime(Parser& parser, double elapsedMs) {
    if (elapsedMs > kParserKillMs) {
        quarantine(parser, QStringLiteral("a single match took %1 ms").arg(elapsedMs, 0, 'f', 1));
    } else if (elapsedMs > kParserSlowMs && ++parser.slowHits > kParserSlowLimit) {
        quarantine(parser, QStringLiteral("matched slower than %1 ms on %2 lines").arg(kParserSlowMs).arg(parser.slowHits));
    }
}

void PluginParserChain::quarantine(Parser& parser, const QString& message) {
    if (parser.quarantined) return;
    parser.quarantined = true;
    flushBlock(parser, false, QStringLiteral("close"));
    qWarning().noquote() << QStringLiteral("plugin parser %1/%2 quarantined: %3").arg(parser.pluginId, parser.parserId, message);
    reportProblem(parser.pluginId, parser.parserId, QStringLiteral("quarantined"), message);
}

void PluginParserChain::feedLine(Parser& parser, const QString& line) {
    const QRegularExpressionMatch m = parser.match->match(line);
    if (!m.hasMatch()) return;
    const qint64 now = nowMs();
    emitMatch(parser, QJsonObject{{"line", line},
                                  {"lines", QJsonArray{line}},
                                  {"groups", namedGroups(*parser.match, m)},
                                  {"captures", numberedGroups(m)},
                                  {"entries", QJsonArray()},
                                  {"complete", true},
                                  {"reason", "match"},
                                  {"startedAt", now},
                                  {"endedAt", now}});
}

bool PluginParserChain::isIgnored(const Parser& parser, const QString& line) const {
    if (parser.ignore && matches(*parser.ignore, line)) return true;
    return parser.ignoreStatusReports && matches(statusReportPattern(), line);
}

bool PluginParserChain::isTerminator(const Parser& parser, const QString& line) const {
    if (parser.end && matches(*parser.end, line)) return true;
    if (parser.untilKind == u"ok") return matches(okPattern(), line);
    if (parser.untilKind == u"error") return matches(errorPattern(), line);
    if (parser.untilKind == u"ok-or-error") return matches(okPattern(), line) || matches(errorPattern(), line);
    return false;
}

void PluginParserChain::feedBlock(Parser& parser, const QString& line) {
    const QString endReason = parser.end ? QStringLiteral("end") : QStringLiteral("until");
    if (!parser.block) {
        const QRegularExpressionMatch opened = parser.begin->match(line);
        if (!opened.hasMatch()) return;
        Parser::Block block;
        block.lines.append(line);
        block.groups = namedGroups(*parser.begin, opened);
        block.captures = numberedGroups(opened);
        block.startedAt = nowMs();
        parser.block = std::move(block);
        collectEntry(parser, line);
        // The opening line may also close a one-line block.
        if (isTerminator(parser, line)) flushBlock(parser, true, endReason);
        return;
    }
    // Interleaved noise (status reports, by default) neither ends nor counts toward the block.
    if (isIgnored(parser, line)) return;
    if (isTerminator(parser, line)) {
        parser.block->lines.append(line);
        collectEntry(parser, line);
        flushBlock(parser, true, endReason);
        return;
    }
    if (parser.restartOnBegin && matches(*parser.begin, line)) {
        flushBlock(parser, false, QStringLiteral("restart"));
        feedBlock(parser, line);
        return;
    }
    if (parser.strict && !(parser.match && matches(*parser.match, line))) {
        flushBlock(parser, false, QStringLiteral("strict"));
        return;
    }
    parser.block->lines.append(line);
    collectEntry(parser, line);
    if (parser.block->lines.size() >= parser.maxLines) flushBlock(parser, false, QStringLiteral("maxLines"));
}

void PluginParserChain::collectEntry(Parser& parser, const QString& line) {
    if (!parser.match || !parser.block) return;
    const QRegularExpressionMatch m = parser.match->match(line);
    if (!m.hasMatch()) return;
    parser.block->entries.append(QJsonObject{{"line", line}, {"groups", namedGroups(*parser.match, m)}, {"captures", numberedGroups(m)}});
}

void PluginParserChain::flushBlock(Parser& parser, bool complete, const QString& reason) {
    if (!parser.block) return;
    Parser::Block block = std::move(*parser.block);
    parser.block.reset();
    if (!complete && !parser.emitPartial) return;
    emitMatch(parser, QJsonObject{{"line", QJsonValue(QJsonValue::Null)},
                                  {"lines", block.lines},
                                  {"groups", block.groups},
                                  {"captures", block.captures},
                                  {"entries", block.entries},
                                  {"complete", complete},
                                  {"reason", reason},
                                  {"startedAt", block.startedAt},
                                  {"endedAt", nowMs()}});
}

void PluginParserChain::emitMatch(Parser& parser, QJsonObject payload) {
    const qint64 now = nowMs();
    if (now - parser.emitWindowStart >= 1000) {
        parser.emitWindowStart = now;
        parser.emitCount = 0;
        parser.rateLimitReported = false;
    }
    if (++parser.emitCount > MaxEmitsPerSecond) {
        if (!parser.rateLimitReported) {
            parser.rateLimitReported = true;
            reportProblem(parser.pluginId, parser.parserId, QStringLiteral("rate-limited"),
                          QStringLiteral("more than %1 matches in one second - further matches in this window were dropped")
                              .arg(MaxEmitsPerSecond));
        }
        return;
    }
    ++parser.seq;
    payload.insert(QStringLiteral("pluginId"), parser.pluginId);
    payload.insert(QStringLiteral("parserId"), parser.parserId);
    payload.insert(QStringLiteral("mode"), parser.mode);
    payload.insert(QStringLiteral("seq"), parser.seq);
    if (deliver_) deliver_(parser.pluginId, QStringLiteral("parser"), payload);
}

void PluginParserChain::reportProblem(const QString& pluginId, const QString& parserId, const QString& reason,
                                      const QString& message) {
    if (deliver_) {
        deliver_(pluginId, QStringLiteral("parser"),
                 QJsonObject{{"pluginId", pluginId}, {"parserId", parserId}, {"reason", reason}, {"message", message}, {"error", true}});
    }
}

bool PluginParserChain::beginCapture(const QueryOptions& options, QueryDone done) {
    if (capture_) return false;
    capture_ = std::make_unique<Capture>();
    capture_->options = options;
    capture_->done = std::move(done);
    capture_->startedAt = nowMs();
    updateSweepTimer();
    return true;
}

void PluginParserChain::finishCapture(bool complete, const QString& reason) {
    if (!capture_) return;
    const std::unique_ptr<Capture> c = std::move(capture_);
    QJsonValue error = QJsonValue(QJsonValue::Undefined);
    bool ok = false;
    for (const QJsonValue& v : c->lines) {
        const QString line = v.toString();
        ok = ok || matches(okPattern(), line);
        if (error.isUndefined() && matches(errorPattern(), line)) error = line;
    }
    QJsonObject result{{"lines", c->lines},
                       {"ok", ok},
                       {"complete", complete},
                       {"reason", reason},
                       {"durationMs", nowMs() - c->startedAt}};
    if (!error.isUndefined()) result.insert(QStringLiteral("error"), error);
    updateSweepTimer();
    if (c->done) c->done(result);
}

void PluginParserChain::sweep() {
    const qint64 now = nowMs();
    for (auto& parser : parsers_) {
        if (parser->block && now - parser->block->startedAt >= parser->timeoutMs) {
            flushBlock(*parser, false, QStringLiteral("timeout"));
        }
    }
    if (capture_ && now - capture_->startedAt >= capture_->options.timeoutMs) {
        finishCapture(false, QStringLiteral("timeout"));
    }
}

void PluginParserChain::reset(const QString& reason) {
    for (auto& parser : parsers_) flushBlock(*parser, false, reason);
    finishCapture(false, reason);
    std::erase_if(parsers_, [](const auto& p) { return !p->manifest; });
    updateSweepTimer();
}

void PluginParserChain::updateSweepTimer() {
    const bool needed = !parsers_.empty() || capture_;
    if (needed && !sweepTimer_.isActive()) {
        sweepTimer_.start();
    } else if (!needed) {
        sweepTimer_.stop();
    }
}

}  // namespace gs::app
