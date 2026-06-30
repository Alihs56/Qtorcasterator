// ============================================================
// فایل: agents/build_pipeline.cpp
// ============================================================
#include "build_pipeline.h"
#include "terminal_executor.h"
#include "api_client.h"
#include "logger.h"
#include <QRegularExpression>

BuildPipeline::BuildPipeline(TerminalExecutor *executor, ApiClient *api,
                             QObject *parent)
    : QObject(parent), m_executor(executor), m_api(api) {}

void BuildPipeline::setBuildDir(const QString &dir) {
    m_buildDir = dir;
}

void BuildPipeline::setBuildCommand(const QString &cmd, const QStringList &args) {
    m_buildCmd = cmd;
    m_buildArgs = args;
}

bool BuildPipeline::isBuilding() const {
    return m_building;
}

int BuildPipeline::build() {
    m_building = true;
    int id = m_executor->execute(m_buildCmd, m_buildArgs, m_buildDir);
    emit buildStarted(id);

    connect(m_executor, &TerminalExecutor::outputReady, this,
            [this, id](int eid, const QString &output) {
                if (eid == id) emit buildProgress(id, output);
            });

    connect(m_executor, &TerminalExecutor::finished, this,
            [this, id](int eid, int exitCode, const QString &fullOutput) {
                if (eid != id) return;
                m_building = false;

                BuildResult result;
                result.exitCode = exitCode;
                result.output = fullOutput;
                result.success = (exitCode == 0);
                parseErrors(fullOutput, result.errors, result.warnings);

                emit buildDone(id, result);
                LOG_INFO("Build", QString("exit=%1, errors=%2, warnings=%3")
                                      .arg(exitCode)
                                      .arg(result.errors.size())
                                      .arg(result.warnings.size()));
            });

    return id;
}

int BuildPipeline::buildWithFix() {
    // استفاده از مکانیزم جدید نسخه جدید برای جلوگیری از چندبار اجرا
    int id = build();

    auto *connection = new QMetaObject::Connection;
    *connection = connect(this, &BuildPipeline::buildDone, this,
                          [this, id, connection](int bid, const BuildResult &result) {
                              if (bid != id) return;

                              disconnect(*connection);
                              delete connection;

                              if (result.success) {
                                  emit fixDone(id, true, "Build succeeded. No fixes needed.");
                                  return;
                              }

                              emit fixStarted(id);
                              QString prompt = generateFixPrompt(result);

                              m_api->sendChatRequest(8003, prompt,
                                                     "You are a C++/Qt expert. Provide minimal fix suggestions.",
                                                     [this, id](const AIResponse &res) {
                                                         if (res.error) {
                                                             emit fixDone(id, false, "API Error: " + res.errorMessage);
                                                             LOG_ERROR("Build", "Fix API error: " + res.errorMessage);
                                                         } else {
                                                             emit fixDone(id, true, res.content);
                                                             LOG_INFO("Build", "Expert fix suggestion received");
                                                         }
                                                     });
                          });

    return id;
}

void BuildPipeline::parseErrors(const QString &output, QStringList &errors,
                                QStringList &warnings) const {
    QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        QRegularExpression errRe(R"((.+?):(\d+):(\d+):\s+error:\s+(.+))");
        auto m = errRe.match(line);
        if (m.hasMatch()) {
            errors << line.trimmed();
            continue;
        }
        QRegularExpression warnRe(R"((.+?):(\d+):(\d+):\s+warning:\s+(.+))");
        auto wm = warnRe.match(line);
        if (wm.hasMatch()) {
            warnings << line.trimmed();
        }
    }
}

QString BuildPipeline::generateFixPrompt(const BuildResult &result) const {
    QString prompt = "The following build failed with exit code " +
                     QString::number(result.exitCode) + ".\n\n";
    prompt += "Build errors:\n";
    for (const QString &e : result.errors) {
        prompt += "  " + e + "\n";
    }
    if (!result.warnings.isEmpty()) {
        prompt += "\nWarnings:\n";
        for (const QString &w : result.warnings) {
            prompt += "  " + w + "\n";
        }
    }
    prompt += "\nPlease provide the minimal fix(es) needed to resolve these errors. "
              "Show the corrected code snippets.";
    return prompt;
}