#include "plugin_manifest.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace gs::app {

namespace {

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static SemVer parse(const QString& str) {
        static const QRegularExpression re(QStringLiteral(R"(^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?)"));
        const auto match = re.match(str.trimmed());
        if (!match.hasMatch()) {
            return {};
        }
        return {
            match.captured(1).toInt(),
            match.captured(2).isEmpty() ? 0 : match.captured(2).toInt(),
            match.captured(3).isEmpty() ? 0 : match.captured(3).toInt()
        };
    }

    bool operator>=(const SemVer& o) const {
        if (major != o.major) return major > o.major;
        if (minor != o.minor) return minor > o.minor;
        return patch >= o.patch;
    }

    bool operator<(const SemVer& o) const {
        return !(*this >= o);
    }
};

}  // namespace

bool parsePluginManifest(const QString& jsonString, PluginManifest* outManifest, QString* outError) {
    if (!outManifest) {
        if (outError) *outError = QStringLiteral("Null output pointer");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (outError) *outError = parseError.errorString();
        return false;
    }

    const QJsonObject obj = doc.object();
    outManifest->id = obj.value(QStringLiteral("id")).toString();
    outManifest->name = obj.value(QStringLiteral("name")).toString();
    outManifest->version = obj.value(QStringLiteral("version")).toString();
    outManifest->description = obj.value(QStringLiteral("description")).toString();
    outManifest->author = obj.value(QStringLiteral("author")).toString();
    outManifest->engine = obj.value(QStringLiteral("engine")).toString();

    // Wasm entry
    if (obj.contains(QStringLiteral("wasm")) && obj[QStringLiteral("wasm")].isObject()) {
        const QJsonObject wasmObj = obj[QStringLiteral("wasm")].toObject();
        outManifest->wasmEntry = wasmObj.value(QStringLiteral("entry")).toString();
    }

    // UI entry & contributions
    if (obj.contains(QStringLiteral("ui")) && obj[QStringLiteral("ui")].isObject()) {
        const QJsonObject uiObj = obj[QStringLiteral("ui")].toObject();
        outManifest->uiEntry = uiObj.value(QStringLiteral("entry")).toString();
        if (uiObj.contains(QStringLiteral("contributions")) && uiObj[QStringLiteral("contributions")].isArray()) {
            const QJsonArray contribArr = uiObj[QStringLiteral("contributions")].toArray();
            for (const auto& item : contribArr) {
                if (item.isObject()) {
                    const QJsonObject c = item.toObject();
                    PluginContribution contrib;
                    contrib.slot = c.value(QStringLiteral("slot")).toString();
                    contrib.label = c.value(QStringLiteral("label")).toString();
                    contrib.icon = c.value(QStringLiteral("icon")).toString();
                    contrib.route = c.value(QStringLiteral("route")).toString();
                    contrib.requiresIdle = c.value(QStringLiteral("requiresIdle")).toBool(false);
                    outManifest->contributions.push_back(contrib);
                }
            }
        }
    }

    // Capabilities
    if (obj.contains(QStringLiteral("capabilities")) && obj[QStringLiteral("capabilities")].isObject()) {
        const QJsonObject capObj = obj[QStringLiteral("capabilities")].toObject();
        if (capObj.contains(QStringLiteral("requestTypes")) && capObj[QStringLiteral("requestTypes")].isArray()) {
            for (const auto& item : capObj[QStringLiteral("requestTypes")].toArray()) {
                outManifest->capabilities.requestTypes.insert(item.toString());
            }
        }
        if (capObj.contains(QStringLiteral("topics")) && capObj[QStringLiteral("topics")].isArray()) {
            for (const auto& item : capObj[QStringLiteral("topics")].toArray()) {
                outManifest->capabilities.topics.insert(item.toString());
            }
        }
    }

    // Parsers
    if (obj.contains(QStringLiteral("parsers")) && obj[QStringLiteral("parsers")].isArray()) {
        for (const auto& item : obj[QStringLiteral("parsers")].toArray()) {
            if (item.isObject()) {
                const QJsonObject p = item.toObject();
                PluginParserSpec spec;
                spec.pattern = p.value(QStringLiteral("pattern")).toString();
                spec.type = p.value(QStringLiteral("type")).toString(QStringLiteral("line"));
                outManifest->parsers.push_back(spec);
            }
        }
    }

    if (!outManifest->isValid()) {
        if (outError) *outError = QStringLiteral("Manifest missing required fields (id, name, or version)");
        return false;
    }

    return true;
}

bool loadPluginManifestFile(const QString& filePath, PluginManifest* outManifest, QString* outError) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) *outError = QStringLiteral("Could not open manifest file: %1").arg(filePath);
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    return parsePluginManifest(content, outManifest, outError);
}

bool isEngineCompatible(const QString& engineRange, const QString& currentVersion) {
    if (engineRange.trimmed().isEmpty()) {
        return true;
    }

    const SemVer current = SemVer::parse(currentVersion);
    const QString range = engineRange.trimmed();

    if (range.startsWith(QStringLiteral(">="))) {
        const SemVer minVer = SemVer::parse(range.mid(2).trimmed());
        return current >= minVer;
    }
    if (range.startsWith(QStringLiteral("^"))) {
        const SemVer base = SemVer::parse(range.mid(1).trimmed());
        if (current.major != base.major) return false;
        return current >= base;
    }

    const SemVer exact = SemVer::parse(range);
    return (current.major == exact.major && current.minor == exact.minor && current.patch == exact.patch);
}

}  // namespace gs::app
