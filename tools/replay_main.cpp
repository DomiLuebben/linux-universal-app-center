#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <iostream>
#include "liblut/backend/replay/ReplayBackend.h"
#include "liblut/progress/ProgressModel.h"
#include "liblut/liblut.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("lut-replay"));
    app.setApplicationVersion(lut::versionString());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linux Update Tool Replay Runner"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(QStringLiteral("fixture"), QStringLiteral("Path to .jsonl fixture file."));

    QCommandLineOption speedOption(
        QStringList() << QStringLiteral("s") << QStringLiteral("speed"),
        QStringLiteral("Replay speed factor (default 1.0, 0 for instant)."),
        QStringLiteral("factor"),
        QStringLiteral("1.0")
    );
    parser.addOption(speedOption);

    QCommandLineOption quietOption(
        QStringList() << QStringLiteral("q") << QStringLiteral("quiet"),
        QStringLiteral("Quiet mode (no progress output, exit code only).")
    );
    parser.addOption(quietOption);

    parser.process(app);

    const QStringList posArgs = parser.positionalArguments();
    if (posArgs.isEmpty()) {
        parser.showHelp(1);
    }

    const QString fixturePath = posArgs.first();
    if (!QFileInfo::exists(fixturePath)) {
        std::cerr << "Error: Fixture file not found: " << fixturePath.toStdString() << "\n";
        return 1;
    }

    double speed = parser.value(speedOption).toDouble();
    if (speed <= 0.0) speed = 0.0;
    bool quiet = parser.isSet(quietOption);

    lut::ReplayBackend backend(fixturePath, speed);
    lut::ProgressModel model;

    QObject::connect(&backend, &lut::Backend::eventEmitted, &model, &lut::ProgressModel::processEvent);

    int exitCode = 0;

    if (!quiet) {
        std::cout << "Replaying fixture: " << fixturePath.toStdString() << " (speed: " << speed << "x)\n";
    }

    if (speed == 0.0) {
        // Synchronous instant replay
        backend.runSynchronously([&](const lut::Event &ev) {
            if (!quiet) {
                if (std::holds_alternative<lut::PhaseChanged>(ev)) {
                    std::cout << "[Phase] " << lut::phaseToString(std::get<lut::PhaseChanged>(ev).phase).toStdString() << "\n";
                }
            }
        });
        if (model.currentPhase() == lut::Phase::Failed) {
            exitCode = 2;
        } else if (model.currentPhase() == lut::Phase::Cancelled) {
            exitCode = 3;
        }
        if (!quiet) {
            std::cout << "Instant replay finished. Progress: " << (model.totalProgress() * 100.0) << "%\n";
        }
        return exitCode;
    }

    // Asynchronous replay with timer
    QObject::connect(&model, &lut::ProgressModel::phaseChanged, [&](lut::Phase phase, const QString &label, bool) {
        if (!quiet) {
            std::cout << "\n>>> Phase: " << lut::phaseToString(phase).toStdString()
                      << " (" << label.toStdString() << ") - Total: "
                      << static_cast<int>(model.totalProgress() * 100) << "%\n";
        }
    });

    QObject::connect(&model, &lut::ProgressModel::activeItemChanged, [&](const QString &name, double prog) {
        if (!quiet && !name.isEmpty()) {
            std::cout << "\r    Item: " << name.toStdString() << " ["
                      << static_cast<int>(prog * 100) << "%]   " << std::flush;
        }
    });

    QObject::connect(&model, &lut::ProgressModel::questionReceived, [&](const lut::Question &q) {
        if (!quiet) {
            std::cout << "\n[Question] Auto-answering question " << q.id.toStdString() << "\n";
        }
        backend.answerQuestion(q.id, {});
    });

    QObject::connect(&backend, &lut::ReplayBackend::replayFinished, [&]() {
        if (!quiet) {
            std::cout << "\n=== Replay Finished. Final Progress: "
                      << static_cast<int>(model.totalProgress() * 100) << "% ===\n";
        }
        if (model.currentPhase() == lut::Phase::Failed) {
            exitCode = 2;
        } else if (model.currentPhase() == lut::Phase::Cancelled) {
            exitCode = 3;
        }
        app.quit();
    });

    backend.start();
    app.exec();
    return exitCode;
}
