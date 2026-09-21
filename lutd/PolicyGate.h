#pragma once

#include <QObject>
#include <QString>

namespace lut {

class PolicyGate : public QObject {
    Q_OBJECT

public:
    explicit PolicyGate(QObject *parent = nullptr);

    bool checkAuthorization(const QString &actionId, const QString &callerService, bool allowInteraction = true);

    static const QString ActionRefresh;
    static const QString ActionUpgrade;
    static const QString ActionInstall;
    static const QString ActionRemove;
    static const QString ActionManageOthers;
};

} // namespace lut
