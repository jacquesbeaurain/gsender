#pragma once

#include "app_settings.hpp"
#include "gs/expr/value.hpp"
#include "gs/job/program_analysis.hpp"

#include <QObject>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>

namespace gs::app {

class Machine;

class JobService : public QObject {
    Q_OBJECT

public:
    explicit JobService(Machine& machine, QObject* parent = nullptr);

    bool loadFile(const QString& path, QString* error = nullptr);
    void forgetRecentFile(const QString& path);
    void clearRecentFiles();
    bool runOutline(QString* error = nullptr);
    expr::Value fileContext() const;
    bool startFromLine(std::size_t line, double safeHeight);
    std::int64_t lastLine() const noexcept { return lastLine_; }
    void setLastLine(std::int64_t line) noexcept { lastLine_ = line; }

private:
    Machine& machine_;
    std::int64_t lastLine_ = 1;
};

}  // namespace gs::app
